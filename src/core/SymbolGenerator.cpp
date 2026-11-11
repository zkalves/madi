// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "madi/core/SymbolGenerator.hpp"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace madi::core {

Node SymbolGenerator::create_default_rectangle_symbol(
    const std::string& name,
    Vec2d center,
    const std::vector<std::string>& input_ports,
    const std::vector<std::string>& output_ports,
    double min_width,
    double min_height) {
  Node node;
  node.name = name.empty() ? "Hierarchical Block" : name;
  node.shape = ShapeType::HierarchicalBlock;

  const size_t max_side_ports = std::max(input_ports.size(), output_ports.size());
  const double h = std::max(min_height, max_side_ports * 28.0 + 44.0);
  const double w = std::max(min_width, 160.0);

  node.bounds = {center.x - w * 0.5, center.y - h * 0.5, w, h};
  return node;
}

void SymbolGenerator::sync_symbol_ports(
    Node& symbol_node,
    GraphModel& parent_graph,
    const std::vector<std::string>& input_ports,
    const std::vector<std::string>& output_ports) {
  // 1. Ensure symbol rectangle height accommodates all ports
  const size_t max_side_ports = std::max(input_ports.size(), output_ports.size());
  const double needed_h = std::max(100.0, max_side_ports * 28.0 + 44.0);
  if (symbol_node.bounds.height < needed_h) {
    symbol_node.bounds.height = needed_h;
  }
  if (symbol_node.bounds.width < 140.0) {
    symbol_node.bounds.width = 140.0;
  }

  // 2. Map existing boundary ports on the symbol by name
  std::unordered_map<std::string, PortId> existing_named_ports;
  for (PortId pid : symbol_node.port_ids) {
    if (const auto* p = parent_graph.find_port(pid)) {
      if (p->is_boundary_interface) {
        existing_named_ports[p->name] = pid;
      }
    }
  }

  std::unordered_set<PortId> retained_pids;

  // 3. Layout input ports on the West (left) side
  const size_t num_in = input_ports.size();
  for (size_t i = 0; i < num_in; ++i) {
    const std::string& pname = input_ports[i];
    const double y_norm = (static_cast<double>(i) + 1.0) / (static_cast<double>(num_in) + 1.0);
    const Vec2d anchor = {0.0, y_norm};

    auto it = existing_named_ports.find(pname);
    if (it != existing_named_ports.end()) {
      if (auto* p = parent_graph.find_port(it->second)) {
        p->local_anchor = anchor;
        p->direction = PortDirection::West;
        p->pin_type = "input";
        p->is_boundary_interface = true;
        retained_pids.insert(it->second);
      }
    } else {
      Port new_p;
      new_p.parent_node = symbol_node.id;
      new_p.name = pname;
      new_p.local_anchor = anchor;
      new_p.direction = PortDirection::West;
      new_p.pin_type = "input";
      new_p.is_boundary_interface = true;
      const PortId new_pid = parent_graph.add_port(std::move(new_p));
      retained_pids.insert(new_pid);
    }
  }

  // 4. Layout output ports on the East (right) side
  const size_t num_out = output_ports.size();
  for (size_t j = 0; j < num_out; ++j) {
    const std::string& pname = output_ports[j];
    const double y_norm = (static_cast<double>(j) + 1.0) / (static_cast<double>(num_out) + 1.0);
    const Vec2d anchor = {1.0, y_norm};

    auto it = existing_named_ports.find(pname);
    if (it != existing_named_ports.end()) {
      if (auto* p = parent_graph.find_port(it->second)) {
        p->local_anchor = anchor;
        p->direction = PortDirection::East;
        p->pin_type = "output";
        p->is_boundary_interface = true;
        retained_pids.insert(it->second);
      }
    } else {
      Port new_p;
      new_p.parent_node = symbol_node.id;
      new_p.name = pname;
      new_p.local_anchor = anchor;
      new_p.direction = PortDirection::East;
      new_p.pin_type = "output";
      new_p.is_boundary_interface = true;
      const PortId new_pid = parent_graph.add_port(std::move(new_p));
      retained_pids.insert(new_pid);
    }
  }

  // 5. Remove any old boundary ports that are no longer present
  for (auto it = symbol_node.port_ids.begin(); it != symbol_node.port_ids.end();) {
    const PortId pid = *it;
    if (const auto* p = parent_graph.find_port(pid)) {
      if (p->is_boundary_interface && !retained_pids.contains(pid)) {
        parent_graph.remove_port(pid);
        it = symbol_node.port_ids.erase(it);
        continue;
      }
    }
    ++it;
  }

  // 6. Ensure all retained ports are in symbol_node.port_ids
  for (PortId pid : retained_pids) {
    if (std::find(symbol_node.port_ids.begin(), symbol_node.port_ids.end(), pid) == symbol_node.port_ids.end()) {
      symbol_node.port_ids.push_back(pid);
    }
  }
}

void SymbolGenerator::sync_symbol_from_diagram(
    Node& symbol_node,
    GraphModel& parent_graph,
    const GraphModel& child_diagram) {
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;

  // Scan nodes in child diagram to find all input and output ports
  for (const auto& [nid, node] : child_diagram.nodes()) {
    if (node.is_input_port()) {
      inputs.push_back(node.name);
    } else if (node.is_output_port()) {
      outputs.push_back(node.name);
    }
  }

  // If no dedicated port nodes were found, also check boundary interface ports
  if (inputs.empty() && outputs.empty()) {
    for (const auto& [pid, p] : child_diagram.ports()) {
      if (p.is_boundary_interface || p.pin_type == "input" || p.pin_type == "output") {
        if (p.pin_type == "output" || p.direction == PortDirection::East) {
          outputs.push_back(p.name);
        } else {
          inputs.push_back(p.name);
        }
      }
    }
  }

  // If the instance is set to allow drift, preserve custom dimensions and existing pin placements!
  auto it_drift = symbol_node.properties.find("definition_sync_mode");
  if (it_drift != symbol_node.properties.end() && it_drift->second == "allow_drift") {
    // Collect existing port names
    std::unordered_set<std::string> existing_names;
    for (PortId pid : symbol_node.port_ids) {
      if (const auto* p = parent_graph.find_port(pid)) {
        existing_names.insert(p->name);
      }
    }
    // Only append newly added ports without modifying existing ones
    for (const auto& in_name : inputs) {
      if (!existing_names.contains(in_name)) {
        Port new_p;
        new_p.parent_node = symbol_node.id;
        new_p.name = in_name;
        new_p.direction = PortDirection::West;
        new_p.pin_type = "input";
        new_p.local_anchor = {0.0, 0.5};
        new_p.is_boundary_interface = true;
        parent_graph.add_port(std::move(new_p));
      }
    }
    for (const auto& out_name : outputs) {
      if (!existing_names.contains(out_name)) {
        Port new_p;
        new_p.parent_node = symbol_node.id;
        new_p.name = out_name;
        new_p.direction = PortDirection::East;
        new_p.pin_type = "output";
        new_p.local_anchor = {1.0, 0.5};
        new_p.is_boundary_interface = true;
        parent_graph.add_port(std::move(new_p));
      }
    }
    return;
  }

  sync_symbol_ports(symbol_node, parent_graph, inputs, outputs);

  if (it_drift != symbol_node.properties.end() && it_drift->second == "follow_primary") {
    symbol_node.bounds.width = 160.0;
    symbol_node.bounds.height = std::max(100.0, std::max(inputs.size(), outputs.size()) * 28.0 + 44.0);
  }
}

void SymbolGenerator::auto_fit_dimensions(Node& symbol_node, const GraphModel& parent_graph) {
  size_t count_west = 0, count_east = 0, count_north = 0, count_south = 0;
  for (PortId pid : symbol_node.port_ids) {
    if (const auto* p = parent_graph.find_port(pid)) {
      if (p->direction == PortDirection::West || p->local_anchor.x <= 0.2) count_west++;
      else if (p->direction == PortDirection::East || p->local_anchor.x >= 0.8) count_east++;
      else if (p->direction == PortDirection::North || p->local_anchor.y <= 0.2) count_north++;
      else count_south++;
    }
  }
  const double needed_h = std::max(80.0, std::max(count_west, count_east) * 30.0 + 44.0);
  const double needed_w = std::max(140.0, std::max(count_north, count_south) * 40.0 + 80.0);
  symbol_node.bounds.width = needed_w;
  symbol_node.bounds.height = needed_h;
}

} // namespace madi::core
