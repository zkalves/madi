// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_CORE_PARAMETRIC_SHAPE_HPP
#define MADI_CORE_PARAMETRIC_SHAPE_HPP

#include "madi/core/GraphModel.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numbers>
#include <array>

namespace madi::core {

enum class ParametricShapeKind {
  None,
  Arrow,
  Trapezoid,
  Star,
  Cross,
  GenericPolygon
};

struct ParametricHandleInfo {
  int id;                     // Handle ID (0, 1, 2...)
  Vec2d normalized_pos;       // [0, 1] relative to bounds
  std::string description;    // Human-readable description
};

inline double distance_to_segment_sq(Vec2d p, Vec2d a, Vec2d b, Vec2d* out_proj = nullptr, double* out_t = nullptr) {
  const double l2 = a.distance_sq_to(b);
  if (l2 < 1e-9) {
    if (out_proj) *out_proj = a;
    if (out_t) *out_t = 0.0;
    return p.distance_sq_to(a);
  }
  const double t = std::clamp(((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / l2, 0.0, 1.0);
  const Vec2d proj = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
  if (out_proj) *out_proj = proj;
  if (out_t) *out_t = t;
  return p.distance_sq_to(proj);
}

inline double distance_to_segment(Vec2d p, Vec2d a, Vec2d b, Vec2d* out_proj = nullptr, double* out_t = nullptr) {
  return std::sqrt(distance_to_segment_sq(p, a, b, out_proj, out_t));
}

inline void ensure_shape_polygon_vertices(Node& node) {
  if (!node.polygon_vertices.empty()) return;
  if (node.shape == ShapeType::Rectangle || node.shape == ShapeType::RoundedRect ||
      node.shape == ShapeType::HierarchicalBlock || node.shape == ShapeType::CustomPolygon) {
    node.polygon_vertices = {
      {0.0, 0.0},
      {1.0, 0.0},
      {1.0, 1.0},
      {0.0, 1.0}
    };
    node.shape = ShapeType::CustomPolygon;
  } else if (node.shape == ShapeType::Ellipse) {
    node.polygon_vertices.reserve(12);
    for (int i = 0; i < 12; ++i) {
      const double rad = (2.0 * std::numbers::pi * i) / 12.0;
      node.polygon_vertices.push_back({0.5 + 0.5 * std::cos(rad), 0.5 + 0.5 * std::sin(rad)});
    }
    node.shape = ShapeType::CustomPolygon;
  }
}

inline ParametricShapeKind get_parametric_kind(const Node& node) {
  auto it = node.properties.find("parametric_type");
  if (it != node.properties.end()) {
    if (it->second == "arrow" && node.polygon_vertices.size() == 7) return ParametricShapeKind::Arrow;
    if (it->second == "trapezoid" && node.polygon_vertices.size() == 4) return ParametricShapeKind::Trapezoid;
    if (it->second == "star" && node.polygon_vertices.size() == 10) return ParametricShapeKind::Star;
    if (it->second == "cross" && node.polygon_vertices.size() == 12) return ParametricShapeKind::Cross;
    if (it->second == "custom_polygon" || !node.polygon_vertices.empty()) return ParametricShapeKind::GenericPolygon;
  }
  if (node.shape == ShapeType::CustomPolygon) {
    if ((node.name.find("Arrow") != std::string::npos ||
         node.name.find("Chevron") != std::string::npos ||
         (node.polygon_vertices.size() == 7 && std::abs(node.polygon_vertices[3].x - 1.0) < 1e-3)) &&
        node.polygon_vertices.size() == 7) {
      return ParametricShapeKind::Arrow;
    }
    if (node.name.find("Trapezoid") != std::string::npos && node.polygon_vertices.size() == 4) {
      return ParametricShapeKind::Trapezoid;
    }
    if (node.name.find("Star") != std::string::npos && node.polygon_vertices.size() == 10) {
      return ParametricShapeKind::Star;
    }
    if (node.name.find("Cross") != std::string::npos && node.polygon_vertices.size() == 12) {
      return ParametricShapeKind::Cross;
    }
    if (!node.polygon_vertices.empty()) {
      return ParametricShapeKind::GenericPolygon;
    }
  }
  return ParametricShapeKind::None;
}

inline std::vector<ParametricHandleInfo> get_parametric_handles(const Node& node) {
  std::vector<ParametricHandleInfo> handles;
  const auto kind = get_parametric_kind(node);

  if (kind == ParametricShapeKind::Arrow && node.polygon_vertices.size() == 7) {
    // 0: Top Shoulder / Shaft Junction
    handles.push_back({0, node.polygon_vertices[1], "Arrowhead Shoulder Point (Drag to adjust head length & shaft thickness; Alt for symmetric)"});
    // 1: Bottom Shoulder / Shaft Junction
    handles.push_back({1, node.polygon_vertices[5], "Bottom Shoulder Point (Drag to adjust head length & shaft thickness; Alt for symmetric)"});
    // 2: Top Wing Tip
    handles.push_back({2, node.polygon_vertices[2], "Arrowhead Top Wing (Drag to adjust head width/flare; Alt for symmetric)"});
    // 3: Bottom Wing Tip
    handles.push_back({3, node.polygon_vertices[4], "Arrowhead Bottom Wing (Drag to adjust head width/flare; Alt for symmetric)"});
    // 4: Tip
    handles.push_back({4, node.polygon_vertices[3], "Arrowhead Tip Point (Drag to adjust tip length)"});
  } else if (kind == ParametricShapeKind::Trapezoid && node.polygon_vertices.size() == 4) {
    handles.push_back({0, node.polygon_vertices[0], "Trapezoid Taper Point (Drag to adjust slant; Alt for symmetric)"});
  } else if (kind == ParametricShapeKind::Star && node.polygon_vertices.size() == 10) {
    handles.push_back({0, node.polygon_vertices[1], "Star Inner Radius Point (Drag to adjust pointiness)"});
  } else if (kind == ParametricShapeKind::Cross && node.polygon_vertices.size() == 12) {
    handles.push_back({0, node.polygon_vertices[1], "Cross Arm Thickness Point (Drag to adjust arm thickness)"});
  } else if (!node.polygon_vertices.empty()) {
    for (size_t i = 0; i < node.polygon_vertices.size(); ++i) {
      handles.push_back({static_cast<int>(i), node.polygon_vertices[i], "Control Point " + std::to_string(i + 1) + " (Drag to reshape)"});
    }
  }
  return handles;
}

inline int insert_polygon_vertex_at_normalized(Node& node, Vec2d norm_pt, double min_edge_margin = 0.02) {
  ensure_shape_polygon_vertices(node);
  if (node.polygon_vertices.size() < 2) {
    node.polygon_vertices.push_back(norm_pt);
    return static_cast<int>(node.polygon_vertices.size() - 1);
  }

  size_t best_edge = 0;
  double best_dist_sq = 1e9;
  Vec2d best_proj = norm_pt;

  for (size_t i = 0; i < node.polygon_vertices.size(); ++i) {
    size_t next_i = (i + 1) % node.polygon_vertices.size();
    const auto& pA = node.polygon_vertices[i];
    const auto& pB = node.polygon_vertices[next_i];
    Vec2d proj = pA;
    double t = 0.0;
    const double d2 = distance_to_segment_sq(norm_pt, pA, pB, &proj, &t);
    if (d2 < best_dist_sq) {
      best_dist_sq = d2;
      best_edge = i;
      const double clamped_t = std::clamp(t, min_edge_margin, 1.0 - min_edge_margin);
      best_proj = {pA.x + clamped_t * (pB.x - pA.x), pA.y + clamped_t * (pB.y - pA.y)};
    }
  }

  const int insert_idx = static_cast<int>(best_edge + 1);
  node.polygon_vertices.insert(node.polygon_vertices.begin() + insert_idx, best_proj);
  node.shape = ShapeType::CustomPolygon;
  node.properties["parametric_type"] = "custom_polygon";
  return insert_idx;
}

inline int insert_polygon_vertex_at_world(Node& node, Vec2d world_pt) {
  ensure_shape_polygon_vertices(node);
  Vec2d unrot = world_pt;
  if (std::abs(node.rotation_deg) > 1e-4) {
    const Vec2d c = node.bounds.center();
    const double rad = -node.rotation_deg * (std::numbers::pi / 180.0);
    const double cos_a = std::cos(rad);
    const double sin_a = std::sin(rad);
    const double dx = world_pt.x - c.x;
    const double dy = world_pt.y - c.y;
    unrot = {c.x + dx * cos_a - dy * sin_a, c.y + dx * sin_a + dy * cos_a};
  }

  const double bw = std::max(1.0, node.bounds.width);
  const double bh = std::max(1.0, node.bounds.height);
  const double nx = std::clamp((unrot.x - node.bounds.x) / bw, 0.0, 1.0);
  const double ny = std::clamp((unrot.y - node.bounds.y) / bh, 0.0, 1.0);
  return insert_polygon_vertex_at_normalized(node, {nx, ny});
}

inline bool remove_polygon_vertex(Node& node, int index) {
  if (node.polygon_vertices.size() <= 3) return false;
  if (index < 0 || index >= static_cast<int>(node.polygon_vertices.size())) return false;
  node.polygon_vertices.erase(node.polygon_vertices.begin() + index);
  node.properties["parametric_type"] = "custom_polygon";
  return true;
}

inline bool is_near_shape_edge(const Node& node, Vec2d world_pt, double tolerance_world) {
  Vec2d unrot = world_pt;
  if (std::abs(node.rotation_deg) > 1e-4) {
    const Vec2d c = node.bounds.center();
    const double rad = -node.rotation_deg * (std::numbers::pi / 180.0);
    const double cos_a = std::cos(rad);
    const double sin_a = std::sin(rad);
    const double dx = world_pt.x - c.x;
    const double dy = world_pt.y - c.y;
    unrot = {c.x + dx * cos_a - dy * sin_a, c.y + dx * sin_a + dy * cos_a};
  }

  const double bw = std::max(1.0, node.bounds.width);
  const double bh = std::max(1.0, node.bounds.height);

  if (!node.polygon_vertices.empty()) {
    const double tol_sq = tolerance_world * tolerance_world;
    for (size_t i = 0; i < node.polygon_vertices.size(); ++i) {
      size_t next_i = (i + 1) % node.polygon_vertices.size();
      const Vec2d pA = {node.bounds.x + node.polygon_vertices[i].x * bw,
                        node.bounds.y + node.polygon_vertices[i].y * bh};
      const Vec2d pB = {node.bounds.x + node.polygon_vertices[next_i].x * bw,
                        node.bounds.y + node.polygon_vertices[next_i].y * bh};
      if (distance_to_segment_sq(unrot, pA, pB) <= tol_sq) {
        return true;
      }
    }
    return false;
  }

  const double x0 = node.bounds.x;
  const double y0 = node.bounds.y;
  const double x1 = node.bounds.right();
  const double y1 = node.bounds.bottom();

  const std::array<std::pair<Vec2d, Vec2d>, 4> edges = {{
    {{x0, y0}, {x1, y0}},
    {{x1, y0}, {x1, y1}},
    {{x1, y1}, {x0, y1}},
    {{x0, y1}, {x0, y0}}
  }};

  const double tol_sq = tolerance_world * tolerance_world;
  for (const auto& [eA, eB] : edges) {
    if (distance_to_segment_sq(unrot, eA, eB) <= tol_sq) {
      return true;
    }
  }
  return false;
}

} // namespace madi::core

#endif // MADI_CORE_PARAMETRIC_SHAPE_HPP
