// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_CORE_COMMAND_HPP
#define MADI_CORE_COMMAND_HPP

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Geometry.hpp"
#include "GraphModel.hpp"

namespace madi::core {

class ICommand {
public:
  virtual ~ICommand() = default;

  virtual bool execute() = 0;
  virtual bool undo() = 0;
  virtual bool redo() {
    return execute();
  }

  [[nodiscard]] virtual std::string description() const = 0;
  [[nodiscard]] virtual bool can_merge_with(const ICommand* other) const noexcept {
    (void)other;
    return false;
  }
  virtual bool merge_with(const ICommand* other) {
    (void)other;
    return false;
  }
};

class CompositeCommand : public ICommand {
public:
  explicit CompositeCommand(std::string desc = "Composite Operation")
    : description_(std::move(desc)) {}

  void add_command(std::unique_ptr<ICommand> cmd) {
    if (cmd) {
      commands_.push_back(std::move(cmd));
    }
  }

  [[nodiscard]] size_t size() const noexcept {
    return commands_.size();
  }
  [[nodiscard]] bool empty() const noexcept {
    return commands_.empty();
  }

  bool execute() override {
    for (auto& cmd : commands_) {
      if (!cmd->execute()) {
        return false;
      }
    }
    return true;
  }

  bool undo() override {
    // Undo in reverse order
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
      if (!(*it)->undo()) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] std::string description() const override {
    return description_;
  }

private:
  std::string description_;
  std::vector<std::unique_ptr<ICommand>> commands_;
};

class AddNodeCommand : public ICommand {
public:
  AddNodeCommand(GraphModel& model, Node node)
    : model_(model), node_(std::move(node)), created_id_(node_.id) {}

  bool execute() override {
    created_id_ = model_.add_node(node_);
    return created_id_ != InvalidNodeId;
  }

  bool undo() override {
    return model_.remove_node(created_id_);
  }

  [[nodiscard]] NodeId created_id() const noexcept {
    return created_id_;
  }

  [[nodiscard]] std::string description() const override {
    return "Add Node: " + node_.name;
  }

private:
  GraphModel& model_;
  Node node_;
  NodeId created_id_{InvalidNodeId};
};

class MoveNodesCommand : public ICommand {
public:
  MoveNodesCommand(GraphModel& model, std::vector<NodeId> node_ids, Vec2d delta)
    : model_(model), node_ids_(std::move(node_ids)), total_delta_(delta) {}

  bool execute() override {
    return apply_delta(total_delta_);
  }

  bool undo() override {
    return apply_delta(-total_delta_);
  }

  [[nodiscard]] std::string description() const override {
    return "Move Nodes (" + std::to_string(node_ids_.size()) + ")";
  }

  [[nodiscard]] bool can_merge_with(const ICommand* other) const noexcept override {
    const auto* move_cmd = dynamic_cast<const MoveNodesCommand*>(other);
    if (!move_cmd || &model_ != &move_cmd->model_)
      return false;
    return node_ids_ == move_cmd->node_ids_;
  }

  bool merge_with(const ICommand* other) override {
    const auto* move_cmd = dynamic_cast<const MoveNodesCommand*>(other);
    if (!can_merge_with(move_cmd))
      return false;
    total_delta_ += move_cmd->total_delta_;
    return true;
  }

private:
  bool apply_delta(Vec2d d) {
    bool ok = true;
    for (NodeId id : node_ids_) {
      if (auto* node = model_.find_node(id)) {
        node->bounds.x += d.x;
        node->bounds.y += d.y;
      } else {
        ok = false;
      }
    }
    model_.update_all_edge_routes();
    return ok;
  }

  GraphModel& model_;
  std::vector<NodeId> node_ids_;
  Vec2d total_delta_;
};

class ResizeNodeCommand : public ICommand {
public:
  ResizeNodeCommand(GraphModel& model, NodeId node_id, Rect2d old_bounds, Rect2d new_bounds)
    : model_(model), node_id_(node_id), old_bounds_(old_bounds), new_bounds_(new_bounds) {}

  bool execute() override {
    if (auto* n = model_.find_node(node_id_)) {
      n->bounds = new_bounds_;
      model_.update_all_edge_routes();
      return true;
    }
    return false;
  }

  bool undo() override {
    if (auto* n = model_.find_node(node_id_)) {
      n->bounds = old_bounds_;
      model_.update_all_edge_routes();
      return true;
    }
    return false;
  }

  [[nodiscard]] std::string description() const override {
    return "Resize Shape";
  }

private:
  GraphModel& model_;
  NodeId node_id_{InvalidNodeId};
  Rect2d old_bounds_;
  Rect2d new_bounds_;
};

class RotateNodeCommand : public ICommand {
public:
  RotateNodeCommand(GraphModel& model, NodeId node_id, double old_rotation_deg, double new_rotation_deg)
    : model_(model), node_id_(node_id), old_rotation_deg_(old_rotation_deg), new_rotation_deg_(new_rotation_deg) {}

  bool execute() override {
    if (auto* n = model_.find_node(node_id_)) {
      n->rotation_deg = new_rotation_deg_;
      model_.update_all_edge_routes();
      return true;
    }
    return false;
  }

  bool undo() override {
    if (auto* n = model_.find_node(node_id_)) {
      n->rotation_deg = old_rotation_deg_;
      model_.update_all_edge_routes();
      return true;
    }
    return false;
  }

  [[nodiscard]] std::string description() const override {
    return "Rotate Shape";
  }

private:
  GraphModel& model_;
  NodeId node_id_{InvalidNodeId};
  double old_rotation_deg_{0.0};
  double new_rotation_deg_{0.0};
};

class RotateNodesCommand : public ICommand {
public:
  RotateNodesCommand(GraphModel& model,
                     std::vector<std::pair<NodeId, double>> old_rotations,
                     std::vector<std::pair<NodeId, double>> new_rotations,
                     std::string description = "Rotate Shapes")
    : model_(model),
      old_rotations_(std::move(old_rotations)),
      new_rotations_(std::move(new_rotations)),
      description_(std::move(description)) {}

  bool execute() override {
    return apply_rotations(new_rotations_);
  }

  bool undo() override {
    return apply_rotations(old_rotations_);
  }

  [[nodiscard]] std::string description() const override {
    return description_;
  }

private:
  bool apply_rotations(const std::vector<std::pair<NodeId, double>>& rots) {
    bool ok = true;
    for (const auto& [id, deg] : rots) {
      if (auto* node = model_.find_node(id)) {
        node->rotation_deg = deg;
      } else {
        ok = false;
      }
    }
    model_.update_all_edge_routes();
    return ok;
  }

  GraphModel& model_;
  std::vector<std::pair<NodeId, double>> old_rotations_;
  std::vector<std::pair<NodeId, double>> new_rotations_;
  std::string description_;
};

class ModifyShapeGeometryCommand : public ICommand {
public:
  ModifyShapeGeometryCommand(
      GraphModel& model,
      NodeId node_id,
      std::vector<Vec2d> old_vertices,
      std::vector<Vec2d> new_vertices,
      std::unordered_map<std::string, std::string> old_props,
      std::unordered_map<std::string, std::string> new_props,
      std::string description = "Modify Shape Geometry")
    : model_(model),
      node_id_(node_id),
      old_vertices_(std::move(old_vertices)),
      new_vertices_(std::move(new_vertices)),
      old_props_(std::move(old_props)),
      new_props_(std::move(new_props)),
      description_(std::move(description)) {}

  bool execute() override {
    if (auto* n = model_.find_node(node_id_)) {
      n->polygon_vertices = new_vertices_;
      for (const auto& [k, v] : new_props_) {
        n->properties[k] = v;
      }
      model_.update_all_edge_routes();
      return true;
    }
    return false;
  }

  bool undo() override {
    if (auto* n = model_.find_node(node_id_)) {
      n->polygon_vertices = old_vertices_;
      for (const auto& [k, v] : old_props_) {
        n->properties[k] = v;
      }
      model_.update_all_edge_routes();
      return true;
    }
    return false;
  }

  [[nodiscard]] std::string description() const override {
    return description_;
  }

private:
  GraphModel& model_;
  NodeId node_id_{InvalidNodeId};
  std::vector<Vec2d> old_vertices_;
  std::vector<Vec2d> new_vertices_;
  std::unordered_map<std::string, std::string> old_props_;
  std::unordered_map<std::string, std::string> new_props_;
  std::string description_;
};

class ChangeZOrderCommand : public ICommand {
public:
  ChangeZOrderCommand(GraphModel& model,
                      std::vector<std::pair<NodeId, int32_t>> old_z_orders,
                      std::vector<std::pair<NodeId, int32_t>> new_z_orders,
                      std::string description = "Change Stacking Order")
    : model_(model),
      old_z_orders_(std::move(old_z_orders)),
      new_z_orders_(std::move(new_z_orders)),
      description_(std::move(description)) {}

  bool execute() override {
    return apply_z_orders(new_z_orders_);
  }

  bool undo() override {
    return apply_z_orders(old_z_orders_);
  }

  [[nodiscard]] std::string description() const override {
    return description_;
  }

private:
  bool apply_z_orders(const std::vector<std::pair<NodeId, int32_t>>& z_orders) {
    bool ok = true;
    for (const auto& [id, z] : z_orders) {
      if (auto* node = model_.find_node(id)) {
        node->z_order = z;
      } else {
        ok = false;
      }
    }
    return ok;
  }

  GraphModel& model_;
  std::vector<std::pair<NodeId, int32_t>> old_z_orders_;
  std::vector<std::pair<NodeId, int32_t>> new_z_orders_;
  std::string description_;
};

class ConnectPortsCommand : public ICommand {
public:
  ConnectPortsCommand(GraphModel& model, Edge edge)
    : model_(model), edge_(std::move(edge)), created_id_(edge_.id) {}

  bool execute() override {
    created_id_ = model_.add_edge(edge_);
    return created_id_ != InvalidEdgeId;
  }

  bool undo() override {
    return model_.remove_edge(created_id_);
  }

  [[nodiscard]] EdgeId created_id() const noexcept {
    return created_id_;
  }

  [[nodiscard]] std::string description() const override {
    return "Connect Ports";
  }

private:
  GraphModel& model_;
  Edge edge_;
  EdgeId created_id_{InvalidEdgeId};
};

class DeleteElementsCommand : public ICommand {
public:
  DeleteElementsCommand(GraphModel& model,
                        std::vector<NodeId> node_ids,
                        std::vector<EdgeId> edge_ids = {})
    : model_(model), node_ids_(std::move(node_ids)), edge_ids_(std::move(edge_ids)) {
    // 1. Snapshot nodes and their ports
    for (NodeId nid : node_ids_) {
      if (const auto* n = model_.find_node(nid)) {
        saved_nodes_.push_back(*n);
        for (PortId pid : n->port_ids) {
          if (const auto* p = model_.find_port(pid)) {
            saved_ports_.push_back(*p);
          }
        }
      }
    }
    // 2. Snapshot explicitly deleted edges only
    for (EdgeId eid : edge_ids_) {
      if (const auto* e = model_.find_edge(eid)) {
        saved_edges_.push_back(*e);
      }
    }
    // 3. Snapshot incident edges that will be detached (NOT deleted)
    for (const auto& [eid, edge] : model_.edges()) {
      if (std::find(edge_ids_.begin(), edge_ids_.end(), eid) == edge_ids_.end()) {
        bool incident = false;
        for (NodeId nid : node_ids_) {
          if (edge.source_node == nid || edge.target_node == nid) {
            incident = true;
            break;
          }
          for (const auto& s : edge.extra_sources) {
            if (s.source_node == nid) { incident = true; break; }
          }
          for (const auto& d : edge.extra_destinations) {
            if (d.target_node == nid) { incident = true; break; }
          }
          if (incident) break;
        }
        if (incident) {
          detached_edges_before_.push_back(edge);
        }
      }
    }
  }

  bool execute() override {
    for (const auto& edge : saved_edges_) {
      model_.remove_edge(edge.id);
    }
    for (NodeId nid : node_ids_) {
      model_.remove_node(nid);
    }
    model_.update_all_edge_routes();
    return true;
  }

  bool undo() override {
    for (const auto& node : saved_nodes_) {
      model_.add_node(node);
    }
    for (const auto& port : saved_ports_) {
      model_.add_port(port);
    }
    for (const auto& edge : saved_edges_) {
      model_.add_edge(edge);
    }
    for (const auto& edge : detached_edges_before_) {
      if (auto* e = model_.find_edge(edge.id)) {
        *e = edge;
      }
    }
    model_.update_all_edge_routes();
    return true;
  }

  [[nodiscard]] std::string description() const override {
    return "Delete Elements (" + std::to_string(saved_nodes_.size()) + " nodes, " +
           std::to_string(saved_edges_.size()) + " edges)";
  }

private:
  GraphModel& model_;
  std::vector<NodeId> node_ids_;
  std::vector<EdgeId> edge_ids_;
  std::vector<Node> saved_nodes_;
  std::vector<Port> saved_ports_;
  std::vector<Edge> saved_edges_;
  std::vector<Edge> detached_edges_before_;
};

class MoveEdgeCommand : public ICommand {
public:
  MoveEdgeCommand(GraphModel& model, EdgeId edge_id, Edge old_state, Edge new_state)
    : model_(model), edge_id_(edge_id), old_state_(std::move(old_state)), new_state_(std::move(new_state)) {}

  bool execute() override {
    if (auto* e = model_.find_edge(edge_id_)) {
      *e = new_state_;
      return true;
    }
    return false;
  }

  bool undo() override {
    if (auto* e = model_.find_edge(edge_id_)) {
      *e = old_state_;
      return true;
    }
    return false;
  }

  [[nodiscard]] std::string description() const override {
    return "Relocate Arrow Net " + std::to_string(edge_id_);
  }

private:
  GraphModel& model_;
  EdgeId edge_id_;
  Edge old_state_;
  Edge new_state_;
};

class PasteElementsCommand : public ICommand {
public:
  PasteElementsCommand(GraphModel& model,
                       std::vector<Node> template_nodes,
                       std::vector<Port> template_ports,
                       std::vector<Edge> template_edges,
                       Vec2d offset = {20.0, 20.0})
    : model_(model),
      template_nodes_(std::move(template_nodes)),
      template_ports_(std::move(template_ports)),
      template_edges_(std::move(template_edges)),
      offset_(offset) {}

  bool execute() override {
    created_nodes_.clear();
    created_ports_.clear();
    created_edges_.clear();
    std::unordered_map<NodeId, NodeId> node_map;
    std::unordered_map<PortId, PortId> port_map;

    // 1. Add cloned nodes
    for (const auto& tn : template_nodes_) {
      Node n = tn;
      n.id = InvalidNodeId;
      n.bounds.x += offset_.x;
      n.bounds.y += offset_.y;
      n.port_ids.clear();
      NodeId new_id = model_.add_node(std::move(n));
      if (new_id == InvalidNodeId) return false;
      node_map[tn.id] = new_id;
      created_nodes_.push_back(new_id);
    }

    // 2. Add cloned ports
    for (const auto& tp : template_ports_) {
      auto it = node_map.find(tp.parent_node);
      if (it == node_map.end()) continue;
      Port p = tp;
      p.id = InvalidPortId;
      p.parent_node = it->second;
      PortId new_pid = model_.add_port(std::move(p));
      if (new_pid == InvalidPortId) return false;
      port_map[tp.id] = new_pid;
      created_ports_.push_back(new_pid);
    }

    // 3. Add cloned edges
    for (const auto& te : template_edges_) {
      auto it_src = node_map.find(te.source_node);
      auto it_tgt = node_map.find(te.target_node);
      if (it_src == node_map.end() || it_tgt == node_map.end()) continue;

      Edge e = te;
      e.id = InvalidEdgeId;
      e.source_node = it_src->second;
      e.target_node = it_tgt->second;
      if (te.source_port != InvalidPortId) {
        auto it_sp = port_map.find(te.source_port);
        e.source_port = (it_sp != port_map.end()) ? it_sp->second : InvalidPortId;
      }
      if (te.target_port != InvalidPortId) {
        auto it_tp = port_map.find(te.target_port);
        e.target_port = (it_tp != port_map.end()) ? it_tp->second : InvalidPortId;
      }
      for (auto& wp : e.waypoints) {
        wp.x += offset_.x;
        wp.y += offset_.y;
      }
      EdgeId new_eid = model_.add_edge(std::move(e));
      if (new_eid != InvalidEdgeId) {
        created_edges_.push_back(new_eid);
      }
    }

    model_.update_all_edge_routes();
    return true;
  }

  bool undo() override {
    for (EdgeId eid : created_edges_) {
      model_.remove_edge(eid);
    }
    for (PortId pid : created_ports_) {
      model_.remove_port(pid);
    }
    for (NodeId nid : created_nodes_) {
      model_.remove_node(nid);
    }
    model_.update_all_edge_routes();
    return true;
  }

  [[nodiscard]] const std::vector<NodeId>& created_nodes() const noexcept {
    return created_nodes_;
  }

  [[nodiscard]] const std::vector<EdgeId>& created_edges() const noexcept {
    return created_edges_;
  }

  [[nodiscard]] std::string description() const override {
    return "Paste Elements (" + std::to_string(created_nodes_.size()) + " nodes)";
  }

private:
  GraphModel& model_;
  std::vector<Node> template_nodes_;
  std::vector<Port> template_ports_;
  std::vector<Edge> template_edges_;
  Vec2d offset_;
  std::vector<NodeId> created_nodes_;
  std::vector<PortId> created_ports_;
  std::vector<EdgeId> created_edges_;
};

class CommandHistory {
public:
  explicit CommandHistory(size_t max_depth = 100) : max_depth_(max_depth) {}

  bool execute(std::unique_ptr<ICommand> cmd) {
    if (!cmd)
      return false;

    if (!cmd->execute()) {
      return false;
    }

    if (in_transaction_) {
      active_transaction_->add_command(std::move(cmd));
      return true;
    }

    // Try coalescing / merging with preceding command
    if (!undo_stack_.empty() && undo_stack_.back()->can_merge_with(cmd.get())) {
      undo_stack_.back()->merge_with(cmd.get());
    } else {
      undo_stack_.push_back(std::move(cmd));
      if (undo_stack_.size() > max_depth_) {
        undo_stack_.pop_front();
      }
    }

    redo_stack_.clear();
    notify_change();
    return true;
  }

  bool undo() {
    if (undo_stack_.empty() || in_transaction_) {
      return false;
    }

    auto cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    if (!cmd->undo()) {
      return false;
    }

    redo_stack_.push_back(std::move(cmd));
    notify_change();
    return true;
  }

  bool redo() {
    if (redo_stack_.empty() || in_transaction_) {
      return false;
    }

    auto cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    if (!cmd->redo()) {
      return false;
    }

    undo_stack_.push_back(std::move(cmd));
    notify_change();
    return true;
  }

  void begin_transaction(std::string name = "Transaction") {
    if (!in_transaction_) {
      active_transaction_ = std::make_unique<CompositeCommand>(std::move(name));
      in_transaction_ = true;
    }
  }

  bool commit() {
    if (!in_transaction_ || !active_transaction_) {
      return false;
    }

    in_transaction_ = false;
    if (!active_transaction_->empty()) {
      undo_stack_.push_back(std::move(active_transaction_));
      if (undo_stack_.size() > max_depth_) {
        undo_stack_.pop_front();
      }
      redo_stack_.clear();
      notify_change();
    }
    active_transaction_.reset();
    return true;
  }

  bool rollback() {
    if (!in_transaction_ || !active_transaction_) {
      return false;
    }

    in_transaction_ = false;
    const bool success = active_transaction_->undo();
    active_transaction_.reset();
    notify_change();
    return success;
  }

  [[nodiscard]] bool can_undo() const noexcept {
    return !undo_stack_.empty() && !in_transaction_;
  }

  [[nodiscard]] bool can_redo() const noexcept {
    return !redo_stack_.empty() && !in_transaction_;
  }

  [[nodiscard]] size_t undo_count() const noexcept {
    return undo_stack_.size();
  }
  [[nodiscard]] size_t redo_count() const noexcept {
    return redo_stack_.size();
  }

  void clear() {
    undo_stack_.clear();
    redo_stack_.clear();
    in_transaction_ = false;
    active_transaction_.reset();
    notify_change();
  }

  void set_on_history_changed(std::function<void()> cb) {
    on_history_changed_ = std::move(cb);
  }

private:
  void notify_change() {
    if (on_history_changed_) {
      on_history_changed_();
    }
  }

  size_t max_depth_{100};
  bool in_transaction_{false};
  std::unique_ptr<CompositeCommand> active_transaction_;
  std::deque<std::unique_ptr<ICommand>> undo_stack_;
  std::vector<std::unique_ptr<ICommand>> redo_stack_;
  std::function<void()> on_history_changed_;
};

}  // namespace madi::core

#endif // MADI_CORE_COMMAND_HPP
