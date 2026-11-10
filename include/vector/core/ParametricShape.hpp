// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_CORE_PARAMETRIC_SHAPE_HPP
#define VECTOR_CORE_PARAMETRIC_SHAPE_HPP

#include "vector/core/GraphModel.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numbers>

namespace vector::core {

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

inline ParametricShapeKind get_parametric_kind(const Node& node) {
  auto it = node.properties.find("parametric_type");
  if (it != node.properties.end()) {
    if (it->second == "arrow") return ParametricShapeKind::Arrow;
    if (it->second == "trapezoid") return ParametricShapeKind::Trapezoid;
    if (it->second == "star") return ParametricShapeKind::Star;
    if (it->second == "cross") return ParametricShapeKind::Cross;
  }
  if (node.shape == ShapeType::CustomPolygon) {
    if (node.name.find("Arrow") != std::string::npos ||
        node.name.find("Chevron") != std::string::npos ||
        (node.polygon_vertices.size() == 7 && std::abs(node.polygon_vertices[3].x - 1.0) < 1e-3)) {
      return ParametricShapeKind::Arrow;
    }
    if (node.name.find("Trapezoid") != std::string::npos) {
      return ParametricShapeKind::Trapezoid;
    }
    if (node.name.find("Star") != std::string::npos || node.polygon_vertices.size() == 10) {
      if (node.name.find("Star") != std::string::npos) return ParametricShapeKind::Star;
    }
    if (node.name.find("Cross") != std::string::npos || node.polygon_vertices.size() == 12) {
      if (node.name.find("Cross") != std::string::npos) return ParametricShapeKind::Cross;
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
  } else if (kind == ParametricShapeKind::GenericPolygon && !node.polygon_vertices.empty()) {
    for (size_t i = 0; i < node.polygon_vertices.size(); ++i) {
      handles.push_back({static_cast<int>(i), node.polygon_vertices[i], "Polygon Vertex " + std::to_string(i + 1) + " (Drag to reshape)"});
    }
  }
  return handles;
}

} // namespace vector::core

#endif // VECTOR_CORE_PARAMETRIC_SHAPE_HPP