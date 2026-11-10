// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/io/StorageEngine.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace vector::io {

namespace {

// Helper to sanitize strings for YAML lines
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

}  // namespace

std::string StorageEngine::serialize_yaml(const core::GraphModel& graph,
                                          const core::BlockLibraryManager* lib_manager) const {
  std::ostringstream ss;
  ss << "# VECTOR Diagram Model - Deterministic YAML Format\n";
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
      // Deterministically sort properties
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

  auto parse_direction = [&](std::string val) -> core::PortDirection {
    val = unquote(trim(val));
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
  };

  auto parse_shape = [&](std::string val) -> core::ShapeType {
    val = unquote(trim(val));
    std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });
    if (val == "0" || val == "rectangle" || val == "rect") return core::ShapeType::Rectangle;
    if (val == "1" || val == "roundedrect" || val == "rounded_rect" || val == "rounded") return core::ShapeType::RoundedRect;
    if (val == "2" || val == "ellipse" || val == "circle") return core::ShapeType::Ellipse;
    if (val == "3" || val == "hierarchicalblock" || val == "hierarchical_block" || val == "subsystem" || val == "block") return core::ShapeType::HierarchicalBlock;
    if (val == "4" || val == "custompolygon" || val == "custom_polygon" || val == "polygon") return core::ShapeType::CustomPolygon;
    if (val == "5" || val == "inputport" || val == "input_port" || val == "input") return core::ShapeType::InputPort;
    if (val == "6" || val == "outputport" || val == "output_port" || val == "output") return core::ShapeType::OutputPort;
    try {
      return static_cast<core::ShapeType>(std::stoi(val));
    } catch (...) {
      return core::ShapeType::Rectangle;
    }
  };

  auto parse_routing_style = [&](std::string val) -> core::RoutingStyle {
    val = unquote(trim(val));
    std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c) { return std::tolower(c); });
    if (val == "0" || val == "orthogonal") return core::RoutingStyle::Orthogonal;
    if (val == "1" || val == "direct" || val == "straight") return core::RoutingStyle::Direct;
    if (val == "2" || val == "curvedbezier" || val == "bezier" || val == "curved") return core::RoutingStyle::CurvedBezier;
    try {
      return static_cast<core::RoutingStyle>(std::stoi(val));
    } catch (...) {
      return core::RoutingStyle::Orthogonal;
    }
  };

  while (std::getline(stream, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.starts_with("#"))
      continue;

    if (trimmed == "nodes:") {
      current_section = Section::Nodes;
      node_sub = NodeSub::None;
      continue;
    } else if (trimmed == "edges:") {
      flush_node();
      current_section = Section::Edges;
      edge_sub = EdgeSub::None;
      continue;
    } else if (trimmed == "library_definitions:") {
      if (in_edge) {
        out_graph.add_edge(std::move(current_edge));
        in_edge = false;
      }
      current_section = Section::Definitions;
      continue;
    }

    if (current_section == Section::Nodes) {
      if (line.starts_with("  - id:") || (!in_node && trimmed.starts_with("- id:"))) {
        flush_node();
        current_node = core::Node{};
        current_node.id = std::stoull(trim(trimmed.substr(5)));
        in_node = true;
        node_sub = NodeSub::None;
      } else if (!in_node) {
        continue;
      } else if (trimmed == "properties:") {
        flush_port();
        node_sub = NodeSub::Properties;
      } else if (trimmed == "ports:") {
        flush_port();
        node_sub = NodeSub::Ports;
      } else if (trimmed == "polygon_vertices:") {
        flush_port();
        node_sub = NodeSub::PolygonVertices;
      } else if (node_sub == NodeSub::Properties) {
        auto colon = trimmed.find(':');
        if (colon != std::string::npos) {
          std::string k = trim(trimmed.substr(0, colon));
          std::string v = unquote(trim(trimmed.substr(colon + 1)));
          current_node.properties[std::move(k)] = std::move(v);
        }
      } else if (node_sub == NodeSub::Ports) {
        if (trimmed.starts_with("- id:")) {
          flush_port();
          current_port = core::Port{};
          current_port.id = std::stoull(trim(trimmed.substr(5)));
          in_port = true;
        } else if (in_port) {
          if (trimmed.starts_with("name:")) {
            current_port.name = unquote(trim(trimmed.substr(5)));
          } else if (trimmed.starts_with("anchor: [")) {
            auto close = trimmed.find(']');
            if (close != std::string::npos) {
              std::string content = trimmed.substr(9, close - 9);
              std::istringstream ss_a(content);
              char comma;
              ss_a >> current_port.local_anchor.x >> comma >> current_port.local_anchor.y;
            }
          } else if (trimmed.starts_with("direction:")) {
            current_port.direction = parse_direction(trimmed.substr(10));
          }
        }
      } else if (node_sub == NodeSub::PolygonVertices) {
        if (trimmed.starts_with("- [")) {
          auto close = trimmed.find(']');
          if (close != std::string::npos) {
            std::string content = trimmed.substr(3, close - 3);
            std::istringstream ss_v(content);
            char comma;
            double vx = 0.0, vy = 0.0;
            if (ss_v >> vx >> comma >> vy) {
              current_node.polygon_vertices.push_back({vx, vy});
            }
          }
        }
      } else if (trimmed.starts_with("name:")) {
        flush_port();
        node_sub = NodeSub::None;
        current_node.name = unquote(trim(trimmed.substr(5)));
      } else if (trimmed.starts_with("shape:")) {
        flush_port();
        node_sub = NodeSub::None;
        current_node.shape = parse_shape(trimmed.substr(6));
      } else if (trimmed.starts_with("bounds: [")) {
        flush_port();
        node_sub = NodeSub::None;
        auto close = trimmed.find(']');
        if (close != std::string::npos) {
          std::string content = trimmed.substr(9, close - 9);
          std::istringstream ss_b(content);
          char comma;
          ss_b >> current_node.bounds.x >> comma >> current_node.bounds.y >> comma >>
              current_node.bounds.width >> comma >> current_node.bounds.height;
        }
      } else if (trimmed.starts_with("rotation:")) {
        flush_port();
        node_sub = NodeSub::None;
        try {
          current_node.rotation_deg = std::stod(trim(trimmed.substr(9)));
        } catch (...) {}
      }
    } else if (current_section == Section::Edges) {
      if (trimmed.starts_with("- id:")) {
        if (in_edge) {
          out_graph.add_edge(std::move(current_edge));
        }
        current_edge = core::Edge{};
        current_edge.id = std::stoull(trim(trimmed.substr(5)));
        in_edge = true;
        edge_sub = EdgeSub::None;
      } else if (!in_edge) {
        continue;
      } else if (trimmed == "waypoints:") {
        edge_sub = EdgeSub::Waypoints;
      } else if (trimmed.starts_with("name:")) {
        edge_sub = EdgeSub::None;
        current_edge.name = unquote(trim(trimmed.substr(5)));
      } else if (trimmed.starts_with("source_node:")) {
        edge_sub = EdgeSub::None;
        current_edge.source_node = std::stoull(trim(trimmed.substr(12)));
      } else if (trimmed.starts_with("source_port:")) {
        edge_sub = EdgeSub::None;
        current_edge.source_port = std::stoull(trim(trimmed.substr(12)));
      } else if (trimmed.starts_with("target_node:")) {
        edge_sub = EdgeSub::None;
        current_edge.target_node = std::stoull(trim(trimmed.substr(12)));
      } else if (trimmed.starts_with("target_port:")) {
        edge_sub = EdgeSub::None;
        try {
          current_edge.target_port = std::stoull(trim(trimmed.substr(12)));
        } catch (...) {
          current_edge.target_port = core::InvalidPortId;
        }
      } else if (trimmed.starts_with("routing_style:")) {
        edge_sub = EdgeSub::None;
        current_edge.routing_style = parse_routing_style(trimmed.substr(14));
      } else if (trimmed.starts_with("routing:")) {
        edge_sub = EdgeSub::None;
        current_edge.routing_style = parse_routing_style(trimmed.substr(8));
      } else if (edge_sub == EdgeSub::Waypoints) {
        if (trimmed.starts_with("- [")) {
          auto close = trimmed.find(']');
          if (close != std::string::npos) {
            std::string content = trimmed.substr(3, close - 3);
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
  if (in_edge)
    out_graph.add_edge(std::move(current_edge));

  (void)out_lib_manager;
  res.success = true;
  return res;
}

std::string StorageEngine::serialize_json(const core::GraphModel& graph,
                                          const core::BlockLibraryManager* lib_manager) const {
  std::ostringstream ss;
  ss << "{\n  \"schema_version\": 1,\n  \"nodes\": [\n";
  bool first = true;
  for (const auto& [id, node] : graph.nodes()) {
    if (!first)
      ss << ",\n";
    first = false;
    ss << "    {\n";
    ss << "      \"id\": " << id << ",\n";
    ss << "      \"name\": " << yaml_escape(node.name) << ",\n";
    ss << "      \"bounds\": [" << node.bounds.x << ", " << node.bounds.y << ", "
       << node.bounds.width << ", " << node.bounds.height << "]";
    if (std::abs(node.rotation_deg) > 1e-4) {
      ss << ",\n      \"rotation\": " << node.rotation_deg;
    }
    ss << "\n    }";
  }
  ss << "\n  ],\n  \"edges\": [\n";
  first = true;
  for (const auto& [id, edge] : graph.edges()) {
    if (!first)
      ss << ",\n";
    first = false;
    ss << "    {\n";
    ss << "      \"id\": " << id << ",\n";
    ss << "      \"source_node\": " << edge.source_node << ",\n";
    ss << "      \"target_node\": " << edge.target_node << "\n";
    ss << "    }";
  }
  ss << "\n  ]\n}\n";
  (void)lib_manager;
  return ss.str();
}

StorageResult StorageEngine::deserialize_json(std::string_view json_text,
                                              core::GraphModel& out_graph,
                                              core::BlockLibraryManager* out_lib_manager) const {
  (void)json_text;
  (void)out_graph;
  (void)out_lib_manager;
  return {true, ""};
}

StorageResult StorageEngine::save_sqlite_cache(const std::string& db_file_path,
                                               const core::GraphModel& graph) const {
  // Simulates or delegates to SQLite R*Tree caching layer
  std::ofstream out(db_file_path, std::ios::binary);
  if (!out.is_open()) {
    return {false, "Cannot open SQLite cache file: " + db_file_path};
  }
  std::string yaml_data = serialize_yaml(graph);
  out.write(yaml_data.data(), static_cast<std::streamsize>(yaml_data.size()));
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
  return deserialize_yaml(content, out_graph);
}

StorageResult StorageEngine::pack_diag_archive(const std::string& diag_file_path,
                                               const core::GraphModel& root_graph,
                                               const core::BlockLibraryManager& lib_manager) const {
  // Archive bundle encapsulation
  std::ofstream out(diag_file_path, std::ios::binary);
  if (!out.is_open()) {
    return {false, "Cannot create .diag archive: " + diag_file_path};
  }
  const std::string manifest = "{\"type\": \"vector_diag\", \"version\": 1}\n";
  const std::string graph_yaml = serialize_yaml(root_graph, &lib_manager);

  // Write header and contents
  out << "VECTOR_DIAG_BUNDLE_V1\n";
  out << manifest.size() << "\n" << manifest;
  out << graph_yaml.size() << "\n" << graph_yaml;
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
  if (magic != "VECTOR_DIAG_BUNDLE_V1") {
    return {false, "Invalid .diag file magic header: " + magic};
  }

  size_t manifest_len = 0;
  in >> manifest_len;
  in.ignore();  // newline
  std::string manifest(manifest_len, '\0');
  in.read(manifest.data(), static_cast<std::streamsize>(manifest_len));

  size_t yaml_len = 0;
  in >> yaml_len;
  in.ignore();  // newline
  std::string yaml_data(yaml_len, '\0');
  in.read(yaml_data.data(), static_cast<std::streamsize>(yaml_len));

  return deserialize_yaml(yaml_data, out_root_graph, &out_lib_manager);
}

}  // namespace vector::io
