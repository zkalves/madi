// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_CORE_HIERARCHICAL_BLOCK_HPP
#define VECTOR_CORE_HIERARCHICAL_BLOCK_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Geometry.hpp"
#include "GraphModel.hpp"

namespace vector::core {

enum class ReferenceMode : uint8_t {
  Linked,   // Tracks primary definition updates dynamically
  Embedded  // Isolated deep-copy / forked definition
};

struct InterfacePort {
  PortId id{InvalidPortId};
  std::string name;
  PortDirection boundary_side{PortDirection::West};
  Vec2d local_anchor{0.0, 0.5};  // Normalized offset [0..1] along block perimeter
  NodeId internal_target_node{InvalidNodeId};
  PortId internal_target_port{InvalidPortId};
  std::string pin_type{"generic"};
};

class BlockDefinition {
public:
  BlockDefinition() = default;
  explicit BlockDefinition(std::string uri, std::string name = {})
    : uri_(std::move(uri)), name_(std::move(name)) {}

  [[nodiscard]] const std::string& uri() const noexcept {
    return uri_;
  }
  void set_uri(std::string uri) {
    uri_ = std::move(uri);
  }

  [[nodiscard]] const std::string& name() const noexcept {
    return name_;
  }
  void set_name(std::string name) {
    name_ = std::move(name);
  }

  [[nodiscard]] const std::string& description() const noexcept {
    return description_;
  }
  void set_description(std::string desc) {
    description_ = std::move(desc);
  }

  [[nodiscard]] const Size2d& default_size() const noexcept {
    return default_size_;
  }
  void set_default_size(Size2d size) noexcept {
    default_size_ = size;
  }

  [[nodiscard]] GraphModel& internal_graph() noexcept {
    return internal_graph_;
  }
  [[nodiscard]] const GraphModel& internal_graph() const noexcept {
    return internal_graph_;
  }

  [[nodiscard]] const std::vector<InterfacePort>& interface_ports() const noexcept {
    return interface_ports_;
  }

  PortId add_interface_port(InterfacePort port) {
    if (port.id == InvalidPortId) {
      port.id = next_interface_port_id_++;
    } else if (port.id >= next_interface_port_id_) {
      next_interface_port_id_ = port.id + 1;
    }
    const PortId id = port.id;
    interface_ports_.push_back(std::move(port));
    return id;
  }

  bool remove_interface_port(PortId id) {
    auto it = std::find_if(interface_ports_.begin(), interface_ports_.end(),
                           [id](const InterfacePort& p) { return p.id == id; });
    if (it != interface_ports_.end()) {
      interface_ports_.erase(it);
      return true;
    }
    return false;
  }

  [[nodiscard]] const InterfacePort* find_interface_port(PortId id) const noexcept {
    for (const auto& p : interface_ports_) {
      if (p.id == id)
        return &p;
    }
    return nullptr;
  }

  [[nodiscard]] const std::unordered_map<std::string, std::string>& default_properties()
      const noexcept {
    return default_properties_;
  }

  void set_default_property(std::string key, std::string val) {
    default_properties_.insert_or_assign(std::move(key), std::move(val));
  }

  [[nodiscard]] std::string get_default_property(const std::string& key) const {
    auto it = default_properties_.find(key);
    return (it != default_properties_.end()) ? it->second : std::string{};
  }

private:
  std::string uri_;
  std::string name_;
  std::string description_;
  Size2d default_size_{160.0, 100.0};
  GraphModel internal_graph_;
  std::vector<InterfacePort> interface_ports_;
  std::unordered_map<std::string, std::string> default_properties_;
  PortId next_interface_port_id_{1};
};

class BlockInstance {
public:
  BlockInstance() = default;
  BlockInstance(NodeId instance_node_id, std::string definition_uri,
                ReferenceMode mode = ReferenceMode::Linked)
    : instance_node_id_(instance_node_id),
      definition_uri_(std::move(definition_uri)),
      reference_mode_(mode) {}

  [[nodiscard]] NodeId instance_node_id() const noexcept {
    return instance_node_id_;
  }
  void set_instance_node_id(NodeId id) noexcept {
    instance_node_id_ = id;
  }

  [[nodiscard]] const std::string& definition_uri() const noexcept {
    return definition_uri_;
  }
  void set_definition_uri(std::string uri) {
    definition_uri_ = std::move(uri);
  }

  [[nodiscard]] ReferenceMode reference_mode() const noexcept {
    return reference_mode_;
  }
  void set_reference_mode(ReferenceMode mode) noexcept {
    reference_mode_ = mode;
  }

  [[nodiscard]] const Transform2d& transform() const noexcept {
    return transform_;
  }
  void set_transform(Transform2d t) noexcept {
    transform_ = t;
  }

  [[nodiscard]] Rect2d bounds() const noexcept {
    return bounds_;
  }
  void set_bounds(Rect2d b) noexcept {
    bounds_ = b;
  }

  // Prototype inheritance: resolves property by checking local overrides first,
  // falling back to definition defaults.
  [[nodiscard]] std::string resolve_property(const std::string& key,
                                             const BlockDefinition& def) const {
    auto it = property_overrides_.find(key);
    if (it != property_overrides_.end()) {
      return it->second;
    }
    return def.get_default_property(key);
  }

  void set_property_override(std::string key, std::string val) {
    property_overrides_.insert_or_assign(std::move(key), std::move(val));
  }

  void clear_property_override(const std::string& key) {
    property_overrides_.erase(key);
  }

  [[nodiscard]] const std::unordered_map<std::string, std::string>& property_overrides()
      const noexcept {
    return property_overrides_;
  }

  // Generates boundary ports in parent GraphModel world space
  [[nodiscard]] std::vector<Port> compute_boundary_ports(const BlockDefinition& def) const {
    std::vector<Port> ports;
    ports.reserve(def.interface_ports().size());

    for (const auto& iport : def.interface_ports()) {
      Port p;
      p.id = iport.id;
      p.parent_node = instance_node_id_;
      p.name = iport.name;
      p.local_anchor = iport.local_anchor;
      p.direction = iport.boundary_side;
      p.pin_type = iport.pin_type;
      p.is_boundary_interface = true;
      ports.push_back(std::move(p));
    }
    return ports;
  }

  [[nodiscard]] Vec2d resolve_boundary_port_world_position(
      const InterfacePort& iport) const noexcept {
    return {bounds_.x + iport.local_anchor.x * bounds_.width,
            bounds_.y + iport.local_anchor.y * bounds_.height};
  }

  [[nodiscard]] BlockDefinition* embedded_definition() noexcept {
    return embedded_definition_.get();
  }

  [[nodiscard]] const BlockDefinition* embedded_definition() const noexcept {
    return embedded_definition_.get();
  }

  void set_embedded_definition(std::unique_ptr<BlockDefinition> def) {
    embedded_definition_ = std::move(def);
    reference_mode_ = ReferenceMode::Embedded;
  }

private:
  NodeId instance_node_id_{InvalidNodeId};
  std::string definition_uri_;
  ReferenceMode reference_mode_{ReferenceMode::Linked};
  Transform2d transform_{Transform2d::identity()};
  Rect2d bounds_{0.0, 0.0, 160.0, 100.0};
  std::unordered_map<std::string, std::string> property_overrides_;
  std::unique_ptr<BlockDefinition> embedded_definition_;
};

class BlockLibraryManager {
public:
  BlockLibraryManager() = default;

  bool register_definition(BlockDefinition def) {
    if (def.uri().empty())
      return false;
    const std::string uri = def.uri();
    definitions_.insert_or_assign(uri, std::move(def));
    return true;
  }

  bool unregister_definition(const std::string& uri) {
    return definitions_.erase(uri) > 0;
  }

  [[nodiscard]] BlockDefinition* find_definition(const std::string& uri) noexcept {
    auto it = definitions_.find(uri);
    return (it != definitions_.end()) ? &it->second : nullptr;
  }

  [[nodiscard]] const BlockDefinition* find_definition(const std::string& uri) const noexcept {
    auto it = definitions_.find(uri);
    return (it != definitions_.end()) ? &it->second : nullptr;
  }

  [[nodiscard]] const std::unordered_map<std::string, BlockDefinition>& definitions()
      const noexcept {
    return definitions_;
  }

  // Detects recursive circular instantiation: returns true if instantiating 'child_uri'
  // inside 'parent_uri' would introduce a dependency cycle.
  [[nodiscard]] bool detect_cycle(const std::string& parent_uri,
                                  const std::string& child_uri) const {
    if (parent_uri == child_uri)
      return true;

    std::unordered_set<std::string> visited;
    std::vector<std::string> stack = {child_uri};

    while (!stack.empty()) {
      std::string current = std::move(stack.back());
      stack.pop_back();

      if (current == parent_uri)
        return true;
      if (visited.contains(current))
        continue;
      visited.insert(current);

      const auto* def = find_definition(current);
      if (!def)
        continue;

      for (const auto& [nid, node] : def->internal_graph().nodes()) {
        auto prop_it = node.properties.find("definition_uri");
        if (prop_it != node.properties.end()) {
          stack.push_back(prop_it->second);
        }
      }
    }
    return false;
  }

private:
  std::unordered_map<std::string, BlockDefinition> definitions_;
};

struct ActiveScope {
  NodeId instance_node_id{InvalidNodeId};
  std::string definition_uri;
  std::string display_name;
  GraphModel* graph_model{nullptr};
};

class HierarchyContext {
public:
  explicit HierarchyContext(GraphModel* root_graph) {
    reset(root_graph);
  }

  void reset(GraphModel* root_graph) {
    scope_stack_.clear();
    scope_stack_.push_back(ActiveScope{.instance_node_id = InvalidNodeId,
                                       .definition_uri = "vector://root",
                                       .display_name = "Top",
                                       .graph_model = root_graph});
  }

  void push_scope(NodeId instance_node_id, std::string definition_uri, std::string display_name,
                  GraphModel* child_graph) {
    scope_stack_.push_back(ActiveScope{.instance_node_id = instance_node_id,
                                       .definition_uri = std::move(definition_uri),
                                       .display_name = std::move(display_name),
                                       .graph_model = child_graph});
  }

  bool pop_scope() {
    if (scope_stack_.size() > 1) {
      scope_stack_.pop_back();
      return true;
    }
    return false;
  }

  [[nodiscard]] size_t depth() const noexcept {
    return scope_stack_.size();
  }

  [[nodiscard]] GraphModel* current_graph() const noexcept {
    return scope_stack_.empty() ? nullptr : scope_stack_.back().graph_model;
  }

  [[nodiscard]] GraphModel* parent_graph() const noexcept {
    if (scope_stack_.size() >= 2) {
      return scope_stack_[scope_stack_.size() - 2].graph_model;
    }
    return nullptr;
  }

  [[nodiscard]] NodeId current_instance_node_id() const noexcept {
    return scope_stack_.empty() ? InvalidNodeId : scope_stack_.back().instance_node_id;
  }

  [[nodiscard]] const ActiveScope& current_scope() const {
    return scope_stack_.back();
  }

  [[nodiscard]] const ActiveScope& scope_at(size_t index) const {
    return scope_stack_.at(index);
  }

  [[nodiscard]] std::string breadcrumb_path() const {
    std::string path;
    for (size_t i = 0; i < scope_stack_.size(); ++i) {
      if (i > 0)
        path += " / ";
      path += scope_stack_[i].display_name;
    }
    return path;
  }

private:
  std::vector<ActiveScope> scope_stack_;
};

}  // namespace vector::core

#endif  // VECTOR_CORE_HIERARCHICAL_BLOCK_HPP
