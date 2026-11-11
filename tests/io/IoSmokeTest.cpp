// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "madi/core/GraphModel.hpp"
#include "madi/io/DrawioParser.hpp"
#include "madi/io/StorageEngine.hpp"

#include <cassert>
#include <iostream>

using namespace madi;

int main() {
  std::cout << "===> Running IoSmokeTest...\n";

  // 1. Create a model with embedded newlines & quotes in properties
  core::GraphModel orig_model;
  core::Node n1;
  n1.name = "Root Module \"Alpha\"";
  n1.bounds = {50.0, 50.0, 160.0, 90.0};
  n1.properties["embedded_graph_json"] = "{\n  \"schema_version\": 1,\n  \"nodes\": [{\"id\": 10, \"name\": \"Sub\"}]\n}";
  n1.properties["author"] = "Madí \"Team\"\nSpecial Line";
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

  io::StorageEngine storage;

  // 2. Test Standard RFC 8259 JSON Serialization & Deserialization
  std::cout << "  -> Testing RFC 8259 standard JSON (.madi) serializer...\n";
  std::string json_str = storage.serialize_json(orig_model);
  assert(!json_str.empty());
  assert(json_str.find("\"format\": \"madi\"") != std::string::npos);
  assert(json_str.find("\"nodes\":") != std::string::npos);
  assert(json_str.find("\"edges\":") != std::string::npos);

  core::GraphModel loaded_json;
  auto json_res = storage.deserialize_json(json_str, loaded_json);
  assert(json_res.success);
  assert(loaded_json.node_count() == 2);
  assert(loaded_json.edge_count() == 1);

  const auto* loaded_jn1 = loaded_json.find_node(nid1);
  assert(loaded_jn1 != nullptr);
  assert(loaded_jn1->name == "Root Module \"Alpha\"");
  auto it_embed_json = loaded_jn1->properties.find("embedded_graph_json");
  assert(it_embed_json != loaded_jn1->properties.end());
  assert(it_embed_json->second.find("schema_version") != std::string::npos);
  assert(it_embed_json->second.find('\n') != std::string::npos);

  // 3. Test JSONC (JSON with Comments & Trailing Commas)
  std::cout << "  -> Testing JSONC (comments & trailing commas) acceptance...\n";
  std::string jsonc_text = R"(
    // Top-level Madí Diagram Model with single-line comment
    {
      /* Multi-line block comment
         describing the diagram format */
      "schema_version": 1,
      "format": "madi",
      "nodes": [
        {
          "id": 1,
          "name": "Node With // in name and /* in text */", // line comment after node
          "shape": 0,
          "bounds": [100.0, 120.0, 160.0, 80.0],
          "properties": {
            "comment_test": "valid value", // property comment
          }, // trailing comma in object
          "ports": [
            {
              "id": 101,
              "name": "out",
              "anchor": [1.0, 0.5],
              "direction": "east",
            }, // trailing comma in array
          ]
        },
      ], // trailing comma in nodes array
      "edges": [
        // empty edge list
      ],
    }
  )";

  core::GraphModel loaded_jsonc;
  auto jsonc_res = storage.deserialize_json(jsonc_text, loaded_jsonc);
  assert(jsonc_res.success);
  assert(loaded_jsonc.node_count() == 1);
  const auto* jc_node = loaded_jsonc.find_node(1);
  assert(jc_node != nullptr);
  assert(jc_node->name == "Node With // in name and /* in text */");
  assert(jc_node->properties.at("comment_test") == "valid value");
  assert(jc_node->port_ids.size() == 1);
  const auto* jc_port = loaded_jsonc.find_port(jc_node->port_ids[0]);
  assert(jc_port != nullptr);
  assert(jc_port->direction == core::PortDirection::East);

  // 4. Test Unified StorageEngine::deserialize with JSON, JSONC, and YAML fallback
  std::cout << "  -> Testing unified StorageEngine::deserialize with auto-fallback...\n";
  core::GraphModel auto_json_model;
  assert(storage.deserialize(json_str, auto_json_model).success);
  assert(auto_json_model.node_count() == 2);

  core::GraphModel auto_jsonc_model;
  assert(storage.deserialize(jsonc_text, auto_jsonc_model).success);
  assert(auto_jsonc_model.node_count() == 1);

  // 5. Test Backward-compatible YAML Serialization & Deserialization
  std::cout << "  -> Testing legacy YAML backward compatibility...\n";
  std::string yaml_str = storage.serialize_yaml(orig_model);
  assert(!yaml_str.empty());

  core::GraphModel loaded_yaml;
  auto yaml_res = storage.deserialize_yaml(yaml_str, loaded_yaml);
  assert(yaml_res.success);
  assert(loaded_yaml.node_count() == 2);
  assert(loaded_yaml.edge_count() == 1);

  core::GraphModel auto_yaml_model;
  assert(storage.deserialize(yaml_str, auto_yaml_model).success);
  assert(auto_yaml_model.node_count() == 2);

  // 6. Test Draw.io XML Import / Export
  std::cout << "  -> Testing Draw.io XML import / export...\n";
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
