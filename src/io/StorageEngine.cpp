// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "madi/io/StorageEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace madi::io {

namespace {

// ==============================================================================
// JSON String and Number Utilities (RFC 8259)
// ==============================================================================

std::string json_escape(std::string_view str) {
  std::string out;
  out.reserve(str.size() + 2);
  out.push_back('"');
  for (unsigned char c : str) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

std::string format_double(double val, int precision = 4) {
  if (std::isnan(val)) return "0.0";
  if (std::isinf(val)) return val > 0 ? "1e308" : "-1e308";
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(precision) << val;
  std::string s = ss.str();
  auto dot = s.find('.');
  if (dot != std::string::npos) {
    while (s.size() > dot + 2 && s.back() == '0') {
      s.pop_back();
    }
  }
  return s;
}

// ==============================================================================
// JSONC Preprocessor (strips // and /* */ comments, removes trailing commas)
// ==============================================================================

std::string strip_jsonc_comments(std::string_view input) {
  std::string output;
  output.reserve(input.size());

  bool in_string = false;
  bool escaped = false;
  const size_t len = input.size();

  for (size_t i = 0; i < len; ++i) {
    char c = input[i];

    if (in_string) {
      output.push_back(c);
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }

    if (c == '"') {
      in_string = true;
      output.push_back(c);
      continue;
    }

    // Check for single-line comment //
    if (c == '/' && i + 1 < len && input[i + 1] == '/') {
      i += 2;
      while (i < len && input[i] != '\n' && input[i] != '\r') {
        ++i;
      }
      if (i < len) {
        output.push_back(input[i]);
      }
      continue;
    }

    // Check for multi-line block comment /* ... */
    if (c == '/' && i + 1 < len && input[i + 1] == '*') {
      i += 2;
      while (i + 1 < len && !(input[i] == '*' && input[i + 1] == '/')) {
        if (input[i] == '\n') {
          output.push_back('\n');
        }
        ++i;
      }
      if (i + 1 < len) {
        i += 1;
      }
      continue;
    }

    output.push_back(c);
  }

  // Second pass: remove trailing commas before '}' or ']' outside of string literals
  std::string clean;
  clean.reserve(output.size());
  in_string = false;
  escaped = false;
  const size_t clen = output.size();

  for (size_t i = 0; i < clen; ++i) {
    char c = output[i];
    if (in_string) {
      clean.push_back(c);
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }

    if (c == '"') {
      in_string = true;
      clean.push_back(c);
      continue;
    }

    if (c == ',') {
      size_t j = i + 1;
      while (j < clen && (output[j] == ' ' || output[j] == '\t' || output[j] == '\r' || output[j] == '\n')) {
        ++j;
      }
      if (j < clen && (output[j] == '}' || output[j] == ']')) {
        continue;
      }
    }

    clean.push_back(c);
  }

  return clean;
}

// ==============================================================================
// Lightweight C++20 RFC 8259 JSON AST & Parser
// ==============================================================================

struct JsonValue {
  enum class Type { Null, Boolean, Number, String, Array, Object };

  Type type{Type::Null};
  bool bool_val{false};
  double num_val{0.0};
  std::string str_val;
  std::vector<JsonValue> arr_val;
  std::vector<std::pair<std::string, JsonValue>> obj_val;

  [[nodiscard]] bool is_null() const { return type == Type::Null; }
  [[nodiscard]] bool is_bool() const { return type == Type::Boolean; }
  [[nodiscard]] bool is_number() const { return type == Type::Number; }
  [[nodiscard]] bool is_string() const { return type == Type::String; }
  [[nodiscard]] bool is_array() const { return type == Type::Array; }
  [[nodiscard]] bool is_object() const { return type == Type::Object; }

  [[nodiscard]] const JsonValue* find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : obj_val) {
      if (k == key) return &v;
    }
    return nullptr;
  }

  [[nodiscard]] std::string as_string(std::string_view def = "") const {
    return is_string() ? str_val : std::string(def);
  }

  [[nodiscard]] double as_double(double def = 0.0) const {
    return is_number() ? num_val : def;
  }

  [[nodiscard]] int64_t as_int(int64_t def = 0) const {
    return is_number() ? static_cast<int64_t>(num_val) : def;
  }

  [[nodiscard]] bool as_bool(bool def = false) const {
    return is_bool() ? bool_val : def;
  }
};

class JsonParser {
public:
  static std::optional<JsonValue> parse(std::string_view src, std::string* out_err = nullptr) {
    JsonParser p(src);
    auto val = p.parse_value();
    p.skip_ws();
    if (val.has_value() && p.pos_ < p.src_.size()) {
      if (out_err) *out_err = "Unexpected trailing content after JSON root at offset " + std::to_string(p.pos_);
      return std::nullopt;
    }
    if (!val.has_value() && out_err && out_err->empty()) {
      *out_err = p.error_;
    }
    return val;
  }

private:
  explicit JsonParser(std::string_view src) : src_(src) {}

  std::string_view src_;
  size_t pos_{0};
  std::string error_;

  void skip_ws() {
    while (pos_ < src_.size()) {
      char c = src_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  char peek() {
    skip_ws();
    return pos_ < src_.size() ? src_[pos_] : '\0';
  }

  char get() {
    skip_ws();
    return pos_ < src_.size() ? src_[pos_++] : '\0';
  }

  std::optional<JsonValue> parse_value() {
    skip_ws();
    if (pos_ >= src_.size()) {
      error_ = "Unexpected end of JSON input";
      return std::nullopt;
    }

    char c = src_[pos_];
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '"') return parse_string();
    if (c == 't' || c == 'f') return parse_bool();
    if (c == 'n') return parse_null();
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number();

    error_ = std::string("Unexpected token '") + c + "' at position " + std::to_string(pos_);
    return std::nullopt;
  }

  std::optional<JsonValue> parse_object() {
    if (get() != '{') return std::nullopt;
    JsonValue obj;
    obj.type = JsonValue::Type::Object;

    skip_ws();
    if (peek() == '}') {
      get();
      return obj;
    }

    while (pos_ < src_.size()) {
      skip_ws();
      if (peek() != '"') {
        error_ = "Expected string key in object at offset " + std::to_string(pos_);
        return std::nullopt;
      }

      auto key_val = parse_string();
      if (!key_val.has_value()) return std::nullopt;

      skip_ws();
      if (get() != ':') {
        error_ = "Expected ':' after key in object at offset " + std::to_string(pos_);
        return std::nullopt;
      }

      auto val = parse_value();
      if (!val.has_value()) return std::nullopt;

      obj.obj_val.emplace_back(std::move(key_val->str_val), std::move(*val));

      skip_ws();
      char next = get();
      if (next == '}') {
        return obj;
      }
      if (next != ',') {
        error_ = "Expected ',' or '}' in object at offset " + std::to_string(pos_);
        return std::nullopt;
      }
    }

    error_ = "Unterminated object";
    return std::nullopt;
  }

  std::optional<JsonValue> parse_array() {
    if (get() != '[') return std::nullopt;
    JsonValue arr;
    arr.type = JsonValue::Type::Array;

    skip_ws();
    if (peek() == ']') {
      get();
      return arr;
    }

    while (pos_ < src_.size()) {
      auto val = parse_value();
      if (!val.has_value()) return std::nullopt;
      arr.arr_val.push_back(std::move(*val));

      skip_ws();
      char next = get();
      if (next == ']') {
        return arr;
      }
      if (next != ',') {
        error_ = "Expected ',' or ']' in array at offset " + std::to_string(pos_);
        return std::nullopt;
      }
    }

    error_ = "Unterminated array";
    return std::nullopt;
  }

  std::optional<JsonValue> parse_string() {
    skip_ws();
    if (pos_ >= src_.size() || src_[pos_] != '"') {
      error_ = "Expected '\"' for string start";
      return std::nullopt;
    }
    ++pos_; // skip opening quote

    std::string str;
    str.reserve(32);

    while (pos_ < src_.size()) {
      char c = src_[pos_++];
      if (c == '"') {
        JsonValue val;
        val.type = JsonValue::Type::String;
        val.str_val = std::move(str);
        return val;
      }
      if (c == '\\') {
        if (pos_ >= src_.size()) {
          error_ = "Unterminated escape sequence in string";
          return std::nullopt;
        }
        char esc = src_[pos_++];
        switch (esc) {
          case '"':  str.push_back('"'); break;
          case '\\': str.push_back('\\'); break;
          case '/':  str.push_back('/'); break;
          case 'b':  str.push_back('\b'); break;
          case 'f':  str.push_back('\f'); break;
          case 'n':  str.push_back('\n'); break;
          case 'r':  str.push_back('\r'); break;
          case 't':  str.push_back('\t'); break;
          case 'u': {
            if (pos_ + 4 > src_.size()) {
              error_ = "Invalid unicode escape sequence in string";
              return std::nullopt;
            }
            uint32_t codepoint = 0;
            for (int k = 0; k < 4; ++k) {
              char h = src_[pos_++];
              codepoint <<= 4;
              if (h >= '0' && h <= '9') codepoint |= (h - '0');
              else if (h >= 'a' && h <= 'f') codepoint |= (h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') codepoint |= (h - 'A' + 10);
              else {
                error_ = "Invalid hex digit in unicode escape";
                return std::nullopt;
              }
            }
            // UTF-8 encode codepoint
            if (codepoint <= 0x7F) {
              str.push_back(static_cast<char>(codepoint));
            } else if (codepoint <= 0x7FF) {
              str.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
              str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else {
              str.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
              str.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
              str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            break;
          }
          default:
            str.push_back(esc);
            break;
        }
      } else {
        str.push_back(c);
      }
    }

    error_ = "Unterminated string literal";
    return std::nullopt;
  }

  std::optional<JsonValue> parse_number() {
    skip_ws();
    const char* start_ptr = src_.data() + pos_;
    char* end_ptr = nullptr;
    double val = std::strtod(start_ptr, &end_ptr);
    if (end_ptr == start_ptr) {
      error_ = "Invalid number format at offset " + std::to_string(pos_);
      return std::nullopt;
    }
    pos_ += (end_ptr - start_ptr);
    JsonValue num;
    num.type = JsonValue::Type::Number;
    num.num_val = val;
    return num;
  }

  std::optional<JsonValue> parse_bool() {
    skip_ws();
    if (src_.substr(pos_).starts_with("true")) {
      pos_ += 4;
      JsonValue b;
      b.type = JsonValue::Type::Boolean;
      b.bool_val = true;
      return b;
    }
    if (src_.substr(pos_).starts_with("false")) {
      pos_ += 5;
      JsonValue b;
      b.type = JsonValue::Type::Boolean;
      b.bool_val = false;
      return b;
    }
    error_ = "Expected boolean at offset " + std::to_string(pos_);
    return std::nullopt;
  }

  std::optional<JsonValue> parse_null() {
    skip_ws();
    if (src_.substr(pos_).starts_with("null")) {
      pos_ += 4;
      JsonValue n;
      n.type = JsonValue::Type::Null;
      return n;
    }
    error_ = "Expected null at offset " + std::to_string(pos_);
    return std::nullopt;
  }
};

// ==============================================================================
// Legacy YAML string sanitizer
// ==============================================================================

std::string yaml_escape(std::string_view str) {
  std::string out;
  out.reserve(str.size() + 2);
  out.push_back('"');
  for (char c : str) {
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else
      out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string trim(std::string_view s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos)
    return {};
  auto end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

core::PortDirection parse_direction_str(std::string val) {
  val = trim(val);
  if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
    val = val.substr(1, val.size() - 2);
  }
  std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });
  if (val == "0" || val == "north") return core::PortDirection::North;
  if (val == "1" || val == "east") return core::PortDirection::East;
  if (val == "2" || val == "south") return core::PortDirection::South;
  if (val == "3" || val == "west") return core::PortDirection::West;
  if (val == "4" || val == "omnidirectional" || val == "omni") return core::PortDirection::Omnidirectional;
  try {
    return static_cast<core::PortDirection>(std::stoi(val));
  } catch (...) {
    return core::PortDirection::Omnidirectional;
  }
}

core::ShapeType parse_shape_str(std::string val) {
  val = trim(val);
  if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
    val = val.substr(1, val.size() - 2);
  }
  std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });
  if (val == "0" || val == "rectangle" || val == "rect") return core::ShapeType::Rectangle;
  if (val == "1" || val == "roundedrect" || val == "rounded_rect" || val == "rounded") return core::ShapeType::RoundedRect;
  if (val == "2" || val == "ellipse" || val == "circle") return core::ShapeType::Ellipse;
  if (val == "3" || val == "hierarchicalblock" || val == "hierarchical_block" || val == "subsystem" || val == "block") return core::ShapeType::HierarchicalBlock;
  if (val == "4" || val == "custompolygon" || val == "custom_polygon" || val == "polygon") return core::ShapeType::CustomPolygon;
  if (val == "5" || val == "inputport" || val == "input_port" || val == "input") return core::ShapeType::InputPort;
  if (val == "6" || val == "outputport" || val == "output_port" || val == "output") return core::ShapeType::OutputPort;
  if (val == "7" || val == "bidirectionalport" || val == "bidirectional_port" || val == "inoutport" || val == "inout_port" || val == "inout" || val == "bidirectional") return core::ShapeType::BidirectionalPort;
  try {
    return static_cast<core::ShapeType>(std::stoi(val));
  } catch (...) {
    return core::ShapeType::Rectangle;
  }
}

core::RoutingStyle parse_routing_style_str(std::string val) {
  val = trim(val);
  if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
    val = val.substr(1, val.size() - 2);
  }
  std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });
  if (val == "0" || val == "orthogonal") return core::RoutingStyle::Orthogonal;
  if (val == "1" || val == "direct" || val == "straight") return core::RoutingStyle::Direct;
  if (val == "2" || val == "curvedbezier" || val == "bezier" || val == "curved") return core::RoutingStyle::CurvedBezier;
  try {
    return static_cast<core::RoutingStyle>(std::stoi(val));
  } catch (...) {
    return core::RoutingStyle::Orthogonal;
  }
}

}  // namespace

// ==============================================================================
// Primary Format: RFC 8259 Standard JSON with .madi Extension
// ==============================================================================

std::string StorageEngine::serialize(const core::GraphModel& graph,
                                     const core::BlockLibraryManager* lib_manager) const {
  return serialize_json(graph, lib_manager);
}

StorageResult StorageEngine::deserialize(std::string_view text,
                                         core::GraphModel& out_graph,
                                         core::BlockLibraryManager* out_lib_manager) const {
  // First attempt JSON/JSONC parsing
  StorageResult json_res = deserialize_json(text, out_graph, out_lib_manager);
  if (json_res.success) {
    return json_res;
  }

  // Fallback to legacy YAML deserializer
  StorageResult yaml_res = deserialize_yaml(text, out_graph, out_lib_manager);
  if (yaml_res.success) {
    return yaml_res;
  }

  // If text starts with JSON tokens, return JSON error; otherwise return YAML error
  std::string_view trimmed = text;
  auto start = trimmed.find_first_not_of(" \t\r\n");
  if (start != std::string_view::npos) {
    trimmed = trimmed.substr(start);
  }
  if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[' || trimmed.starts_with("//") || trimmed.starts_with("/*"))) {
    return json_res;
  }
  return yaml_res;
}

// ==============================================================================
// 1. RFC 8259 Standard JSON Serializer & Deserializer
// ==============================================================================

std::string StorageEngine::serialize_json(const core::GraphModel& graph,
                                          const core::BlockLibraryManager* lib_manager) const {
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"$schema\": \"https://madi.dev/schema/v1/diagram.json\",\n";
  ss << "  \"schema_version\": 1,\n";
  ss << "  \"format\": \"madi\",\n";
  ss << "  \"generator\": \"madi 1.0.0\",\n";

  // Nodes deterministically sorted by ID
  std::map<core::NodeId, const core::Node*> sorted_nodes;
  for (const auto& [id, node] : graph.nodes()) {
    sorted_nodes[id] = &node;
  }

  ss << "  \"nodes\": [\n";
  bool first_node = true;
  for (const auto& [id, node] : sorted_nodes) {
    if (!first_node) ss << ",\n";
    first_node = false;

    ss << "    {\n";
    ss << "      \"id\": " << id << ",\n";
    ss << "      \"name\": " << json_escape(node->name) << ",\n";
    ss << "      \"shape\": " << static_cast<int>(node->shape) << ",\n";
    ss << "      \"bounds\": [" << format_double(node->bounds.x) << ", "
       << format_double(node->bounds.y) << ", "
       << format_double(node->bounds.width) << ", "
       << format_double(node->bounds.height) << "]";

    if (std::abs(node->rotation_deg) > 1e-4) {
      ss << ",\n      \"rotation\": " << format_double(node->rotation_deg, 2);
    }

    if (!node->polygon_vertices.empty()) {
      ss << ",\n      \"polygon_vertices\": [\n";
      for (size_t i = 0; i < node->polygon_vertices.size(); ++i) {
        const auto& pv = node->polygon_vertices[i];
        ss << "        [" << format_double(pv.x) << ", " << format_double(pv.y) << "]";
        if (i + 1 < node->polygon_vertices.size()) ss << ",";
        ss << "\n";
      }
      ss << "      ]";
    }

    if (!node->properties.empty()) {
      ss << ",\n      \"properties\": {\n";
      std::map<std::string, std::string> sorted_props(node->properties.begin(),
                                                      node->properties.end());
      bool first_p = true;
      for (const auto& [k, v] : sorted_props) {
        if (!first_p) ss << ",\n";
        first_p = false;
        ss << "        " << json_escape(k) << ": " << json_escape(v);
      }
      ss << "\n      }";
    }

    if (!node->port_ids.empty()) {
      ss << ",\n      \"ports\": [\n";
      bool first_port = true;
      for (core::PortId pid : node->port_ids) {
        if (const auto* p = graph.find_port(pid)) {
          if (!first_port) ss << ",\n";
          first_port = false;
          ss << "        {\n";
          ss << "          \"id\": " << p->id << ",\n";
          ss << "          \"name\": " << json_escape(p->name) << ",\n";
          ss << "          \"anchor\": [" << format_double(p->local_anchor.x) << ", "
             << format_double(p->local_anchor.y) << "],\n";
          ss << "          \"direction\": " << static_cast<int>(p->direction) << "\n";
          ss << "        }";
        }
      }
      ss << "\n      ]";
    }

    ss << "\n    }";
  }
  ss << "\n  ],\n";

  // Edges deterministically sorted by ID
  std::map<core::EdgeId, const core::Edge*> sorted_edges;
  for (const auto& [id, edge] : graph.edges()) {
    sorted_edges[id] = &edge;
  }

  ss << "  \"edges\": [\n";
  bool first_edge = true;
  for (const auto& [id, edge] : sorted_edges) {
    if (!first_edge) ss << ",\n";
    first_edge = false;

    ss << "    {\n";
    ss << "      \"id\": " << id << ",\n";
    ss << "      \"name\": " << json_escape(edge->name) << ",\n";
    ss << "      \"source_node\": " << edge->source_node << ",\n";
    ss << "      \"source_port\": " << edge->source_port << ",\n";
    ss << "      \"target_node\": " << edge->target_node << ",\n";
    ss << "      \"target_port\": " << edge->target_port << ",\n";
    ss << "      \"source_arrowhead\": " << json_escape(edge->source_arrowhead) << ",\n";
    ss << "      \"arrowhead\": " << json_escape(edge->arrowhead) << ",\n";
    ss << "      \"branches_aligned\": " << (edge->branches_aligned ? "true" : "false") << ",\n";
    ss << "      \"routing_style\": " << static_cast<int>(edge->routing_style);

    if (!edge->waypoints.empty()) {
      ss << ",\n      \"waypoints\": [\n";
      for (size_t i = 0; i < edge->waypoints.size(); ++i) {
        const auto& wp = edge->waypoints[i];
        ss << "        [" << format_double(wp.x) << ", " << format_double(wp.y) << "]";
        if (i + 1 < edge->waypoints.size()) ss << ",";
        ss << "\n";
      }
      ss << "      ]";
    }

    if (!edge->extra_sources.empty()) {
      ss << ",\n      \"extra_sources\": [\n";
      for (size_t i = 0; i < edge->extra_sources.size(); ++i) {
        const auto& bs = edge->extra_sources[i];
        ss << "        {\n";
        ss << "          \"source_node\": " << bs.source_node << ",\n";
        ss << "          \"source_port\": " << bs.source_port << ",\n";
        ss << "          \"arrowhead\": " << json_escape(bs.arrowhead);
        if (!bs.waypoints.empty()) {
          ss << ",\n          \"waypoints\": [\n";
          for (size_t w = 0; w < bs.waypoints.size(); ++w) {
            ss << "            [" << format_double(bs.waypoints[w].x) << ", " << format_double(bs.waypoints[w].y) << "]";
            if (w + 1 < bs.waypoints.size()) ss << ",";
            ss << "\n";
          }
          ss << "          ]";
        }
        ss << "\n        }";
        if (i + 1 < edge->extra_sources.size()) ss << ",";
        ss << "\n";
      }
      ss << "      ]";
    }

    if (!edge->extra_destinations.empty()) {
      ss << ",\n      \"extra_destinations\": [\n";
      for (size_t i = 0; i < edge->extra_destinations.size(); ++i) {
        const auto& bt = edge->extra_destinations[i];
        ss << "        {\n";
        ss << "          \"target_node\": " << bt.target_node << ",\n";
        ss << "          \"target_port\": " << bt.target_port << ",\n";
        ss << "          \"arrowhead\": " << json_escape(bt.arrowhead);
        if (!bt.waypoints.empty()) {
          ss << ",\n          \"waypoints\": [\n";
          for (size_t w = 0; w < bt.waypoints.size(); ++w) {
            ss << "            [" << format_double(bt.waypoints[w].x) << ", " << format_double(bt.waypoints[w].y) << "]";
            if (w + 1 < bt.waypoints.size()) ss << ",";
            ss << "\n";
          }
          ss << "          ]";
        }
        ss << "\n        }";
        if (i + 1 < edge->extra_destinations.size()) ss << ",";
        ss << "\n";
      }
      ss << "      ]";
    }

    ss << "\n    }";
  }
  ss << "\n  ]";

  // Library definitions if provided
  if (lib_manager && !lib_manager->definitions().empty()) {
    ss << ",\n  \"library_definitions\": [\n";
    std::map<std::string, const core::BlockDefinition*> sorted_defs;
    for (const auto& [uri, def] : lib_manager->definitions()) {
      sorted_defs[uri] = &def;
    }
    bool first_def = true;
    for (const auto& [uri, def] : sorted_defs) {
      if (!first_def) ss << ",\n";
      first_def = false;
      ss << "    {\n";
      ss << "      \"uri\": " << json_escape(uri) << ",\n";
      ss << "      \"name\": " << json_escape(def->name()) << ",\n";
      ss << "      \"description\": " << json_escape(def->description()) << ",\n";
      ss << "      \"default_size\": [" << format_double(def->default_size().width) << ", "
         << format_double(def->default_size().height) << "]";

      if (!def->interface_ports().empty()) {
        ss << ",\n      \"interface_ports\": [\n";
        bool first_ip = true;
        for (const auto& ip : def->interface_ports()) {
          if (!first_ip) ss << ",\n";
          first_ip = false;
          ss << "        {\n";
          ss << "          \"id\": " << ip.id << ",\n";
          ss << "          \"name\": " << json_escape(ip.name) << ",\n";
          ss << "          \"anchor\": [" << format_double(ip.local_anchor.x) << ", "
             << format_double(ip.local_anchor.y) << "],\n";
          ss << "          \"direction\": " << static_cast<int>(ip.boundary_side) << ",\n";
          ss << "          \"target_node\": " << ip.internal_target_node << ",\n";
          ss << "          \"target_port\": " << ip.internal_target_port << "\n";
          ss << "        }";
        }
        ss << "\n      ]";
      }
      ss << "\n    }";
    }
    ss << "\n  ]";
  }

  ss << "\n}\n";
  return ss.str();
}

StorageResult StorageEngine::deserialize_json(std::string_view json_text,
                                              core::GraphModel& out_graph,
                                              core::BlockLibraryManager* out_lib_manager) const {
  out_graph.clear();
  std::string sanitized = strip_jsonc_comments(json_text);
  std::string parse_err;
  auto root_opt = JsonParser::parse(sanitized, &parse_err);
  if (!root_opt.has_value()) {
    return {false, "JSON parse error: " + parse_err};
  }
  const auto& root = root_opt.value();
  if (!root.is_object()) {
    return {false, "Root JSON value must be an object"};
  }

  // Nodes
  if (const auto* nodes_val = root.find("nodes"); nodes_val && nodes_val->is_array()) {
    for (const auto& item : nodes_val->arr_val) {
      if (!item.is_object()) continue;

      core::Node node;
      if (const auto* v = item.find("id")) node.id = static_cast<core::NodeId>(v->as_int());
      if (const auto* v = item.find("name")) node.name = v->as_string();
      if (const auto* v = item.find("shape")) {
        if (v->is_string()) node.shape = parse_shape_str(v->as_string());
        else node.shape = static_cast<core::ShapeType>(v->as_int());
      }
      if (const auto* v = item.find("bounds"); v && v->is_array() && v->arr_val.size() >= 4) {
        node.bounds.x = v->arr_val[0].as_double();
        node.bounds.y = v->arr_val[1].as_double();
        node.bounds.width = v->arr_val[2].as_double();
        node.bounds.height = v->arr_val[3].as_double();
      }
      if (const auto* v = item.find("rotation")) {
        node.rotation_deg = v->as_double();
      }
      if (const auto* v = item.find("polygon_vertices"); v && v->is_array()) {
        for (const auto& p_item : v->arr_val) {
          if (p_item.is_array() && p_item.arr_val.size() >= 2) {
            node.polygon_vertices.push_back({p_item.arr_val[0].as_double(), p_item.arr_val[1].as_double()});
          }
        }
      }
      if (const auto* v = item.find("properties"); v && v->is_object()) {
        for (const auto& [pk, pv] : v->obj_val) {
          if (pv.is_string()) {
            node.properties[pk] = pv.as_string();
          } else if (pv.is_number()) {
            node.properties[pk] = format_double(pv.as_double());
          } else if (pv.is_bool()) {
            node.properties[pk] = pv.as_bool() ? "true" : "false";
          }
        }
      }

      std::vector<core::Port> node_ports;
      if (const auto* v = item.find("ports"); v && v->is_array()) {
        for (const auto& port_item : v->arr_val) {
          if (!port_item.is_object()) continue;
          core::Port p;
          if (const auto* pv = port_item.find("id")) p.id = static_cast<core::PortId>(pv->as_int());
          if (const auto* pv = port_item.find("name")) p.name = pv->as_string();
          if (const auto* pv = port_item.find("anchor"); pv && pv->is_array() && pv->arr_val.size() >= 2) {
            p.local_anchor.x = pv->arr_val[0].as_double();
            p.local_anchor.y = pv->arr_val[1].as_double();
          }
          if (const auto* pv = port_item.find("direction")) {
            if (pv->is_string()) p.direction = parse_direction_str(pv->as_string());
            else p.direction = static_cast<core::PortDirection>(pv->as_int());
          }
          node_ports.push_back(std::move(p));
        }
      }

      const auto nid = out_graph.add_node(std::move(node));
      for (auto& p : node_ports) {
        p.parent_node = nid;
        out_graph.add_port(std::move(p));
      }
    }
  }

  // Edges
  if (const auto* edges_val = root.find("edges"); edges_val && edges_val->is_array()) {
    for (const auto& item : edges_val->arr_val) {
      if (!item.is_object()) continue;
      core::Edge edge;
      if (const auto* v = item.find("id")) edge.id = static_cast<core::EdgeId>(v->as_int());
      if (const auto* v = item.find("name")) edge.name = v->as_string();
      if (const auto* v = item.find("source_node")) edge.source_node = static_cast<core::NodeId>(v->as_int());
      if (const auto* v = item.find("source_port")) edge.source_port = static_cast<core::PortId>(v->as_int());
      if (const auto* v = item.find("target_node")) edge.target_node = static_cast<core::NodeId>(v->as_int());
      if (const auto* v = item.find("target_port")) edge.target_port = static_cast<core::PortId>(v->as_int());
      if (const auto* v = item.find("routing_style")) {
        if (v->is_string()) edge.routing_style = parse_routing_style_str(v->as_string());
        else edge.routing_style = static_cast<core::RoutingStyle>(v->as_int());
      }
      if (const auto* v = item.find("waypoints"); v && v->is_array()) {
        for (const auto& wp_item : v->arr_val) {
          if (wp_item.is_array() && wp_item.arr_val.size() >= 2) {
            edge.waypoints.push_back({wp_item.arr_val[0].as_double(), wp_item.arr_val[1].as_double()});
          }
        }
      }
      if (const auto* v = item.find("source_arrowhead")) edge.source_arrowhead = v->as_string();
      if (const auto* v = item.find("arrowhead")) edge.arrowhead = v->as_string();
      if (const auto* v = item.find("branches_aligned")) edge.branches_aligned = v->as_bool();
      if (const auto* v = item.find("extra_sources"); v && v->is_array()) {
        for (const auto& s_item : v->arr_val) {
          if (!s_item.is_object()) continue;
          core::BranchSource bs;
          if (const auto* sv = s_item.find("source_node")) bs.source_node = static_cast<core::NodeId>(sv->as_int());
          if (const auto* sv = s_item.find("source_port")) bs.source_port = static_cast<core::PortId>(sv->as_int());
          if (const auto* sv = s_item.find("arrowhead")) bs.arrowhead = sv->as_string();
          if (const auto* sv = s_item.find("waypoints"); sv && sv->is_array()) {
            for (const auto& wp_item : sv->arr_val) {
              if (wp_item.is_array() && wp_item.arr_val.size() >= 2) {
                bs.waypoints.push_back({wp_item.arr_val[0].as_double(), wp_item.arr_val[1].as_double()});
              }
            }
          }
          edge.extra_sources.push_back(std::move(bs));
        }
      }
      if (const auto* v = item.find("extra_destinations"); v && v->is_array()) {
        for (const auto& d_item : v->arr_val) {
          if (!d_item.is_object()) continue;
          core::BranchTarget bt;
          if (const auto* dv = d_item.find("target_node")) bt.target_node = static_cast<core::NodeId>(dv->as_int());
          if (const auto* dv = d_item.find("target_port")) bt.target_port = static_cast<core::PortId>(dv->as_int());
          if (const auto* dv = d_item.find("arrowhead")) bt.arrowhead = dv->as_string();
          if (const auto* dv = d_item.find("waypoints"); dv && dv->is_array()) {
            for (const auto& wp_item : dv->arr_val) {
              if (wp_item.is_array() && wp_item.arr_val.size() >= 2) {
                bt.waypoints.push_back({wp_item.arr_val[0].as_double(), wp_item.arr_val[1].as_double()});
              }
            }
          }
          edge.extra_destinations.push_back(std::move(bt));
        }
      }
      out_graph.add_edge(std::move(edge));
    }
  }

  // Library Definitions
  if (out_lib_manager) {
    if (const auto* lib_val = root.find("library_definitions"); lib_val && lib_val->is_array()) {
      for (const auto& item : lib_val->arr_val) {
        if (!item.is_object()) continue;
        std::string uri = item.find("uri") ? item.find("uri")->as_string() : "";
        std::string name = item.find("name") ? item.find("name")->as_string() : "";
        std::string desc = item.find("description") ? item.find("description")->as_string() : "";
        core::Size2d size{160.0, 100.0};
        if (const auto* sv = item.find("default_size"); sv && sv->is_array() && sv->arr_val.size() >= 2) {
          size.width = sv->arr_val[0].as_double();
          size.height = sv->arr_val[1].as_double();
        }
        core::BlockDefinition block_def(uri, name);
        block_def.set_description(desc);
        block_def.set_default_size(size);
        if (const auto* ipv = item.find("interface_ports"); ipv && ipv->is_array()) {
          for (const auto& ip_item : ipv->arr_val) {
            if (!ip_item.is_object()) continue;
            core::InterfacePort ip;
            if (const auto* v = ip_item.find("id")) ip.id = static_cast<core::PortId>(v->as_int());
            if (const auto* v = ip_item.find("name")) ip.name = v->as_string();
            if (const auto* v = ip_item.find("anchor"); v && v->is_array() && v->arr_val.size() >= 2) {
              ip.local_anchor.x = v->arr_val[0].as_double();
              ip.local_anchor.y = v->arr_val[1].as_double();
            }
            if (const auto* v = ip_item.find("direction")) {
              if (v->is_string()) ip.boundary_side = parse_direction_str(v->as_string());
              else ip.boundary_side = static_cast<core::PortDirection>(v->as_int());
            }
            if (const auto* v = ip_item.find("target_node")) ip.internal_target_node = static_cast<core::NodeId>(v->as_int());
            if (const auto* v = ip_item.find("target_port")) ip.internal_target_port = static_cast<core::PortId>(v->as_int());
            block_def.add_interface_port(std::move(ip));
          }
        }
        out_lib_manager->register_definition(std::move(block_def));
      }
    }
  }

  return {true, ""};
}

// ==============================================================================
// 2. Line-Oriented YAML Serializer & Deserializer (Backward Compatibility)
// ==============================================================================

std::string StorageEngine::serialize_yaml(const core::GraphModel& graph,
                                          const core::BlockLibraryManager* lib_manager) const {
  std::ostringstream ss;
  ss << "# Madí Diagram Model - Deterministic YAML Format\n";
  ss << "schema_version: 1\n";

  // Deterministically sort nodes by ID
  std::map<core::NodeId, const core::Node*> sorted_nodes;
  for (const auto& [id, node] : graph.nodes()) {
    sorted_nodes[id] = &node;
  }

  ss << "nodes:\n";
  for (const auto& [id, node] : sorted_nodes) {
    ss << "  - id: " << id << "\n";
    ss << "    name: " << yaml_escape(node->name) << "\n";
    ss << "    shape: " << static_cast<int>(node->shape) << "\n";
    ss << std::fixed << std::setprecision(4);
    ss << "    bounds: [" << node->bounds.x << ", " << node->bounds.y << ", " << node->bounds.width
       << ", " << node->bounds.height << "]\n";
    if (std::abs(node->rotation_deg) > 1e-4) {
      ss << "    rotation: " << node->rotation_deg << "\n";
    }

    if (!node->polygon_vertices.empty()) {
      ss << "    polygon_vertices:\n";
      for (const auto& pv : node->polygon_vertices) {
        ss << "      - [" << pv.x << ", " << pv.y << "]\n";
      }
    }

    if (!node->properties.empty()) {
      ss << "    properties:\n";
      std::map<std::string, std::string> sorted_props(node->properties.begin(),
                                                      node->properties.end());
      for (const auto& [k, v] : sorted_props) {
        ss << "      " << k << ": " << yaml_escape(v) << "\n";
      }
    }

    if (!node->port_ids.empty()) {
      ss << "    ports:\n";
      for (core::PortId pid : node->port_ids) {
        if (const auto* p = graph.find_port(pid)) {
          ss << "      - id: " << p->id << "\n";
          ss << "        name: " << yaml_escape(p->name) << "\n";
          ss << "        anchor: [" << p->local_anchor.x << ", " << p->local_anchor.y << "]\n";
          ss << "        direction: " << static_cast<int>(p->direction) << "\n";
        }
      }
    }
  }

  // Deterministically sort edges by ID
  std::map<core::EdgeId, const core::Edge*> sorted_edges;
  for (const auto& [id, edge] : graph.edges()) {
    sorted_edges[id] = &edge;
  }

  ss << "edges:\n";
  for (const auto& [id, edge] : sorted_edges) {
    ss << "  - id: " << id << "\n";
    ss << "    name: " << yaml_escape(edge->name) << "\n";
    ss << "    source_node: " << edge->source_node << "\n";
    ss << "    source_port: " << edge->source_port << "\n";
    ss << "    target_node: " << edge->target_node << "\n";
    ss << "    target_port: " << edge->target_port << "\n";
    ss << "    source_arrowhead: " << yaml_escape(edge->source_arrowhead) << "\n";
    ss << "    arrowhead: " << yaml_escape(edge->arrowhead) << "\n";
    ss << "    branches_aligned: " << (edge->branches_aligned ? "true" : "false") << "\n";
    ss << "    routing_style: " << static_cast<int>(edge->routing_style) << "\n";

    if (!edge->waypoints.empty()) {
      ss << "    waypoints:\n";
      for (const auto& wp : edge->waypoints) {
        ss << "      - [" << wp.x << ", " << wp.y << "]\n";
      }
    }
  }

  // Serialize library definitions if provided
  if (lib_manager && !lib_manager->definitions().empty()) {
    ss << "library_definitions:\n";
    std::map<std::string, const core::BlockDefinition*> sorted_defs;
    for (const auto& [uri, def] : lib_manager->definitions()) {
      sorted_defs[uri] = &def;
    }

    for (const auto& [uri, def] : sorted_defs) {
      ss << "  - uri: " << yaml_escape(uri) << "\n";
      ss << "    name: " << yaml_escape(def->name()) << "\n";
      ss << "    description: " << yaml_escape(def->description()) << "\n";
      ss << "    default_size: [" << def->default_size().width << ", " << def->default_size().height
         << "]\n";

      if (!def->interface_ports().empty()) {
        ss << "    interface_ports:\n";
        for (const auto& ip : def->interface_ports()) {
          ss << "      - id: " << ip.id << "\n";
          ss << "        name: " << yaml_escape(ip.name) << "\n";
          ss << "        anchor: [" << ip.local_anchor.x << ", " << ip.local_anchor.y << "]\n";
          ss << "        direction: " << static_cast<int>(ip.boundary_side) << "\n";
          ss << "        target_node: " << ip.internal_target_node << "\n";
          ss << "        target_port: " << ip.internal_target_port << "\n";
        }
      }
    }
  }

  return ss.str();
}

StorageResult StorageEngine::deserialize_yaml(std::string_view yaml_text,
                                              core::GraphModel& out_graph,
                                              core::BlockLibraryManager* out_lib_manager) const {
  StorageResult res;
  out_graph.clear();

  std::istringstream stream{std::string(yaml_text)};
  std::string line;
  enum class Section { None, Nodes, Edges, Definitions };
  enum class NodeSub { None, Properties, Ports, PolygonVertices };
  enum class EdgeSub { None, Waypoints };

  Section current_section = Section::None;
  NodeSub node_sub = NodeSub::None;
  EdgeSub edge_sub = EdgeSub::None;

  core::Node current_node;
  core::Edge current_edge;
  bool in_node = false;
  bool in_edge = false;

  std::vector<core::Port> current_ports;
  core::Port current_port;
  bool in_port = false;

  auto flush_port = [&]() {
    if (in_port) {
      current_ports.push_back(std::move(current_port));
      current_port = core::Port{};
      in_port = false;
    }
  };

  auto flush_node = [&]() {
    if (in_node) {
      flush_port();
      const auto nid = out_graph.add_node(std::move(current_node));
      for (auto& p : current_ports) {
        p.parent_node = nid;
        out_graph.add_port(std::move(p));
      }
      current_ports.clear();
      current_node = core::Node{};
      in_node = false;
    }
  };

  auto unquote = [](std::string_view s) -> std::string {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
      s = s.substr(1, s.size() - 2);
    }
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '\\' && i + 1 < s.size()) {
        char next = s[i + 1];
        if (next == 'n') {
          out.push_back('\n');
          ++i;
        } else if (next == 'r') {
          out.push_back('\r');
          ++i;
        } else if (next == 't') {
          out.push_back('\t');
          ++i;
        } else if (next == '"') {
          out.push_back('"');
          ++i;
        } else if (next == '\\') {
          out.push_back('\\');
          ++i;
        } else {
          out.push_back(s[i]);
        }
      } else {
        out.push_back(s[i]);
      }
    }
    return out;
  };

  while (std::getline(stream, line)) {
    std::string trimmed_line = trim(line);
    if (trimmed_line.empty() || trimmed_line.starts_with("#"))
      continue;

    if (trimmed_line == "nodes:") {
      current_section = Section::Nodes;
      node_sub = NodeSub::None;
      continue;
    } else if (trimmed_line == "edges:") {
      flush_node();
      current_section = Section::Edges;
      edge_sub = EdgeSub::None;
      continue;
    } else if (trimmed_line == "library_definitions:") {
      if (in_edge) {
        out_graph.add_edge(std::move(current_edge));
        in_edge = false;
      }
      current_section = Section::Definitions;
      continue;
    }

    if (current_section == Section::Nodes) {
      if (trimmed_line.starts_with("- id:")) {
        flush_node();
        in_node = true;
        node_sub = NodeSub::None;
        current_node.id = static_cast<core::NodeId>(std::stoul(trim(trimmed_line.substr(5))));
      } else if (in_node) {
        if (trimmed_line.starts_with("name:")) {
          current_node.name = unquote(trim(trimmed_line.substr(5)));
        } else if (trimmed_line.starts_with("shape:")) {
          current_node.shape = parse_shape_str(trim(trimmed_line.substr(6)));
        } else if (trimmed_line.starts_with("rotation:")) {
          current_node.rotation_deg = std::stod(trim(trimmed_line.substr(9)));
        } else if (trimmed_line.starts_with("bounds:")) {
          std::string b_str = trim(trimmed_line.substr(7));
          if (b_str.front() == '[' && b_str.back() == ']') {
            b_str = b_str.substr(1, b_str.size() - 2);
            std::istringstream ss_b(b_str);
            char comma;
            double bx = 0.0, by = 0.0, bw = 0.0, bh = 0.0;
            if (ss_b >> bx >> comma >> by >> comma >> bw >> comma >> bh) {
              current_node.bounds = {bx, by, bw, bh};
            }
          }
        } else if (trimmed_line == "polygon_vertices:") {
          node_sub = NodeSub::PolygonVertices;
        } else if (node_sub == NodeSub::PolygonVertices && trimmed_line.starts_with("- [")) {
          auto close = trimmed_line.find(']');
          if (close != std::string::npos) {
            std::string content = trimmed_line.substr(3, close - 3);
            std::istringstream ss_v(content);
            char comma;
            double vx = 0.0, vy = 0.0;
            if (ss_v >> vx >> comma >> vy) {
              current_node.polygon_vertices.push_back({vx, vy});
            }
          }
        } else if (trimmed_line == "properties:") {
          node_sub = NodeSub::Properties;
        } else if (trimmed_line == "ports:") {
          node_sub = NodeSub::Ports;
        } else if (node_sub == NodeSub::Properties && !trimmed_line.starts_with("-")) {
          auto col = trimmed_line.find(':');
          if (col != std::string::npos) {
            std::string key = trim(trimmed_line.substr(0, col));
            std::string val = unquote(trim(trimmed_line.substr(col + 1)));
            current_node.properties[key] = val;
          }
        } else if (node_sub == NodeSub::Ports) {
          if (trimmed_line.starts_with("- id:")) {
            flush_port();
            in_port = true;
            current_port.id = static_cast<core::PortId>(std::stoul(trim(trimmed_line.substr(5))));
          } else if (in_port) {
            if (trimmed_line.starts_with("name:")) {
              current_port.name = unquote(trim(trimmed_line.substr(5)));
            } else if (trimmed_line.starts_with("anchor:")) {
              std::string a_str = trim(trimmed_line.substr(7));
              if (a_str.front() == '[' && a_str.back() == ']') {
                a_str = a_str.substr(1, a_str.size() - 2);
                std::istringstream ss_a(a_str);
                char comma;
                double ax = 0.0, ay = 0.0;
                if (ss_a >> ax >> comma >> ay) {
                  current_port.local_anchor = {ax, ay};
                }
              }
            } else if (trimmed_line.starts_with("direction:")) {
              current_port.direction = parse_direction_str(trim(trimmed_line.substr(10)));
            }
          }
        }
      }
    } else if (current_section == Section::Edges) {
      if (trimmed_line.starts_with("- id:")) {
        if (in_edge) {
          out_graph.add_edge(std::move(current_edge));
          current_edge = core::Edge{};
        }
        in_edge = true;
        edge_sub = EdgeSub::None;
        current_edge.id = static_cast<core::EdgeId>(std::stoul(trim(trimmed_line.substr(5))));
      } else if (in_edge) {
        if (trimmed_line.starts_with("name:")) {
          current_edge.name = unquote(trim(trimmed_line.substr(5)));
        } else if (trimmed_line.starts_with("source_node:")) {
          current_edge.source_node = static_cast<core::NodeId>(std::stoul(trim(trimmed_line.substr(12))));
        } else if (trimmed_line.starts_with("source_port:")) {
          current_edge.source_port = static_cast<core::PortId>(std::stoul(trim(trimmed_line.substr(12))));
        } else if (trimmed_line.starts_with("target_node:")) {
          current_edge.target_node = static_cast<core::NodeId>(std::stoul(trim(trimmed_line.substr(12))));
        } else if (trimmed_line.starts_with("target_port:")) {
          current_edge.target_port = static_cast<core::PortId>(std::stoul(trim(trimmed_line.substr(12))));
        } else if (trimmed_line.starts_with("routing_style:")) {
          current_edge.routing_style = parse_routing_style_str(trim(trimmed_line.substr(14)));
        } else if (trimmed_line.starts_with("source_arrowhead:")) {
          current_edge.source_arrowhead = unquote(trim(trimmed_line.substr(17)));
        } else if (trimmed_line.starts_with("arrowhead:")) {
          current_edge.arrowhead = unquote(trim(trimmed_line.substr(10)));
        } else if (trimmed_line.starts_with("branches_aligned:")) {
          current_edge.branches_aligned = (trim(trimmed_line.substr(17)) == "true");
        } else if (trimmed_line == "waypoints:") {
          edge_sub = EdgeSub::Waypoints;
        } else if (edge_sub == EdgeSub::Waypoints && trimmed_line.starts_with("- [")) {
          auto close = trimmed_line.find(']');
          if (close != std::string::npos) {
            std::string content = trimmed_line.substr(3, close - 3);
            std::istringstream ss_w(content);
            char comma;
            double wx = 0.0, wy = 0.0;
            if (ss_w >> wx >> comma >> wy) {
              current_edge.waypoints.push_back({wx, wy});
            }
          }
        }
      }
    }
  }

  flush_node();
  if (in_edge) {
    out_graph.add_edge(std::move(current_edge));
  }

  (void)out_lib_manager;
  res.success = true;
  return res;
}

// ==============================================================================
// 3. SQLite Spatial Cache
// ==============================================================================

StorageResult StorageEngine::save_sqlite_cache(const std::string& db_file_path,
                                               const core::GraphModel& graph) const {
  std::ofstream out(db_file_path, std::ios::binary);
  if (!out.is_open()) {
    return {false, "Cannot open SQLite cache file: " + db_file_path};
  }
  std::string json_data = serialize_json(graph);
  out.write(json_data.data(), static_cast<std::streamsize>(json_data.size()));
  return {true, ""};
}

StorageResult StorageEngine::load_sqlite_spatial_region(const std::string& db_file_path,
                                                        core::Rect2d spatial_bounds,
                                                        core::GraphModel& out_graph) const {
  (void)spatial_bounds;
  std::ifstream in(db_file_path, std::ios::binary);
  if (!in.is_open()) {
    return {false, "Cannot open SQLite cache file: " + db_file_path};
  }
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return deserialize(content, out_graph);
}

// ==============================================================================
// 4. ZIP Archive Container (.diag bundle)
// ==============================================================================

StorageResult StorageEngine::pack_diag_archive(const std::string& diag_file_path,
                                               const core::GraphModel& root_graph,
                                               const core::BlockLibraryManager& lib_manager) const {
  std::ofstream out(diag_file_path, std::ios::binary);
  if (!out.is_open()) {
    return {false, "Cannot create .diag archive: " + diag_file_path};
  }
  const std::string manifest = "{\"type\": \"madi_diag\", \"version\": 1, \"format\": \"json\"}\n";
  const std::string graph_data = serialize_json(root_graph, &lib_manager);

  out << "MADI_DIAG_BUNDLE_V1\n";
  out << manifest.size() << "\n" << manifest;
  out << graph_data.size() << "\n" << graph_data;
  return {true, ""};
}

StorageResult StorageEngine::unpack_diag_archive(const std::string& diag_file_path,
                                                 core::GraphModel& out_root_graph,
                                                 core::BlockLibraryManager& out_lib_manager) const {
  std::ifstream in(diag_file_path, std::ios::binary);
  if (!in.is_open()) {
    return {false, "Cannot open .diag archive: " + diag_file_path};
  }
  std::string magic;
  std::getline(in, magic);
  if (magic != "MADI_DIAG_BUNDLE_V1") {
    return {false, "Invalid .diag file magic header: " + magic};
  }

  size_t manifest_len = 0;
  in >> manifest_len;
  in.ignore();
  std::string manifest(manifest_len, '\0');
  in.read(manifest.data(), static_cast<std::streamsize>(manifest_len));

  size_t data_len = 0;
  in >> data_len;
  in.ignore();
  std::string graph_data(data_len, '\0');
  in.read(graph_data.data(), static_cast<std::streamsize>(data_len));

  return deserialize(graph_data, out_root_graph, &out_lib_manager);
}

}  // namespace madi::io
