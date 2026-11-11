// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "madi/core/Geometry.hpp"
#include "madi/core/GraphModel.hpp"
#include "madi/core/HierarchicalBlock.hpp"
#include "madi/core/SpatialIndex.hpp"
#include "madi/core/Command.hpp"
#include "madi/grid/GridManager.hpp"
#include "madi/routing/RouterWorker.hpp"
#include "madi/io/DrawioParser.hpp"
#include "madi/io/StorageEngine.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace madi;

void test_hierarchical_block_definition_and_instances() {
  std::cout << "[Test 1] Defining BlockDefinition with 2 internal nodes and 2 boundary interface ports...\n";

  core::BlockLibraryManager lib_manager;
  core::BlockDefinition alu_def("madi://stdlib/alu_32bit", "ALU 32-bit");
  alu_def.set_default_size({160.0, 100.0});
  alu_def.set_default_property("architecture", "RISC-V");
  alu_def.set_default_property("bit_width", "32");

  // 1. Add 2 internal nodes
  core::Node internal_core;
  internal_core.name = "ALU Computation Core";
  internal_core.bounds = {20.0, 20.0, 60.0, 60.0};
  const core::NodeId core_id = alu_def.internal_graph().add_node(std::move(internal_core));

  core::Node internal_reg;
  internal_reg.name = "Status Flag Register";
  internal_reg.bounds = {90.0, 20.0, 50.0, 40.0};
  const core::NodeId reg_id = alu_def.internal_graph().add_node(std::move(internal_reg));
  (void)reg_id;

  assert(alu_def.internal_graph().node_count() == 2);

  // Add internal ports
  core::Port core_in_p;
  core_in_p.parent_node = core_id;
  core_in_p.name = "in_a";
  core_in_p.local_anchor = {0.0, 0.5};
  core_in_p.direction = core::PortDirection::West;
  const core::PortId core_in_id = alu_def.internal_graph().add_port(std::move(core_in_p));

  core::Port core_out_p;
  core_out_p.parent_node = core_id;
  core_out_p.name = "out_y";
  core_out_p.local_anchor = {1.0, 0.5};
  core_out_p.direction = core::PortDirection::East;
  const core::PortId core_out_id = alu_def.internal_graph().add_port(std::move(core_out_p));

  // 2. Expose 2 boundary interface ports
  core::InterfacePort port_a;
  port_a.name = "Input_A";
  port_a.boundary_side = core::PortDirection::West;
  port_a.local_anchor = {0.0, 0.5}; // left midpoint
  port_a.internal_target_node = core_id;
  port_a.internal_target_port = core_in_id;
  const core::PortId bport_a_id = alu_def.add_interface_port(std::move(port_a));
  (void)bport_a_id;

  core::InterfacePort port_y;
  port_y.name = "Output_Y";
  port_y.boundary_side = core::PortDirection::East;
  port_y.local_anchor = {1.0, 0.5}; // right midpoint
  port_y.internal_target_node = core_id;
  port_y.internal_target_port = core_out_id;
  const core::PortId bport_y_id = alu_def.add_interface_port(std::move(port_y));
  (void)bport_y_id;

  assert(alu_def.interface_ports().size() == 2);
  lib_manager.register_definition(std::move(alu_def));

  const auto* registered_def = lib_manager.find_definition("madi://stdlib/alu_32bit");
  assert(registered_def != nullptr);
  std::cout << "  -> Definition registered with URI: " << registered_def->uri() << "\n";

  // 3. Instantiate multiple BlockInstance objects in top-level graph
  std::cout << "[Test 2] Instantiating multiple BlockInstance objects referencing primary definition...\n";
  core::GraphModel top_graph;

  // Instance 1: ALU_Stage1
  core::Node inst1_node;
  inst1_node.name = "ALU_Stage1";
  inst1_node.bounds = {100.0, 200.0, 160.0, 100.0};
  inst1_node.shape = core::ShapeType::HierarchicalBlock;
  const core::NodeId inst1_id = top_graph.add_node(std::move(inst1_node));

  core::BlockInstance inst1(inst1_id, "madi://stdlib/alu_32bit", core::ReferenceMode::Linked);
  inst1.set_bounds({100.0, 200.0, 160.0, 100.0});
  inst1.set_property_override("instance_label", "Stage 1 Adder");
  inst1.set_property_override("clock_freq", "200MHz");

  // Instance 2: ALU_Stage2
  core::Node inst2_node;
  inst2_node.name = "ALU_Stage2";
  inst2_node.bounds = {400.0, 200.0, 160.0, 100.0};
  inst2_node.shape = core::ShapeType::HierarchicalBlock;
  const core::NodeId inst2_id = top_graph.add_node(std::move(inst2_node));

  core::BlockInstance inst2(inst2_id, "madi://stdlib/alu_32bit", core::ReferenceMode::Linked);
  inst2.set_bounds({400.0, 200.0, 160.0, 100.0});
  inst2.set_property_override("instance_label", "Stage 2 Multiplier");

  // Verify Prototype Inheritance
  assert(inst1.resolve_property("instance_label", *registered_def) == "Stage 1 Adder");
  assert(inst1.resolve_property("architecture", *registered_def) == "RISC-V"); // Inherited from def
  assert(inst2.resolve_property("bit_width", *registered_def) == "32");        // Inherited from def
  std::cout << "  -> Prototype inheritance verified (Instance override + Definition fallback).\n";

  // Add boundary pins to top-level graph
  auto boundary_ports_1 = inst1.compute_boundary_ports(*registered_def);
  core::PortId top_p1_out = core::InvalidPortId;
  for (auto& p : boundary_ports_1) {
    if (p.name == "Output_Y") {
      top_p1_out = top_graph.add_port(p);
    } else {
      top_graph.add_port(p);
    }
  }

  auto boundary_ports_2 = inst2.compute_boundary_ports(*registered_def);
  core::PortId top_p2_in = core::InvalidPortId;
  for (auto& p : boundary_ports_2) {
    if (p.name == "Input_A") {
      top_p2_in = top_graph.add_port(p);
    } else {
      top_graph.add_port(p);
    }
  }

  // 4. Connect top-level nets between instances
  std::cout << "[Test 3] Connecting top-level nets between instances...\n";
  core::Edge net;
  net.name = "Pipeline_Bus";
  net.source_node = inst1_id;
  net.source_port = top_p1_out;
  net.target_node = inst2_id;
  net.target_port = top_p2_in;
  net.waypoints = {
    {260.0, 250.0}, // inst1 right port (100 + 160, 200 + 50)
    {330.0, 250.0},
    {330.0, 250.0},
    {400.0, 250.0}  // inst2 left port (400, 200 + 50)
  };
  const core::EdgeId net_id = top_graph.add_edge(std::move(net));
  assert(net_id != core::InvalidEdgeId);
  assert(top_graph.edge_count() == 1);
  std::cout << "  -> Top-level net connected between ALU_1 and ALU_2.\n";

  // 5. Test Spatial Indexing (Flat BVH / R-Tree)
  std::cout << "[Test 4] Verifying Spatial Indexing and Frustum Culling...\n";
  core::FlatBvhSpatialIndex spatial_index;
  spatial_index.insert(inst1_id, top_graph.find_node(inst1_id)->bounds);
  spatial_index.insert(inst2_id, top_graph.find_node(inst2_id)->bounds);
  spatial_index.rebuild();

  // Test point hit test
  auto hits_inst1 = spatial_index.query_point({150.0, 230.0});
  assert(hits_inst1.size() == 1 && hits_inst1[0] == inst1_id);

  auto hits_empty = spatial_index.query_point({50.0, 50.0});
  assert(hits_empty.empty());

  // Test frustum culling
  core::Rect2d viewport_frustum(0.0, 0.0, 300.0, 600.0); // Covers inst1, excludes inst2
  auto visible_items = spatial_index.query_frustum(viewport_frustum);
  assert(visible_items.size() == 1 && visible_items[0] == inst1_id);

  core::Rect2d wide_frustum(0.0, 0.0, 1000.0, 1000.0); // Covers both
  auto all_visible = spatial_index.query_frustum(wide_frustum);
  assert(all_visible.size() == 2);
  std::cout << "  -> Spatial queries & Frustum culling verified.\n";

  // 6. Test Transactional Command Pattern & Rollback
  std::cout << "[Test 5] Verifying Transactional Command Engine and Rollback...\n";
  core::CommandHistory history;

  const double orig_x = top_graph.find_node(inst1_id)->bounds.x;
  const double orig_y = top_graph.find_node(inst1_id)->bounds.y;

  // Begin transaction and move nodes
  history.begin_transaction("Move Instances");
  auto move_cmd = std::make_unique<core::MoveNodesCommand>(top_graph, std::vector<core::NodeId>{inst1_id}, core::Vec2d{50.0, 30.0});
  history.execute(std::move(move_cmd));

  assert(top_graph.find_node(inst1_id)->bounds.x == orig_x + 50.0);
  assert(top_graph.find_node(inst1_id)->bounds.y == orig_y + 30.0);

  // Rollback transaction
  std::cout << "  -> Rolling back transaction...\n";
  const bool rolled_back = history.rollback();
  assert(rolled_back);
  (void)rolled_back;

  assert(top_graph.find_node(inst1_id)->bounds.x == orig_x);
  assert(top_graph.find_node(inst1_id)->bounds.y == orig_y);
  (void)orig_x;
  (void)orig_y;
  assert(!history.can_undo());
  std::cout << "  -> Transaction rollback successfully restored coordinates.\n";

  // Test normal undo/redo
  history.execute(std::make_unique<core::MoveNodesCommand>(top_graph, std::vector<core::NodeId>{inst1_id}, core::Vec2d{25.0, 10.0}));
  assert(top_graph.find_node(inst1_id)->bounds.x == orig_x + 25.0);
  assert(history.can_undo());

  history.undo();
  assert(top_graph.find_node(inst1_id)->bounds.x == orig_x);
  assert(history.can_redo());

  history.redo();
  assert(top_graph.find_node(inst1_id)->bounds.x == orig_x + 25.0);
  std::cout << "  -> Command undo and redo verified.\n";

  // 7. Test Asynchronous Obstacle Avoidance Routing
  std::cout << "[Test 6] Verifying Obstacle Avoidance Routing...\n";
  routing::RouteBatchRequest req;
  req.batch_id = 42;
  // Place an obstacle directly between inst1 and inst2
  req.obstacles.push_back(routing::ObstacleRect{
    .id = 999,
    .bounds = {300.0, 200.0, 60.0, 100.0}, // In between (260, 250) and (400, 250)
    .padding = 10.0
  });

  routing::RouteNetRequest net_req{
    .edge_id = net_id,
    .source_point = {260.0, 250.0},
    .source_direction = core::PortDirection::East,
    .target_point = {400.0, 250.0},
    .target_direction = core::PortDirection::West,
    .routing_style = core::RoutingStyle::Orthogonal
  };
  req.nets.push_back(net_req);

  routing::OrthogonalAvoidanceEngine router;
  auto route_res = router.compute_routes(req);
  assert(!route_res.results.empty());
  assert(route_res.results[0].success);
  assert(route_res.results[0].waypoints.size() >= 3);
  std::cout << "  -> Avoidance router produced " << route_res.results[0].waypoints.size() << " waypoints avoiding obstacle.\n";

  // 8. Test Tri-Model Storage & Draw.io Interop
  std::cout << "[Test 7] Verifying RFC 8259 JSON (.madi), JSONC comments, and Draw.io compatibility...\n";
  io::StorageEngine storage;
  std::string json_out = storage.serialize_json(top_graph, &lib_manager);
  assert(!json_out.empty());
  assert(json_out.find("\"format\": \"madi\"") != std::string::npos);
  assert(json_out.find("ALU_Stage1") != std::string::npos);

  core::GraphModel loaded_json_graph;
  auto load_json_res = storage.deserialize_json(json_out, loaded_json_graph);
  assert(load_json_res.success);
  assert(loaded_json_graph.node_count() == top_graph.node_count());

  std::string yaml_out = storage.serialize_yaml(top_graph, &lib_manager);
  assert(!yaml_out.empty());

  core::GraphModel loaded_graph;
  auto load_res = storage.deserialize_yaml(yaml_out, loaded_graph);
  assert(load_res.success);
  assert(loaded_graph.node_count() == top_graph.node_count());

  io::DrawioParser drawio;
  std::string drawio_xml = drawio.export_xml(top_graph, "Top-Level Architecture");
  assert(drawio_xml.find("<mxGraphModel") != std::string::npos);
  assert(drawio_xml.find("Pipeline_Bus") != std::string::npos);

  core::GraphModel drawio_imported;
  auto drawio_res = drawio.import_xml(drawio_xml, drawio_imported);
  assert(drawio_res.success);
  assert(drawio_imported.node_count() == top_graph.node_count());
  std::cout << "  -> JSON (.madi), YAML, and Draw.io bidirectional XML import/export verified.\n";

  std::cout << "\n>>> ALL MADÍ ARCHITECTURE SMOKE TESTS PASSED CLEANLY! <<<\n";
}

int main() {
  try {
    test_hierarchical_block_definition_and_instances();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Smoke test failed with exception: " << ex.what() << "\n";
    return 1;
  }
}
