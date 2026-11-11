// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "madi/io/DrawioParser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <zlib.h>

namespace madi::io {

namespace {

std::string xml_escape(std::string_view text) {
  std::string res;
  res.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&': res += "&amp;"; break;
      case '<': res += "&lt;"; break;
      case '>': res += "&gt;"; break;
      case '"': res += "&quot;"; break;
      case '\'': res += "&apos;"; break;
      default: res += c; break;
    }
  }
  return res;
}

std::string xml_unescape(std::string_view text) {
  std::string res;
  res.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '&') {
      const auto rem = text.substr(i);
      if (rem.starts_with("&amp;")) { res += '&'; i += 4; }
      else if (rem.starts_with("&lt;")) { res += '<'; i += 3; }
      else if (rem.starts_with("&gt;")) { res += '>'; i += 3; }
      else if (rem.starts_with("&quot;")) { res += '"'; i += 5; }
      else if (rem.starts_with("&apos;")) { res += '\''; i += 5; }
      else if (rem.starts_with("&#") && rem.find(';') != std::string_view::npos) {
        size_t semi = rem.find(';');
        std::string num_str(rem.substr(2, semi - 2));
        int code = 0;
        if (!num_str.empty() && (num_str[0] == 'x' || num_str[0] == 'X')) {
          code = static_cast<int>(std::strtol(num_str.c_str() + 1, nullptr, 16));
        } else {
          code = std::atoi(num_str.c_str());
        }
        if (code > 0 && code < 128) res += static_cast<char>(code);
        i += semi;
      } else {
        res += text[i];
      }
    } else {
      res += text[i];
    }
  }
  return res;
}

std::string get_attribute_value(std::string_view tag, std::string_view attr) {
  const std::string needle = std::string(attr) + "=\"";
  auto pos = tag.find(needle);
  if (pos == std::string_view::npos) {
    const std::string needle_single = std::string(attr) + "='";
    pos = tag.find(needle_single);
    if (pos == std::string_view::npos) return {};
    const size_t val_start = pos + needle_single.size();
    const size_t val_end = tag.find('\'', val_start);
    if (val_end == std::string_view::npos) return {};
    return std::string(tag.substr(val_start, val_end - val_start));
  }
  const size_t val_start = pos + needle.size();
  const size_t val_end = tag.find('"', val_start);
  if (val_end == std::string_view::npos) return {};
  return std::string(tag.substr(val_start, val_end - val_start));
}

std::string extract_style_param(std::string_view style, std::string_view key) {
  const std::string prefix = std::string(key) + "=";
  size_t pos = style.find(prefix);
  if (pos == std::string_view::npos) return {};
  pos += prefix.size();
  size_t end_pos = style.find(';', pos);
  if (end_pos == std::string_view::npos) end_pos = style.size();
  return std::string(style.substr(pos, end_pos - pos));
}

const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_decode(std::string_view in) {
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++) T[static_cast<size_t>(base64_chars[static_cast<size_t>(i)])] = i;

  std::string out;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (T[c] == -1) continue;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

std::string base64_encode(const unsigned char* in, size_t len) {
  std::string out;
  int val = 0, valb = -6;
  for (size_t i = 0; i < len; ++i) {
    val = (val << 8) + in[i];
    valb += 8;
    while (valb >= 0) {
      out.push_back(base64_chars[static_cast<size_t>((val >> valb) & 0x3F)]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(base64_chars[static_cast<size_t>(((val << 8) >> (valb + 8)) & 0x3F)]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

} // namespace

std::string DrawioParser::export_xml(const core::GraphModel& graph,
                                     const std::string& diagram_name) {
  std::ostringstream ss;
  ss << "<mxfile host=\"app.diagrams.net\" agent=\"Madí Diagram Engine\" version=\"21.0.0\" type=\"device\">\n";
  ss << "  <diagram id=\"Page-1\" name=\"" << xml_escape(diagram_name.empty() ? "Page-1" : diagram_name) << "\">\n";
  ss << "    <mxGraphModel dx=\"1200\" dy=\"800\" grid=\"1\" gridSize=\"10\" guides=\"1\" tooltips=\"1\" connect=\"1\" arrows=\"1\" fold=\"1\" page=\"1\" pageScale=\"1\" pageWidth=\"1100\" pageHeight=\"850\" math=\"0\" shadow=\"0\">\n";
  ss << "      <root>\n";
  ss << "        <mxCell id=\"0\" />\n";
  ss << "        <mxCell id=\"1\" parent=\"0\" />\n";

  // Nodes
  for (const auto& [nid, node] : graph.nodes()) {
    std::string shape_style = "whiteSpace=wrap;html=1;";
    switch (node.shape) {
      case core::ShapeType::Rectangle:
        shape_style += "rounded=0;";
        break;
      case core::ShapeType::RoundedRect:
        shape_style += "rounded=1;arcSize=15;";
        break;
      case core::ShapeType::Ellipse:
        shape_style += "ellipse;";
        break;
      case core::ShapeType::CustomPolygon:
        shape_style += "shape=polygon;perimeter=polygonPerimeter;";
        break;
      case core::ShapeType::HierarchicalBlock:
        shape_style += "shape=process;rounded=0;";
        break;
      case core::ShapeType::InputPort:
        shape_style += "shape=mxgraph.flowchart.terminator;fillColor=#22C55E;strokeColor=#15803D;fontColor=#FFFFFF;";
        break;
      case core::ShapeType::OutputPort:
        shape_style += "shape=mxgraph.flowchart.terminator;fillColor=#EF4444;strokeColor=#B91C1C;fontColor=#FFFFFF;";
        break;
      case core::ShapeType::BidirectionalPort:
        shape_style += "shape=mxgraph.flowchart.terminator;fillColor=#A855F7;strokeColor=#7E22CE;fontColor=#FFFFFF;";
        break;
    }

    auto it_fc = node.properties.find("fill_color");
    if (it_fc != node.properties.end()) {
      shape_style += "fillColor=" + it_fc->second + ";";
    }
    auto it_sc = node.properties.find("stroke_color");
    if (it_sc != node.properties.end()) {
      shape_style += "strokeColor=" + it_sc->second + ";";
    }
    auto it_tc = node.properties.find("text_color");
    if (it_tc != node.properties.end()) {
      shape_style += "fontColor=" + it_tc->second + ";";
    }
    auto it_sw = node.properties.find("stroke_width");
    if (it_sw != node.properties.end()) {
      shape_style += "strokeWidth=" + it_sw->second + ";";
    }
    auto it_ff = node.properties.find("font_family");
    if (it_ff != node.properties.end()) {
      shape_style += "fontFamily=" + it_ff->second + ";";
    }
    auto it_fs = node.properties.find("font_size");
    if (it_fs != node.properties.end()) {
      shape_style += "fontSize=" + it_fs->second + ";";
    }
    auto it_fb = node.properties.find("font_bold");
    if (it_fb != node.properties.end() && (it_fb->second == "true" || it_fb->second == "1")) {
      shape_style += "fontStyle=1;";
    }
    auto it_fi = node.properties.find("font_italic");
    if (it_fi != node.properties.end() && (it_fi->second == "true" || it_fi->second == "1")) {
      shape_style += "fontStyle=2;";
    }

    ss << "        <mxCell id=\"node_" << node.id << "\" value=\"" << xml_escape(node.name) << "\" "
       << "style=\"" << shape_style << "\" vertex=\"1\" parent=\"1\">\n";
    ss << "          <mxGeometry x=\"" << node.bounds.x << "\" y=\"" << node.bounds.y
       << "\" width=\"" << node.bounds.width << "\" height=\"" << node.bounds.height << "\" as=\"geometry\" />\n";
    ss << "        </mxCell>\n";
  }

  // Edges
  for (const auto& [eid, edge] : graph.edges()) {
    std::string edge_style = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;";
    ss << "        <mxCell id=\"edge_" << edge.id << "\" value=\"" << xml_escape(edge.name) << "\" "
       << "style=\"" << edge_style << "\" edge=\"1\" parent=\"1\" ";
    if (edge.source_node != core::InvalidNodeId) {
      ss << "source=\"node_" << edge.source_node << "\" ";
    }
    if (edge.target_node != core::InvalidNodeId) {
      ss << "target=\"node_" << edge.target_node << "\" ";
    }
    ss << ">\n";
    ss << "          <mxGeometry relative=\"1\" as=\"geometry\">\n";
    if (!edge.waypoints.empty()) {
      ss << "            <Array as=\"points\">\n";
      for (const auto& wp : edge.waypoints) {
        ss << "              <mxPoint x=\"" << wp.x << "\" y=\"" << wp.y << "\" />\n";
      }
      ss << "            </Array>\n";
    }
    ss << "          </mxGeometry>\n";
    ss << "        </mxCell>\n";
  }

  ss << "      </root>\n";
  ss << "    </mxGraphModel>\n";
  ss << "  </diagram>\n";
  ss << "</mxfile>\n";

  return ss.str();
}

std::string DrawioParser::decompress_diagram_payload(std::string_view base64_payload) {
  const std::string compressed = base64_decode(base64_payload);
  if (compressed.empty()) return {};

  z_stream strm{};
  if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
    return {};
  }

  strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
  strm.avail_in = static_cast<uInt>(compressed.size());

  std::string out;
  char buf[4096];
  int ret = Z_OK;
  while (ret == Z_OK) {
    strm.next_out = reinterpret_cast<Bytef*>(buf);
    strm.avail_out = sizeof(buf);
    ret = inflate(&strm, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
      inflateEnd(&strm);
      return {};
    }
    out.append(buf, sizeof(buf) - strm.avail_out);
  }
  inflateEnd(&strm);
  return out;
}

std::string DrawioParser::compress_diagram_payload(std::string_view raw_xml) {
  if (raw_xml.empty()) return {};

  z_stream strm{};
  if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    return {};
  }

  strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw_xml.data()));
  strm.avail_in = static_cast<uInt>(raw_xml.size());

  std::vector<unsigned char> compressed;
  unsigned char buf[4096];
  int ret = Z_OK;
  while (ret == Z_OK) {
    strm.next_out = buf;
    strm.avail_out = sizeof(buf);
    ret = deflate(&strm, Z_FINISH);
    compressed.insert(compressed.end(), buf, buf + (sizeof(buf) - strm.avail_out));
  }
  deflateEnd(&strm);

  return base64_encode(compressed.data(), compressed.size());
}

bool DrawioParser::parse_mx_graph_model(std::string_view xml_data,
                                        core::GraphModel& out_graph,
                                        DrawioParseResult& result) {
  struct PendingEdge {
    std::string id;
    std::string name;
    std::string src_id;
    std::string tgt_id;
    std::vector<core::Vec2d> waypoints;
  };

  std::unordered_map<std::string, core::NodeId> id_to_node;
  std::vector<PendingEdge> pending_edges;

  size_t cursor = 0;
  while ((cursor = xml_data.find("<mxCell", cursor)) != std::string_view::npos) {
    const size_t tag_open_end = xml_data.find('>', cursor);
    if (tag_open_end == std::string_view::npos) break;

    const std::string_view open_tag = xml_data.substr(cursor, tag_open_end - cursor + 1);

    const std::string cid = get_attribute_value(open_tag, "id");
    const std::string val = xml_unescape(get_attribute_value(open_tag, "value"));
    const std::string style = get_attribute_value(open_tag, "style");
    const std::string vertex = get_attribute_value(open_tag, "vertex");
    const std::string edge = get_attribute_value(open_tag, "edge");
    const std::string src = get_attribute_value(open_tag, "source");
    const std::string tgt = get_attribute_value(open_tag, "target");

    // Check if element has geometry body or is self closing
    size_t cell_end = tag_open_end;
    if (!open_tag.ends_with("/>")) {
      size_t closing = xml_data.find("</mxCell>", tag_open_end);
      if (closing != std::string_view::npos) {
        cell_end = closing + 9;
      }
    }

    const std::string_view cell_body = xml_data.substr(cursor, cell_end - cursor);

    if (vertex == "1" && cid != "0" && cid != "1") {
      core::Node node;
      node.name = val.empty() ? ("Block " + std::to_string(out_graph.node_count() + 1)) : val;

      // Extract geometry
      size_t geom_pos = cell_body.find("<mxGeometry");
      if (geom_pos != std::string_view::npos) {
        size_t geom_end = cell_body.find('>', geom_pos);
        if (geom_end != std::string_view::npos) {
          const std::string_view geom_tag = cell_body.substr(geom_pos, geom_end - geom_pos + 1);
          bool ok_x = false, ok_y = false, ok_w = false, ok_h = false;
          std::string sx = get_attribute_value(geom_tag, "x");
          std::string sy = get_attribute_value(geom_tag, "y");
          std::string sw = get_attribute_value(geom_tag, "width");
          std::string sh = get_attribute_value(geom_tag, "height");

          double gx = 0.0, gy = 0.0, gw = 120.0, gh = 70.0;
          try {
            if (!sx.empty()) gx = std::stod(sx);
            if (!sy.empty()) gy = std::stod(sy);
            if (!sw.empty()) gw = std::stod(sw);
            if (!sh.empty()) gh = std::stod(sh);
          } catch (...) {}
          (void)ok_x; (void)ok_y; (void)ok_w; (void)ok_h;
          node.bounds = {gx, gy, std::max(20.0, gw), std::max(20.0, gh)};
        }
      }

      // Infer shape type from style
      if (style.find("ellipse") != std::string_view::npos) {
        node.shape = core::ShapeType::Ellipse;
      } else if (style.find("rounded=1") != std::string_view::npos) {
        node.shape = core::ShapeType::RoundedRect;
      } else if (style.find("shape=polygon") != std::string_view::npos) {
        node.shape = core::ShapeType::CustomPolygon;
      } else if (style.find("shape=process") != std::string_view::npos) {
        node.shape = core::ShapeType::HierarchicalBlock;
      } else if (style.find("terminator") != std::string_view::npos) {
        if (style.find("A855F7") != std::string_view::npos || style.find("purple") != std::string_view::npos) {
          node.shape = core::ShapeType::BidirectionalPort;
          node.properties["interface_kind"] = "inout";
        } else if (style.find("22C55E") != std::string_view::npos) {
          node.shape = core::ShapeType::InputPort;
        } else {
          node.shape = core::ShapeType::OutputPort;
        }
      } else {
        node.shape = core::ShapeType::Rectangle;
      }

      // Parse style properties
      const std::string fc = extract_style_param(style, "fillColor");
      if (!fc.empty()) node.properties["fill_color"] = fc;

      const std::string sc = extract_style_param(style, "strokeColor");
      if (!sc.empty()) node.properties["stroke_color"] = sc;

      const std::string tc = extract_style_param(style, "fontColor");
      if (!tc.empty()) node.properties["text_color"] = tc;

      const std::string sw = extract_style_param(style, "strokeWidth");
      if (!sw.empty()) node.properties["stroke_width"] = sw;

      const std::string ff = extract_style_param(style, "fontFamily");
      if (!ff.empty()) node.properties["font_family"] = ff;

      const std::string fs = extract_style_param(style, "fontSize");
      if (!fs.empty()) node.properties["font_size"] = fs;

      const std::string fstyle = extract_style_param(style, "fontStyle");
      if (!fstyle.empty()) {
        int fsi = std::atoi(fstyle.c_str());
        if (fsi & 1) node.properties["font_bold"] = "true";
        if (fsi & 2) node.properties["font_italic"] = "true";
        if (fsi & 4) node.properties["font_underline"] = "true";
      }

      node.z_order = static_cast<int32_t>(out_graph.node_count() + 1);
      const core::NodeId nid = out_graph.add_node(std::move(node));

      // Add 4 cardinal connection ports
      const std::array<std::tuple<std::string, core::Vec2d, core::PortDirection>, 4> port_defs = {
        std::tuple{"N", core::Vec2d{0.5, 0.0}, core::PortDirection::North},
        std::tuple{"E", core::Vec2d{1.0, 0.5}, core::PortDirection::East},
        std::tuple{"S", core::Vec2d{0.5, 1.0}, core::PortDirection::South},
        std::tuple{"W", core::Vec2d{0.0, 0.5}, core::PortDirection::West}
      };
      for (const auto& [pname, anchor, dir] : port_defs) {
        core::Port p;
        p.parent_node = nid;
        p.name = pname;
        p.local_anchor = anchor;
        p.direction = dir;
        out_graph.add_port(std::move(p));
      }

      id_to_node[cid] = nid;
      result.imported_nodes++;
    } else if (edge == "1") {
      PendingEdge pe;
      pe.id = cid;
      pe.name = val;
      pe.src_id = src;
      pe.tgt_id = tgt;

      // Extract waypoints from <Array as="points">
      size_t arr_pos = cell_body.find("<Array");
      if (arr_pos != std::string_view::npos) {
        size_t arr_end = cell_body.find("</Array>", arr_pos);
        if (arr_end != std::string_view::npos) {
          const std::string_view arr_body = cell_body.substr(arr_pos, arr_end - arr_pos);
          size_t pt_cur = 0;
          while ((pt_cur = arr_body.find("<mxPoint", pt_cur)) != std::string_view::npos) {
            size_t pt_end = arr_body.find('>', pt_cur);
            if (pt_end == std::string_view::npos) break;
            const std::string_view pt_tag = arr_body.substr(pt_cur, pt_end - pt_cur + 1);
            std::string px = get_attribute_value(pt_tag, "x");
            std::string py = get_attribute_value(pt_tag, "y");
            if (!px.empty() && !py.empty()) {
              try {
                pe.waypoints.push_back({std::stod(px), std::stod(py)});
              } catch (...) {}
            }
            pt_cur = pt_end + 1;
          }
        }
      }
      pending_edges.push_back(std::move(pe));
    }

    cursor = (cell_end > cursor) ? cell_end : (tag_open_end + 1);
  }

  // Resolve and add pending edges
  for (const auto& pe : pending_edges) {
    core::Edge e;
    e.name = pe.name;
    auto it_s = id_to_node.find(pe.src_id);
    auto it_t = id_to_node.find(pe.tgt_id);
    if (it_s != id_to_node.end()) e.source_node = it_s->second;
    if (it_t != id_to_node.end()) e.target_node = it_t->second;
    e.waypoints = pe.waypoints;

    out_graph.add_edge(std::move(e));
    result.imported_edges++;
  }

  result.success = true;
  return true;
}

DrawioParseResult DrawioParser::import_xml(std::string_view xml_data,
                                           core::GraphModel& out_graph) {
  DrawioParseResult result;
  if (xml_data.empty()) {
    result.error_message = "Empty XML content.";
    return result;
  }

  // Check if raw <mxGraphModel> exists in the document
  if (xml_data.find("<mxGraphModel") != std::string_view::npos) {
    parse_mx_graph_model(xml_data, out_graph, result);
    return result;
  }

  // Check if compressed diagram exists inside <diagram>
  size_t diag_pos = xml_data.find("<diagram");
  if (diag_pos != std::string_view::npos) {
    size_t content_start = xml_data.find('>', diag_pos);
    size_t diag_end = xml_data.find("</diagram>", content_start);
    if (content_start != std::string_view::npos && diag_end != std::string_view::npos) {
      content_start++;
      std::string_view payload = xml_data.substr(content_start, diag_end - content_start);
      // Trim whitespace
      while (!payload.empty() && std::isspace(static_cast<unsigned char>(payload.front()))) payload.remove_prefix(1);
      while (!payload.empty() && std::isspace(static_cast<unsigned char>(payload.back()))) payload.remove_suffix(1);

      if (!payload.empty()) {
        std::string decompressed = decompress_diagram_payload(payload);
        if (!decompressed.empty() && decompressed.find("<mxGraphModel") != std::string::npos) {
          parse_mx_graph_model(decompressed, out_graph, result);
          return result;
        }
      }
    }
  }

  // Fallback direct parse attempt
  if (!parse_mx_graph_model(xml_data, out_graph, result)) {
    result.error_message = "Failed to parse Draw.io XML: invalid or missing <mxGraphModel>.";
    result.success = false;
  }
  return result;
}

} // namespace madi::io
