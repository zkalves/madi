// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/core/GraphModel.hpp"
#include "vector/io/DrawioParser.hpp"
#include "vector/io/StorageEngine.hpp"

#include <cassert>
#include <iostream>

using namespace vector;

int main() {
  std::cout << "===> Running IoSmokeTest...\n";

  // 1. Create a model with embedded newlines & quotes in properties
  core::GraphModel orig_model;
  core::Node n1;
  n1.name = "Root Module \"Alpha\"";
  n1.bounds = {50.0, 50.0, 160.0, 90.0};
  n1.properties["embedded_graph_yaml"] = "# Line 1\nschema_version: 1\nnodes:\n  - id: 10\n    name: \"Sub\"\n";
  n1.properties["author"] = "VECTOR \"Team\"\nSpecial Line";
  const core::NodeId nid1 = orig_model.add_node(std::move(n1));

  core::Port p1;
  p1.parent_node = nid1;
  p1.name = "Data_Out";
  p1.local_anchor = {1.0, 0.5};
  p1.direction = core::PortDirection::East;
  const core::PortId pid1 = orig_model.add_port(std::move(p1));

  core::Node n2;
  n2.name = "Destination";
  n2.bounds = {300.0, 50.0, 120.0, 80.0};
  const core::NodeId nid2 = orig_model.add_node(std::move(n2));

  core::Port p2;
  p2.parent_node = nid2;
  p2.name = "Data_In";
  p2.local_anchor = {0.0, 0.5};
  p2.direction = core::PortDirection::West;
  const core::PortId pid2 = orig_model.add_port(std::move(p2));

  core::Edge edge;
  edge.name = "Bus_Net";
  edge.source_node = nid1;
  edge.source_port = pid1;
  edge.target_node = nid2;
  edge.target_port = pid2;
  orig_model.add_edge(std::move(edge));

  // 2. Test YAML Serialization & Deserialization with unescaping
  io::StorageEngine storage;
  std::string yaml_str = storage.serialize_yaml(orig_model);
  assert(!yaml_str.empty());

  core::GraphModel loaded_yaml;
  auto yaml_res = storage.deserialize_yaml(yaml_str, loaded_yaml);
  assert(yaml_res.success);
  assert(loaded_yaml.node_count() == 2);
  assert(loaded_yaml.edge_count() == 1);

  const auto* loaded_n1 = loaded_yaml.find_node(nid1);
  assert(loaded_n1 != nullptr);
  assert(loaded_n1->name == "Root Module \"Alpha\"");
  auto it_embed = loaded_n1->properties.find("embedded_graph_yaml");
  assert(it_embed != loaded_n1->properties.end());
  // Verify real newlines and quotes were restored correctly
  assert(it_embed->second.find('\n') != std::string::npos);
  assert(it_embed->second.find("schema_version: 1") != std::string::npos);

  // 3. Test Draw.io XML Import / Export
  io::DrawioParser drawio;
  std::string drawio_xml = drawio.export_xml(orig_model, "Architecture_Page");
  assert(!drawio_xml.empty());
  assert(drawio_xml.find("Bus_Net") != std::string::npos);

  core::GraphModel loaded_drawio;
  auto drawio_res = drawio.import_xml(drawio_xml, loaded_drawio);
  assert(drawio_res.success);
  assert(loaded_drawio.node_count() == 2);
  assert(loaded_drawio.edge_count() == 1);

  std::cout << "===> IoSmokeTest passed successfully!\n";
  return 0;
}
