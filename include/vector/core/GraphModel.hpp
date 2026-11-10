// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_CORE_GRAPH_MODEL_HPP
#define VECTOR_CORE_GRAPH_MODEL_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Geometry.hpp"

namespace vector::core {

using NodeId = uint64_t;
using PortId = uint64_t;
using EdgeId = uint64_t;

inline constexpr NodeId InvalidNodeId = 0;
inline constexpr PortId InvalidPortId = 0;
inline constexpr EdgeId InvalidEdgeId = 0;

enum class PortDirection : uint8_t { North, East, South, West, Omnidirectional };

inline Vec2d direction_to_vector(PortDirection dir) noexcept {
  switch (dir) {
    case PortDirection::North:
      return {0.0, -1.0};
    case PortDirection::East:
      return {1.0, 0.0};
    case PortDirection::South:
      return {0.0, 1.0};
    case PortDirection::West:
      return {-1.0, 0.0};
    case PortDirection::Omnidirectional:
      return {0.0, 0.0};
  }
  return {0.0, 0.0};
}

struct Port {
  PortId id{InvalidPortId};
  NodeId parent_node{InvalidNodeId};
  std::string name;
  Vec2d local_anchor{0.0, 0.0};  // Normalized offset [0..1] or relative coords
  PortDirection direction{PortDirection::Omnidirectional};
  std::string pin_type{"generic"};
  bool is_boundary_interface{false};

  constexpr bool operator==(const Port& other) const noexcept {
    return id == other.id && parent_node == other.parent_node;
  }
};

enum class ShapeType : uint8_t {
  Rectangle,
  RoundedRect,
  Ellipse,
  HierarchicalBlock,
  CustomPolygon,
  InputPort,
  OutputPort
};

struct Node {
  NodeId id{InvalidNodeId};
  std::string name;
  Rect2d bounds{0.0, 0.0, 100.0, 60.0};
  double rotation_deg{0.0}; // Angle in degrees [0, 360)
  ShapeType shape{ShapeType::Rectangle};
  std::vector<Vec2d> polygon_vertices; // Local normalized [0..1] points
  std::vector<std::vector<Vec2d>> subpath_polygons; // Multiple contours/holes in normalized [0..1] coords
  std::unordered_map<std::string, std::string> properties;
  std::vector<PortId> port_ids;
  int32_t z_order{0};
  bool visible{true};
  bool selected{false};

  [[nodiscard]] Rect2d bounding_box() const noexcept {
    if (std::abs(rotation_deg) < 1e-4) {
      return bounds;
    }
    const Vec2d c = bounds.center();
    const double rad = rotation_deg * (std::numbers::pi / 180.0);
    const double cos_a = std::abs(std::cos(rad));
    const double sin_a = std::abs(std::sin(rad));
    const double hw = 0.5 * bounds.width;
    const double hh = 0.5 * bounds.height;
    const double new_hw = hw * cos_a + hh * sin_a;
    const double new_hh = hw * sin_a + hh * cos_a;
    return Rect2d{c.x - new_hw, c.y - new_hh, new_hw * 2.0, new_hh * 2.0};
  }

  [[nodiscard]] bool contains_point(Vec2d pt) const noexcept {
    if (std::abs(rotation_deg) < 1e-4) {
      return bounds.contains(pt);
    }
    const Vec2d c = bounds.center();
    const double rad = -rotation_deg * (std::numbers::pi / 180.0);
    const double cos_a = std::cos(rad);
    const double sin_a = std::sin(rad);
    const double dx = pt.x - c.x;
    const double dy = pt.y - c.y;
    const Vec2d local_pt = {c.x + dx * cos_a - dy * sin_a, c.y + dx * sin_a + dy * cos_a};
    return bounds.contains(local_pt);
  }

  void set_rotation(double deg) noexcept {
    rotation_deg = std::fmod(deg, 360.0);
    if (rotation_deg < 0.0) rotation_deg += 360.0;
  }

  [[nodiscard]] double rotation() const noexcept {
    return rotation_deg;
  }

  [[nodiscard]] std::vector<Vec2d> get_world_polygon_vertices() const {
    if (polygon_vertices.empty()) {
      return {
        {bounds.x + 0.25 * bounds.width, bounds.y},
        {bounds.x + 0.75 * bounds.width, bounds.y},
        {bounds.x + bounds.width, bounds.y + 0.5 * bounds.height},
        {bounds.x + 0.75 * bounds.width, bounds.y + bounds.height},
        {bounds.x + 0.25 * bounds.width, bounds.y + bounds.height},
        {bounds.x, bounds.y + 0.5 * bounds.height}
      };
    }
    std::vector<Vec2d> pts;
    pts.reserve(polygon_vertices.size());
    for (const auto& v : polygon_vertices) {
      pts.push_back({bounds.x + v.x * bounds.width, bounds.y + v.y * bounds.height});
    }
    return pts;
  }

  [[nodiscard]] std::vector<std::vector<Vec2d>> get_world_subpath_polygons() const {
    if (!subpath_polygons.empty()) {
      std::vector<std::vector<Vec2d>> result;
      result.reserve(subpath_polygons.size());
      for (const auto& sub : subpath_polygons) {
        std::vector<Vec2d> pts;
        pts.reserve(sub.size());
        for (const auto& v : sub) {
          pts.push_back({bounds.x + v.x * bounds.width, bounds.y + v.y * bounds.height});
        }
        result.push_back(std::move(pts));
      }
      return result;
    }
    auto it_sub = properties.find("subpaths");
    if (it_sub != properties.end() && !it_sub->second.empty()) {
      std::vector<std::vector<Vec2d>> result;
      std::stringstream ss_subs(it_sub->second);
      std::string contour_str;
      while (std::getline(ss_subs, contour_str, '|')) {
        if (contour_str.empty()) continue;
        std::stringstream ss_c(contour_str);
        std::string token;
        std::vector<Vec2d> pts;
        while (std::getline(ss_c, token, ';')) {
          if (token.empty()) continue;
          auto comma = token.find(',');
          if (comma != std::string::npos) {
            double nx = std::stod(token.substr(0, comma));
            double ny = std::stod(token.substr(comma + 1));
            pts.push_back({bounds.x + nx * bounds.width, bounds.y + ny * bounds.height});
          }
        }
        if (!pts.empty()) {
          result.push_back(std::move(pts));
        }
      }
      if (!result.empty()) return result;
    }
    return {get_world_polygon_vertices()};
  }

  void set_regular_polygon(int sides) {
    shape = ShapeType::CustomPolygon;
    polygon_vertices.clear();
    if (sides < 3) sides = 3;
    polygon_vertices.reserve(sides);
    for (int i = 0; i < sides; ++i) {
      const double angle = -std::numbers::pi / 2.0 + (2.0 * std::numbers::pi * i) / sides;
      const double nx = 0.5 + 0.5 * std::cos(angle);
      const double ny = 0.5 + 0.5 * std::sin(angle);
      polygon_vertices.push_back({nx, ny});
    }
  }

  [[nodiscard]] Vec2d resolve_port_position(const Port& port) const noexcept {
    const Vec2d unrotated = {bounds.x + port.local_anchor.x * bounds.width,
                            bounds.y + port.local_anchor.y * bounds.height};
    if (std::abs(rotation_deg) < 1e-4) return unrotated;
    const Vec2d center = bounds.center();
    const double rad = rotation_deg * (std::numbers::pi / 180.0);
    const double cos_a = std::cos(rad);
    const double sin_a = std::sin(rad);
    const double dx = unrotated.x - center.x;
    const double dy = unrotated.y - center.y;
    return {center.x + dx * cos_a - dy * sin_a, center.y + dx * sin_a + dy * cos_a};
  }

  [[nodiscard]] bool is_input_port() const noexcept {
    if (shape == ShapeType::InputPort) return true;
    auto it = properties.find("interface_kind");
    return it != properties.end() && it->second == "input";
  }

  [[nodiscard]] bool is_output_port() const noexcept {
    if (shape == ShapeType::OutputPort) return true;
    auto it = properties.find("interface_kind");
    return it != properties.end() && it->second == "output";
  }

  [[nodiscard]] bool is_interface_port() const noexcept {
    return is_input_port() || is_output_port();
  }

  [[nodiscard]] bool is_subsystem() const noexcept {
    return shape == ShapeType::HierarchicalBlock || properties.count("subsystem_type") > 0;
  }

  [[nodiscard]] bool is_file_subsystem() const noexcept {
    auto it = properties.find("subsystem_type");
    return it != properties.end() && it->second == "file";
  }

  [[nodiscard]] std::string subsystem_file() const {
    auto it = properties.find("subsystem_file");
    return (it != properties.end()) ? it->second : std::string{};
  }

  void set_subsystem_builtin() {
    shape = ShapeType::HierarchicalBlock;
    properties["subsystem_type"] = "builtin";
  }

  void set_subsystem_file(const std::string& path) {
    shape = ShapeType::HierarchicalBlock;
    properties["subsystem_type"] = "file";
    properties["subsystem_file"] = path;
  }

  [[nodiscard]] Vec2d symbol_anchor() const noexcept {
    auto it_x = properties.find("symbol_anchor_x");
    auto it_y = properties.find("symbol_anchor_y");
    if (it_x != properties.end() && it_y != properties.end()) {
      try {
        return {std::stod(it_x->second), std::stod(it_y->second)};
      } catch (...) {}
    }
    return {0.0, 0.0};
  }

  void set_symbol_anchor(Vec2d pt) {
    properties["symbol_anchor_x"] = std::to_string(std::clamp(pt.x, 0.0, 1.0));
    properties["symbol_anchor_y"] = std::to_string(std::clamp(pt.y, 0.0, 1.0));
  }

  [[nodiscard]] Vec2d symbol_resize_point() const noexcept {
    auto it_x = properties.find("symbol_resize_x");
    auto it_y = properties.find("symbol_resize_y");
    if (it_x != properties.end() && it_y != properties.end()) {
      try {
        return {std::stod(it_x->second), std::stod(it_y->second)};
      } catch (...) {}
    }
    return {1.0, 1.0};
  }

  void set_symbol_resize_point(Vec2d pt) {
    properties["symbol_resize_x"] = std::to_string(std::clamp(pt.x, 0.0, 1.0));
    properties["symbol_resize_y"] = std::to_string(std::clamp(pt.y, 0.0, 1.0));
  }
};

enum class RoutingStyle : uint8_t { Orthogonal, Direct, CurvedBezier };
enum class BranchAlignment : uint8_t { Aligned, Independent };

struct BranchTarget {
  NodeId target_node{InvalidNodeId};
  PortId target_port{InvalidPortId};
  std::vector<Vec2d> waypoints;
  std::string arrowhead{"stealth"};
};

struct Edge {
  EdgeId id{InvalidEdgeId};
  std::string name;
  NodeId source_node{InvalidNodeId};
  PortId source_port{InvalidPortId};
  NodeId target_node{InvalidNodeId};
  PortId target_port{InvalidPortId};
  std::vector<Vec2d> waypoints;
  RoutingStyle routing_style{RoutingStyle::Orthogonal};
  std::unordered_map<std::string, std::string> properties;
  bool visible{true};
  bool selected{false};
  std::string arrowhead{"stealth"};
  std::vector<BranchTarget> extra_destinations;
  BranchAlignment branch_alignment{BranchAlignment::Aligned};
  bool branches_aligned{true};

  [[nodiscard]] bool is_multipoint() const noexcept {
    return !extra_destinations.empty();
  }

  [[nodiscard]] bool is_branches_aligned() const noexcept {
    auto it = properties.find("aligned");
    if (it != properties.end()) {
      return (it->second != "false" && it->second != "0" && it->second != "independent");
    }
    return branches_aligned && (branch_alignment == BranchAlignment::Aligned);
  }

  void set_branches_aligned(bool aligned) {
    branches_aligned = aligned;
    branch_alignment = aligned ? BranchAlignment::Aligned : BranchAlignment::Independent;
    properties["aligned"] = aligned ? "true" : "false";
  }

  void add_destination(NodeId node_id, PortId port_id, std::string arrow = "stealth") {
    extra_destinations.push_back(BranchTarget{
      .target_node = node_id,
      .target_port = port_id,
      .waypoints = {},
      .arrowhead = std::move(arrow)
    });
  }

  [[nodiscard]] Rect2d bounding_box() const noexcept {
    if (waypoints.empty() && extra_destinations.empty())
      return {};
    double min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9;
    for (const auto& wp : waypoints) {
      min_x = std::min(min_x, wp.x); max_x = std::max(max_x, wp.x);
      min_y = std::min(min_y, wp.y); max_y = std::max(max_y, wp.y);
    }
    for (const auto& branch : extra_destinations) {
      for (const auto& wp : branch.waypoints) {
        min_x = std::min(min_x, wp.x); max_x = std::max(max_x, wp.x);
        min_y = std::min(min_y, wp.y); max_y = std::max(max_y, wp.y);
      }
    }
    if (min_x > max_x) return {};
    return Rect2d{min_x, min_y, max_x - min_x, max_y - min_y}.expanded(6.0);
  }
};

class IGraphModelListener {
public:
  virtual ~IGraphModelListener() = default;
  virtual void on_node_added(const Node& node) = 0;
  virtual void on_node_removed(NodeId id) = 0;
  virtual void on_node_modified(const Node& node) = 0;
  virtual void on_edge_added(const Edge& edge) = 0;
  virtual void on_edge_removed(EdgeId id) = 0;
  virtual void on_edge_modified(const Edge& edge) = 0;
  virtual void on_port_added(const Port& port) = 0;
  virtual void on_port_removed(PortId id) = 0;
  virtual void on_model_cleared() = 0;
};

class GraphModel {
public:
  GraphModel() = default;
  ~GraphModel() = default;

  GraphModel(const GraphModel&) = default;
  GraphModel& operator=(const GraphModel&) = default;
  GraphModel(GraphModel&&) noexcept = default;
  GraphModel& operator=(GraphModel&&) noexcept = default;

  NodeId add_node(Node node) {
    if (node.id == InvalidNodeId) {
      node.id = next_node_id_++;
    } else if (node.id >= next_node_id_) {
      next_node_id_ = node.id + 1;
    }
    if (node.z_order == 0 && !nodes_.empty()) {
      node.z_order = get_max_z_order() + 1;
    }
    const NodeId id = node.id;
    auto [it, inserted] = nodes_.insert_or_assign(id, std::move(node));
    notify_listeners([&](IGraphModelListener* l) { l->on_node_added(it->second); });
    return id;
  }

  bool remove_node(NodeId id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
      return false;
    }

    // Cascade delete associated ports
    for (PortId pid : it->second.port_ids) {
      ports_.erase(pid);
      notify_listeners([pid](IGraphModelListener* l) { l->on_port_removed(pid); });
    }

    // Cascade delete connected edges
    std::vector<EdgeId> edges_to_remove;
    for (const auto& [eid, edge] : edges_) {
      if (edge.source_node == id || edge.target_node == id) {
        edges_to_remove.push_back(eid);
      }
    }
    for (EdgeId eid : edges_to_remove) {
      edges_.erase(eid);
      notify_listeners([eid](IGraphModelListener* l) { l->on_edge_removed(eid); });
    }

    nodes_.erase(it);
    notify_listeners([id](IGraphModelListener* l) { l->on_node_removed(id); });
    return true;
  }

  [[nodiscard]] Node* find_node(NodeId id) noexcept {
    auto it = nodes_.find(id);
    return (it != nodes_.end()) ? &it->second : nullptr;
  }

  [[nodiscard]] const Node* find_node(NodeId id) const noexcept {
    auto it = nodes_.find(id);
    return (it != nodes_.end()) ? &it->second : nullptr;
  }

  [[nodiscard]] Node* find_node_by_name(std::string_view name) noexcept {
    for (auto& [id, n] : nodes_) {
      if (n.name == name) return &n;
    }
    return nullptr;
  }

  [[nodiscard]] const Node* find_node_by_name(std::string_view name) const noexcept {
    for (const auto& [id, n] : nodes_) {
      if (n.name == name) return &n;
    }
    return nullptr;
  }

  PortId add_port(Port port) {
    if (port.id == InvalidPortId) {
      port.id = next_port_id_++;
    } else if (port.id >= next_port_id_) {
      next_port_id_ = port.id + 1;
    }
    const PortId id = port.id;
    const NodeId parent_id = port.parent_node;

    auto [it, inserted] = ports_.insert_or_assign(id, std::move(port));

    if (auto* parent = find_node(parent_id)) {
      if (std::find(parent->port_ids.begin(), parent->port_ids.end(), id) == parent->port_ids.end()) {
        parent->port_ids.push_back(id);
      }
    }

    notify_listeners([&](IGraphModelListener* l) { l->on_port_added(it->second); });
    return id;
  }

  bool remove_port(PortId id) {
    auto it = ports_.find(id);
    if (it == ports_.end()) {
      return false;
    }
    const NodeId parent_id = it->second.parent_node;
    if (auto* parent = find_node(parent_id)) {
      std::erase(parent->port_ids, id);
    }

    // Disconnect edges attached to this port
    std::vector<EdgeId> edges_to_remove;
    for (const auto& [eid, edge] : edges_) {
      if (edge.source_port == id || edge.target_port == id) {
        edges_to_remove.push_back(eid);
      }
    }
    for (EdgeId eid : edges_to_remove) {
      edges_.erase(eid);
      notify_listeners([eid](IGraphModelListener* l) { l->on_edge_removed(eid); });
    }

    ports_.erase(it);
    notify_listeners([id](IGraphModelListener* l) { l->on_port_removed(id); });
    return true;
  }

  [[nodiscard]] Port* find_port(PortId id) noexcept {
    auto it = ports_.find(id);
    return (it != ports_.end()) ? &it->second : nullptr;
  }

  [[nodiscard]] const Port* find_port(PortId id) const noexcept {
    auto it = ports_.find(id);
    return (it != ports_.end()) ? &it->second : nullptr;
  }

  EdgeId add_edge(Edge edge) {
    if (edge.id == InvalidEdgeId) {
      edge.id = next_edge_id_++;
    } else if (edge.id >= next_edge_id_) {
      next_edge_id_ = edge.id + 1;
    }
    const EdgeId id = edge.id;
    update_edge_route(edge);
    auto [it, inserted] = edges_.insert_or_assign(id, std::move(edge));
    notify_listeners([&](IGraphModelListener* l) { l->on_edge_added(it->second); });
    return id;
  }

  [[nodiscard]] std::vector<Vec2d> compute_orthogonal_path(
      Vec2d p_src, Vec2d p_dst,
      NodeId src_node_id = InvalidNodeId, NodeId dst_node_id = InvalidNodeId) const {
    return compute_orthogonal_path(p_src, PortDirection::Omnidirectional,
                                   p_dst, PortDirection::Omnidirectional,
                                   src_node_id, dst_node_id);
  }

  [[nodiscard]] std::vector<Vec2d> compute_orthogonal_path(
      Vec2d p_src, PortDirection dir_src,
      Vec2d p_dst, PortDirection dir_dst,
      NodeId src_node_id = InvalidNodeId, NodeId dst_node_id = InvalidNodeId) const {
    const double stub = 24.0;
    const Vec2d v_src = direction_to_vector(dir_src);
    const Vec2d v_dst = direction_to_vector(dir_dst);

    const Vec2d s_stub =
        (dir_src != PortDirection::Omnidirectional) ? (p_src + v_src * stub) : p_src;
    const Vec2d d_stub =
        (dir_dst != PortDirection::Omnidirectional) ? (p_dst + v_dst * stub) : p_dst;

    std::vector<Vec2d> wps;
    wps.push_back(p_src);
    if (s_stub != p_src)
      wps.push_back(s_stub);

    // Obstacle avoidance check
    const Node* hit_obstacle = nullptr;
    const double padding = 15.0;
    for (const auto& [nid, node] : nodes_) {
      if (nid == src_node_id || nid == dst_node_id || !node.visible) continue;
      const Rect2d ob = node.bounds.expanded(padding);
      const double min_x = std::min(s_stub.x, d_stub.x);
      const double max_x = std::max(s_stub.x, d_stub.x);
      const double min_y = std::min(s_stub.y, d_stub.y);
      const double max_y = std::max(s_stub.y, d_stub.y);
      if (max_x >= ob.x && min_x <= ob.right() && max_y >= ob.y && min_y <= ob.bottom()) {
        hit_obstacle = &node;
        break;
      }
    }

    if (hit_obstacle) {
      const Rect2d ob = hit_obstacle->bounds.expanded(padding);
      const bool route_horiz = (std::abs(p_src.x - p_dst.x) >= std::abs(p_src.y - p_dst.y));
      if (route_horiz) {
        const double bypass_y = (s_stub.y <= ob.center().y) ? (ob.top() - 10.0) : (ob.bottom() + 10.0);
        wps.push_back({s_stub.x, bypass_y});
        wps.push_back({d_stub.x, bypass_y});
      } else {
        const double bypass_x = (s_stub.x <= ob.center().x) ? (ob.left() - 10.0) : (ob.right() + 10.0);
        wps.push_back({bypass_x, s_stub.y});
        wps.push_back({bypass_x, d_stub.y});
      }
    } else if (dir_src == PortDirection::East || dir_src == PortDirection::West) {
      if (dir_dst == PortDirection::East || dir_dst == PortDirection::West) {
        if ((dir_src == PortDirection::East && p_src.x < p_dst.x) ||
            (dir_src == PortDirection::West && p_src.x > p_dst.x)) {
          const double mid_x = (s_stub.x + d_stub.x) * 0.5;
          wps.push_back({mid_x, s_stub.y});
          wps.push_back({mid_x, d_stub.y});
        } else {
          const auto* src_node = find_node(src_node_id);
          const auto* dst_node = find_node(dst_node_id);
          const double src_top = src_node ? src_node->bounds.top() : s_stub.y;
          const double dst_top = dst_node ? dst_node->bounds.top() : d_stub.y;
          const double src_bot = src_node ? src_node->bounds.bottom() : s_stub.y;
          const double dst_bot = dst_node ? dst_node->bounds.bottom() : d_stub.y;
          const double route_y = (s_stub.y < d_stub.y)
                                     ? (std::min(src_top, dst_top) - 30.0)
                                     : (std::max(src_bot, dst_bot) + 30.0);
          wps.push_back({s_stub.x, route_y});
          wps.push_back({d_stub.x, route_y});
        }
      } else {
        wps.push_back({d_stub.x, s_stub.y});
      }
    } else if (dir_src == PortDirection::North || dir_src == PortDirection::South) {
      if (dir_dst == PortDirection::North || dir_dst == PortDirection::South) {
        if ((dir_src == PortDirection::South && p_src.y < p_dst.y) ||
            (dir_src == PortDirection::North && p_src.y > p_dst.y)) {
          const double mid_y = (s_stub.y + d_stub.y) * 0.5;
          wps.push_back({s_stub.x, mid_y});
          wps.push_back({d_stub.x, mid_y});
        } else {
          const auto* src_node = find_node(src_node_id);
          const auto* dst_node = find_node(dst_node_id);
          const double src_left = src_node ? src_node->bounds.left() : s_stub.x;
          const double dst_left = dst_node ? dst_node->bounds.left() : d_stub.x;
          const double src_right = src_node ? src_node->bounds.right() : s_stub.x;
          const double dst_right = dst_node ? dst_node->bounds.right() : d_stub.x;
          const double route_x = (s_stub.x < d_stub.x)
                                     ? (std::min(src_left, dst_left) - 30.0)
                                     : (std::max(src_right, dst_right) + 30.0);
          wps.push_back({route_x, s_stub.y});
          wps.push_back({route_x, d_stub.y});
        }
      } else {
        wps.push_back({s_stub.x, d_stub.y});
      }
    } else {
      const double mid_x = (s_stub.x + d_stub.x) * 0.5;
      wps.push_back({mid_x, s_stub.y});
      wps.push_back({mid_x, d_stub.y});
    }

    if (d_stub != p_dst)
      wps.push_back(d_stub);
    wps.push_back(p_dst);

    std::vector<Vec2d> simplified;
    for (size_t i = 0; i < wps.size(); ++i) {
      if (simplified.size() >= 2) {
        const auto& p_prev2 = simplified[simplified.size() - 2];
        const auto& p_prev1 = simplified.back();
        const auto& p_curr = wps[i];
        if ((std::abs(p_prev2.x - p_prev1.x) < 1e-4 && std::abs(p_prev1.x - p_curr.x) < 1e-4) ||
            (std::abs(p_prev2.y - p_prev1.y) < 1e-4 && std::abs(p_prev1.y - p_curr.y) < 1e-4)) {
          simplified.back() = p_curr;
          continue;
        }
      }
      simplified.push_back(wps[i]);
    }
    return simplified;
  }

  void update_edge_route(Edge& edge) {
    if (edge.source_node == InvalidNodeId || edge.target_node == InvalidNodeId) {
      return;
    }
    const auto* src_node = find_node(edge.source_node);
    const auto* dst_node = find_node(edge.target_node);
    if (!src_node || !dst_node)
      return;

    const Port* src_port = find_port(edge.source_port);
    const Port* dst_port = find_port(edge.target_port);

    if (!src_port && !src_node->port_ids.empty()) {
      double min_d = 1e18;
      const Vec2d target_center = dst_node->bounds.center();
      for (PortId pid : src_node->port_ids) {
        if (const auto* p = find_port(pid)) {
          double d = src_node->resolve_port_position(*p).distance_sq_to(target_center);
          if (d < min_d) {
            min_d = d;
            src_port = p;
            edge.source_port = p->id;
          }
        }
      }
    }

    if (!dst_port && !dst_node->port_ids.empty()) {
      double min_d = 1e18;
      const Vec2d source_pos =
          src_port ? src_node->resolve_port_position(*src_port) : src_node->bounds.center();
      for (PortId pid : dst_node->port_ids) {
        if (const auto* p = find_port(pid)) {
          double d = dst_node->resolve_port_position(*p).distance_sq_to(source_pos);
          if (d < min_d) {
            min_d = d;
            dst_port = p;
            edge.target_port = p->id;
          }
        }
      }
    }

    if (!src_port || !dst_port)
      return;

    const Vec2d p_src = src_node->resolve_port_position(*src_port);
    const Vec2d p_dst = dst_node->resolve_port_position(*dst_port);

    if (edge.routing_style == RoutingStyle::Direct) {
      edge.waypoints = {p_src, p_dst};
    } else {
      edge.waypoints = compute_orthogonal_path(
          p_src, src_port->direction, p_dst, dst_port->direction, edge.source_node, edge.target_node);
    }

    // Auto-route multipoint extra destinations branching from the primary trunk
    if (edge.is_branches_aligned() && !edge.extra_destinations.empty()) {
      int horiz_count = 0;
      int vert_count = 0;
      if (dst_port->direction == PortDirection::East || dst_port->direction == PortDirection::West) horiz_count++;
      else if (dst_port->direction == PortDirection::North || dst_port->direction == PortDirection::South) vert_count++;

      for (const auto& br : edge.extra_destinations) {
        if (const auto* bp = find_port(br.target_port)) {
          if (bp->direction == PortDirection::East || bp->direction == PortDirection::West) horiz_count++;
          else if (bp->direction == PortDirection::North || bp->direction == PortDirection::South) vert_count++;
        }
      }

      if (horiz_count >= vert_count) {
        // Horizontal entry into destinations: Common Vertical Bus Spine at spine_x
        double spine_x = (p_src.x + p_dst.x) * 0.5;
        for (size_t i = 0; i + 1 < edge.waypoints.size(); ++i) {
          if (std::abs(edge.waypoints[i].x - edge.waypoints[i + 1].x) < 1e-3 &&
              std::abs(edge.waypoints[i].y - edge.waypoints[i + 1].y) > 2.0) {
            spine_x = edge.waypoints[i].x;
            break;
          }
        }

        double trunk_ref_y = p_dst.y;
        for (const auto& wp : edge.waypoints) {
          if (std::abs(wp.x - spine_x) < 1e-3) {
            trunk_ref_y = wp.y;
            break;
          }
        }

        for (auto& branch : edge.extra_destinations) {
          if (branch.target_node == InvalidNodeId) continue;
          const auto* b_node = find_node(branch.target_node);
          if (!b_node) continue;
          const Port* b_port = find_port(branch.target_port);
          if (!b_port && !b_node->port_ids.empty()) {
            b_port = find_port(b_node->port_ids.front());
            branch.target_port = b_port ? b_port->id : InvalidPortId;
          }
          if (!b_port) continue;
          const Vec2d p_branch_dst = b_node->resolve_port_position(*b_port);

          std::vector<Vec2d> wps;
          wps.push_back({spine_x, trunk_ref_y});
          if (std::abs(trunk_ref_y - p_branch_dst.y) > 1e-3) {
            wps.push_back({spine_x, p_branch_dst.y});
          }
          wps.push_back(p_branch_dst);

          std::vector<Vec2d> clean_wps;
          for (const auto& pt : wps) {
            if (clean_wps.empty() || clean_wps.back() != pt) clean_wps.push_back(pt);
          }
          branch.waypoints = std::move(clean_wps);
        }
      } else {
        // Vertical entry into destinations: Common Horizontal Bus Spine at spine_y
        double spine_y = (p_src.y + p_dst.y) * 0.5;
        for (size_t i = 0; i + 1 < edge.waypoints.size(); ++i) {
          if (std::abs(edge.waypoints[i].y - edge.waypoints[i + 1].y) < 1e-3 &&
              std::abs(edge.waypoints[i].x - edge.waypoints[i + 1].x) > 2.0) {
            spine_y = edge.waypoints[i].y;
            break;
          }
        }

        double trunk_ref_x = p_dst.x;
        for (const auto& wp : edge.waypoints) {
          if (std::abs(wp.y - spine_y) < 1e-3) {
            trunk_ref_x = wp.x;
            break;
          }
        }

        for (auto& branch : edge.extra_destinations) {
          if (branch.target_node == InvalidNodeId) continue;
          const auto* b_node = find_node(branch.target_node);
          if (!b_node) continue;
          const Port* b_port = find_port(branch.target_port);
          if (!b_port && !b_node->port_ids.empty()) {
            b_port = find_port(b_node->port_ids.front());
            branch.target_port = b_port ? b_port->id : InvalidPortId;
          }
          if (!b_port) continue;
          const Vec2d p_branch_dst = b_node->resolve_port_position(*b_port);

          std::vector<Vec2d> wps;
          wps.push_back({trunk_ref_x, spine_y});
          if (std::abs(trunk_ref_x - p_branch_dst.x) > 1e-3) {
            wps.push_back({p_branch_dst.x, spine_y});
          }
          wps.push_back(p_branch_dst);

          std::vector<Vec2d> clean_wps;
          for (const auto& pt : wps) {
            if (clean_wps.empty() || clean_wps.back() != pt) clean_wps.push_back(pt);
          }
          branch.waypoints = std::move(clean_wps);
        }
      }
    } else {
      // Independent routing: each branch calculates closest point projection
      for (auto& branch : edge.extra_destinations) {
        if (branch.target_node == InvalidNodeId) continue;
        const auto* b_node = find_node(branch.target_node);
        if (!b_node) continue;
        const Port* b_port = find_port(branch.target_port);
        if (!b_port && !b_node->port_ids.empty()) {
          b_port = find_port(b_node->port_ids.front());
          branch.target_port = b_port ? b_port->id : InvalidPortId;
        }
        if (!b_port) continue;
        const Vec2d p_branch_dst = b_node->resolve_port_position(*b_port);

        // Find the closest point along edge.waypoints to branch from
        Vec2d best_tap = edge.waypoints.empty() ? p_src : edge.waypoints.front();
        double min_d_sq = 1e18;
        for (size_t i = 0; i + 1 < edge.waypoints.size(); ++i) {
          const auto& p1 = edge.waypoints[i];
          const auto& p2 = edge.waypoints[i + 1];
          const Vec2d seg = p2 - p1;
          const double len_sq = seg.length_sq();
          Vec2d proj = p1;
          if (len_sq > 1e-6) {
            const double t = std::clamp((p_branch_dst - p1).dot(seg) / len_sq, 0.1, 0.9);
            proj = p1 + seg * t;
          }
          const double d_sq = proj.distance_sq_to(p_branch_dst);
          if (d_sq < min_d_sq) {
            min_d_sq = d_sq;
            best_tap = proj;
          }
        }

        branch.waypoints = compute_orthogonal_path(
            best_tap, PortDirection::Omnidirectional, p_branch_dst, b_port->direction, edge.source_node, branch.target_node);
        if (branch.waypoints.empty() || branch.waypoints.front() != best_tap) {
          branch.waypoints.insert(branch.waypoints.begin(), best_tap);
        }
      }
    }
  }

  void update_all_edge_routes() {
    for (auto& [eid, edge] : edges_) {
      update_edge_route(edge);
    }
  }

  bool remove_edge(EdgeId id) {
    auto it = edges_.find(id);
    if (it == edges_.end()) {
      return false;
    }
    edges_.erase(it);
    notify_listeners([id](IGraphModelListener* l) { l->on_edge_removed(id); });
    return true;
  }

  [[nodiscard]] Edge* find_edge(EdgeId id) noexcept {
    auto it = edges_.find(id);
    return (it != edges_.end()) ? &it->second : nullptr;
  }

  [[nodiscard]] const Edge* find_edge(EdgeId id) const noexcept {
    auto it = edges_.find(id);
    return (it != edges_.end()) ? &it->second : nullptr;
  }

  [[nodiscard]] const std::unordered_map<NodeId, Node>& nodes() const noexcept {
    return nodes_;
  }

  [[nodiscard]] const std::unordered_map<PortId, Port>& ports() const noexcept {
    return ports_;
  }

  [[nodiscard]] const std::unordered_map<EdgeId, Edge>& edges() const noexcept {
    return edges_;
  }

  [[nodiscard]] size_t node_count() const noexcept {
    return nodes_.size();
  }
  [[nodiscard]] size_t port_count() const noexcept {
    return ports_.size();
  }
  [[nodiscard]] size_t edge_count() const noexcept {
    return edges_.size();
  }

  [[nodiscard]] int32_t get_max_z_order() const noexcept {
    int32_t max_z = 0;
    for (const auto& [id, node] : nodes_) {
      if (node.z_order > max_z) {
        max_z = node.z_order;
      }
    }
    return max_z;
  }

  [[nodiscard]] int32_t get_min_z_order() const noexcept {
    if (nodes_.empty()) return 0;
    int32_t min_z = nodes_.begin()->second.z_order;
    for (const auto& [id, node] : nodes_) {
      if (node.z_order < min_z) {
        min_z = node.z_order;
      }
    }
    return min_z;
  }

  void clear() {
    nodes_.clear();
    ports_.clear();
    edges_.clear();
    notify_listeners([](IGraphModelListener* l) { l->on_model_cleared(); });
  }

  [[nodiscard]] Rect2d get_bounding_box() const noexcept {
    if (nodes_.empty() && edges_.empty()) {
      return {};
    }
    Rect2d bbox{};
    for (const auto& [id, node] : nodes_) {
      if (node.visible) {
        bbox = bbox.united(node.bounding_box());
      }
    }
    for (const auto& [id, edge] : edges_) {
      if (edge.visible) {
        bbox = bbox.united(edge.bounding_box());
      }
    }
    return bbox;
  }

  void add_listener(IGraphModelListener* listener) {
    if (listener) {
      listeners_.push_back(listener);
    }
  }

  void remove_listener(IGraphModelListener* listener) {
    std::erase(listeners_, listener);
  }

private:
  void notify_listeners(const std::function<void(IGraphModelListener*)>& fn) {
    for (auto* listener : listeners_) {
      if (listener) {
        fn(listener);
      }
    }
  }

  NodeId next_node_id_{1};
  PortId next_port_id_{1};
  EdgeId next_edge_id_{1};

  std::unordered_map<NodeId, Node> nodes_;
  std::unordered_map<PortId, Port> ports_;
  std::unordered_map<EdgeId, Edge> edges_;
  std::vector<IGraphModelListener*> listeners_;
};

}  // namespace vector::core

#endif  // VECTOR_CORE_GRAPH_MODEL_HPP
