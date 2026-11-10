// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/core/Geometry.hpp"
#include "vector/core/GraphModel.hpp"
#include "vector/core/HierarchicalBlock.hpp"
#include "vector/core/SpatialIndex.hpp"
#include "vector/io/DrawioParser.hpp"
#include "vector/io/StorageEngine.hpp"
#include "vector/routing/RouterWorker.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

namespace {

void print_help(std::string_view prog_name) {
  std::cout << "VECTOR CLI - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing\n";
  std::cout << "Usage: " << prog_name << " [options] [arguments]\n\n";
  std::cout << "Options:\n";
  std::cout << "  -h, --help                 Show this help message\n";
  std::cout << "  -v, --version              Display engine version\n";
  std::cout << "  --stats <file>             Inspect and display diagram statistics\n";
  std::cout << "  --convert <in> <out>       Convert diagram between Draw.io XML and YAML\n";
  std::cout << "  --route <in> <out>         Re-route all nets using obstacle avoidance engine\n";
  std::cout << "  --export-svg <in> <out>    Export diagram directly to SVG vector image\n";
}

bool load_diagram(const std::string& file_path, vector::core::GraphModel& model, std::string& err) {
  std::ifstream in(file_path);
  if (!in.is_open()) {
    err = "Unable to open input file: " + file_path;
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());

  if (file_path.ends_with(".drawio") || file_path.ends_with(".xml")) {
    vector::io::DrawioParser parser;
    auto res = parser.import_xml(content, model);
    if (!res.success) {
      err = "Draw.io parser error: " + res.error_message;
      return false;
    }
  } else {
    vector::io::StorageEngine storage;
    auto res = storage.deserialize_yaml(content, model);
    if (!res.success) {
      err = "YAML deserializer error: " + res.error_message;
      return false;
    }
  }
  return true;
}

bool save_diagram(const std::string& file_path, const vector::core::GraphModel& model, std::string& err) {
  std::ofstream out(file_path);
  if (!out.is_open()) {
    err = "Unable to open output file for writing: " + file_path;
    return false;
  }

  if (file_path.ends_with(".drawio") || file_path.ends_with(".xml")) {
    vector::io::DrawioParser parser;
    out << parser.export_xml(model);
  } else {
    vector::io::StorageEngine storage;
    out << storage.serialize_yaml(model);
  }
  return true;
}

std::string render_svg(const vector::core::GraphModel& model) {
  double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
  bool has_content = false;

  for (const auto& [nid, n] : model.nodes()) {
    has_content = true;
    min_x = std::min(min_x, n.bounds.x);
    min_y = std::min(min_y, n.bounds.y);
    max_x = std::max(max_x, n.bounds.x + n.bounds.width);
    max_y = std::max(max_y, n.bounds.y + n.bounds.height);
  }
  for (const auto& [eid, e] : model.edges()) {
    for (const auto& pt : e.waypoints) {
      has_content = true;
      min_x = std::min(min_x, pt.x);
      min_y = std::min(min_y, pt.y);
      max_x = std::max(max_x, pt.x);
      max_y = std::max(max_y, pt.y);
    }
  }

  if (!has_content) {
    min_x = 0; min_y = 0; max_x = 800; max_y = 600;
  }

  const double margin = 40.0;
  min_x -= margin; min_y -= margin;
  max_x += margin; max_y += margin;
  const double width = std::max(100.0, max_x - min_x);
  const double height = std::max(100.0, max_y - min_y);

  std::ostringstream ss;
  ss << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
  ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" ";
  ss << "width=\"" << width << "\" height=\"" << height << "\" ";
  ss << "viewBox=\"" << min_x << " " << min_y << " " << width << " " << height << "\">\n";
  ss << "  <rect x=\"" << min_x << "\" y=\"" << min_y << "\" width=\"" << width << "\" height=\"" << height << "\" fill=\"#FFFFFF\"/>\n";

  // Wires
  for (const auto& [eid, edge] : model.edges()) {
    if (!edge.visible || edge.waypoints.empty()) continue;
    ss << "  <polyline points=\"";
    for (size_t i = 0; i < edge.waypoints.size(); ++i) {
      if (i > 0) ss << " ";
      ss << edge.waypoints[i].x << "," << edge.waypoints[i].y;
    }
    ss << "\" fill=\"none\" stroke=\"#2563EB\" stroke-width=\"2.0\" stroke-linejoin=\"round\" stroke-linecap=\"round\"/>\n";
    if (!edge.waypoints.empty()) {
      ss << "  <circle cx=\"" << edge.waypoints.front().x << "\" cy=\"" << edge.waypoints.front().y << "\" r=\"3.0\" fill=\"#2563EB\"/>\n";
    }
  }

  // Nodes
  for (const auto& [nid, n] : model.nodes()) {
    ss << "  <rect x=\"" << n.bounds.x << "\" y=\"" << n.bounds.y << "\" width=\"" << n.bounds.width << "\" height=\"" << n.bounds.height
       << "\" rx=\"6\" ry=\"6\" fill=\"#F8FAFC\" stroke=\"#334155\" stroke-width=\"1.5\"/>\n";
    if (!n.name.empty()) {
      const double cx = n.bounds.x + n.bounds.width * 0.5;
      const double cy = n.bounds.y + n.bounds.height * 0.5;
      ss << "  <text x=\"" << cx << "\" y=\"" << cy << "\" font-family=\"Inter, sans-serif\" font-size=\"12\" fill=\"#0F172A\" text-anchor=\"middle\" dominant-baseline=\"central\">"
         << n.name << "</text>\n";
    }
    for (vector::core::PortId pid : n.port_ids) {
      if (const auto* p = model.find_port(pid)) {
        auto pt = n.resolve_port_position(*p);
        ss << "  <circle cx=\"" << pt.x << "\" cy=\"" << pt.y << "\" r=\"3.5\" fill=\"#0EA5E9\" stroke=\"#0F172A\" stroke-width=\"1.0\"/>\n";
      }
    }
  }

  ss << "</svg>\n";
  return ss.str();
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    print_help(argv[0]);
    return 0;
  }

  const std::string_view arg1 = argv[1];
  if (arg1 == "-h" || arg1 == "--help") {
    print_help(argv[0]);
    return 0;
  } else if (arg1 == "-v" || arg1 == "--version") {
    std::cout << "VECTOR Engine version 1.0.0-alpha (C++20)\n";
    std::cout << "Licensed under GNU GPLv3\n";
    return 0;
  } else if (arg1 == "--stats" && argc >= 3) {
    const std::string file_path = argv[2];
    vector::core::GraphModel model;
    std::string err;
    if (!load_diagram(file_path, model, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }
    const auto bbox = model.get_bounding_box();
    std::cout << "=== VECTOR Diagram Statistics ===\n";
    std::cout << "File:   " << file_path << "\n";
    std::cout << "Nodes:  " << model.node_count() << "\n";
    std::cout << "Edges:  " << model.edge_count() << "\n";
    std::cout << "Ports:  " << model.port_count() << "\n";
    std::cout << "Bounds: [" << bbox.x << ", " << bbox.y << ", "
              << bbox.width << ", " << bbox.height << "]\n";
    return 0;
  } else if (arg1 == "--convert" && argc >= 4) {
    const std::string in_path = argv[2];
    const std::string out_path = argv[3];
    vector::core::GraphModel model;
    std::string err;
    if (!load_diagram(in_path, model, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }
    if (!save_diagram(out_path, model, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }
    std::cout << "Successfully converted " << in_path << " -> " << out_path
              << " (" << model.node_count() << " nodes, " << model.edge_count() << " edges)\n";
    return 0;
  } else if (arg1 == "--route" && argc >= 4) {
    const std::string in_path = argv[2];
    const std::string out_path = argv[3];
    vector::core::GraphModel model;
    std::string err;
    if (!load_diagram(in_path, model, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }

    vector::routing::RouteBatchRequest req;
    for (const auto& [nid, node] : model.nodes()) {
      req.obstacles.push_back({nid, node.bounds, 8.0});
    }
    for (const auto& [eid, edge] : model.edges()) {
      const auto* src_node = model.find_node(edge.source_node);
      const auto* tgt_node = model.find_node(edge.target_node);
      const auto* src_port = model.find_port(edge.source_port);
      const auto* tgt_port = model.find_port(edge.target_port);
      if (src_node && tgt_node) {
        vector::routing::RouteNetRequest net;
        net.edge_id = eid;
        net.source_point = src_port ? src_node->resolve_port_position(*src_port) : src_node->bounds.center();
        net.source_direction = src_port ? src_port->direction : vector::core::PortDirection::Omnidirectional;
        net.target_point = tgt_port ? tgt_node->resolve_port_position(*tgt_port) : tgt_node->bounds.center();
        net.target_direction = tgt_port ? tgt_port->direction : vector::core::PortDirection::Omnidirectional;
        net.routing_style = edge.routing_style;
        req.nets.push_back(net);
      }
    }

    vector::routing::OrthogonalAvoidanceEngine router;
    auto res = router.compute_routes(req);
    for (const auto& r : res.results) {
      if (r.success) {
        if (auto* e = model.find_edge(r.edge_id)) {
          e->waypoints = r.waypoints;
        }
      }
    }

    if (!save_diagram(out_path, model, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }
    std::cout << "Successfully re-routed " << model.edge_count() << " nets with obstacle avoidance to " << out_path << "\n";
    return 0;
  } else if (arg1 == "--export-svg" && argc >= 4) {
    const std::string in_path = argv[2];
    const std::string out_path = argv[3];
    vector::core::GraphModel model;
    std::string err;
    if (!load_diagram(in_path, model, err)) {
      std::cerr << "Error: " << err << "\n";
      return 1;
    }
    std::string svg = render_svg(model);
    std::ofstream out(out_path);
    if (!out.is_open()) {
      std::cerr << "Error opening output SVG file: " << out_path << "\n";
      return 1;
    }
    out << svg;
    std::cout << "Successfully exported SVG diagram to " << out_path << "\n";
    return 0;
  }

  print_help(argv[0]);
  return 0;
}
