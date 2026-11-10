// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/core/Command.hpp"
#include "vector/core/Geometry.hpp"
#include "vector/core/GraphModel.hpp"
#include "vector/core/HierarchicalBlock.hpp"
#include "vector/core/SpatialIndex.hpp"

#include <cassert>
#include <iostream>

using namespace vector;

int main() {
  std::cout << "===> Running CoreSmokeTest...\n";

  // 1. Graph Model & Hierarchy Definitions
  core::BlockLibraryManager lib_manager;
  core::BlockDefinition alu_def("vector://stdlib/alu_32bit", "ALU 32-bit");
  alu_def.set_default_size({160.0, 100.0});
  alu_def.set_default_property("architecture", "RISC-V");

  core::Node core_node;
  core_node.name = "ALU Core";
  core_node.bounds = {20.0, 20.0, 60.0, 60.0};
  const core::NodeId core_id = alu_def.internal_graph().add_node(std::move(core_node));
  assert(core_id != core::InvalidNodeId);

  core::Port port_out;
  port_out.parent_node = core_id;
  port_out.name = "out_y";
  port_out.local_anchor = {1.0, 0.5};
  port_out.direction = core::PortDirection::East;
  const core::PortId out_id = alu_def.internal_graph().add_port(std::move(port_out));
  assert(out_id != core::InvalidPortId);

  core::InterfacePort if_port;
  if_port.name = "Output_Y";
  if_port.boundary_side = core::PortDirection::East;
  if_port.local_anchor = {1.0, 0.5};
  if_port.internal_target_node = core_id;
  if_port.internal_target_port = out_id;
  alu_def.add_interface_port(std::move(if_port));
  lib_manager.register_definition(std::move(alu_def));

  // 2. Spatial Index & Frustum Culling
  core::GraphModel top_graph;
  core::Node n1;
  n1.name = "N1";
  n1.bounds = {0.0, 0.0, 100.0, 50.0};
  const core::NodeId n1_id = top_graph.add_node(std::move(n1));

  core::Node n2;
  n2.name = "N2";
  n2.bounds = {500.0, 500.0, 100.0, 50.0};
  const core::NodeId n2_id = top_graph.add_node(std::move(n2));

  core::FlatBvhSpatialIndex spatial_index;
  spatial_index.insert(n1_id, {0.0, 0.0, 100.0, 50.0});
  spatial_index.insert(n2_id, {500.0, 500.0, 100.0, 50.0});

  auto hits_box = spatial_index.query_box({-10.0, -10.0, 150.0, 100.0});
  assert(hits_box.size() == 1);
  assert(hits_box[0] == n1_id);

  auto hits_pt = spatial_index.query_point({520.0, 520.0});
  assert(hits_pt.size() == 1);
  assert(hits_pt[0] == n2_id);

  // 3. Command History & DeleteElementsCommand Undo/Redo
  core::CommandHistory history;
  history.execute(std::make_unique<core::MoveNodesCommand>(top_graph, std::vector<core::NodeId>{n1_id}, core::Vec2d{15.0, 20.0}));
  assert(top_graph.find_node(n1_id)->bounds.x == 15.0);
  assert(history.can_undo());

  history.undo();
  assert(top_graph.find_node(n1_id)->bounds.x == 0.0);
  assert(history.can_redo());

  history.redo();
  assert(top_graph.find_node(n1_id)->bounds.x == 15.0);

  // Test DeleteElementsCommand
  history.execute(std::make_unique<core::DeleteElementsCommand>(top_graph, std::vector<core::NodeId>{n1_id}));
  assert(top_graph.find_node(n1_id) == nullptr);
  assert(history.can_undo());

  history.undo();
  assert(top_graph.find_node(n1_id) != nullptr);
  assert(top_graph.find_node(n1_id)->name == "N1");

  std::cout << "===> CoreSmokeTest passed successfully!\n";
  return 0;
}
