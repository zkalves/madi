// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_CORE_SYMBOL_GENERATOR_HPP
#define VECTOR_CORE_SYMBOL_GENERATOR_HPP

#include "Geometry.hpp"
#include "GraphModel.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace vector::core {

class SymbolGenerator {
public:
  // Creates a default rectangular symbol with optional initial input/output ports
  static Node create_default_rectangle_symbol(
      const std::string& name,
      Vec2d center,
      const std::vector<std::string>& input_ports = {},
      const std::vector<std::string>& output_ports = {},
      double min_width = 160.0,
      double min_height = 100.0);

  // Synchronizes boundary ports on a symbol node from input/output port definitions
  static void sync_symbol_ports(
      Node& symbol_node,
      GraphModel& parent_graph,
      const std::vector<std::string>& input_ports,
      const std::vector<std::string>& output_ports);

  // Synchronizes symbol node boundary ports from a child GraphModel
  static void sync_symbol_from_diagram(
      Node& symbol_node,
      GraphModel& parent_graph,
      const GraphModel& child_diagram);

  // Computes optimal dimensions for a symbol node based on its pin count
  static void auto_fit_dimensions(Node& symbol_node, const GraphModel& parent_graph);
};

} // namespace vector::core

#endif // VECTOR_CORE_SYMBOL_GENERATOR_HPP
