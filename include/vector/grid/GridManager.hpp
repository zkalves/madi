// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_GRID_GRID_MANAGER_HPP
#define VECTOR_GRID_GRID_MANAGER_HPP

#include "../core/Geometry.hpp"
#include "../core/GraphModel.hpp"
#include "../core/SpatialIndex.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace vector::grid {

enum class SnapFlags : uint32_t {
  None                    = 0,
  Grid                    = 1 << 0,
  ShapeBounds             = 1 << 1,
  ShapeCenter             = 1 << 2,
  InterfacePort           = 1 << 3,
  ConnectorOrthogonal     = 1 << 4,
  All                     = 0x1F
};

enum class SnapStrength : uint8_t {
  Off,
  Weak,
  Normal,
  Strong,
  Custom
};

enum class SnapType : uint8_t {
  Rectangular,
  Isometric,
  Polar
};

enum class GridSpacingMode : uint8_t {
  Variable,
  Fixed
};

inline const char* to_string(SnapStrength s) noexcept {
  switch (s) {
    case SnapStrength::Off: return "Off";
    case SnapStrength::Weak: return "Weak (3px)";
    case SnapStrength::Normal: return "Normal (6px)";
    case SnapStrength::Strong: return "Strong (12px)";
    case SnapStrength::Custom: return "Custom";
  }
  return "Unknown";
}

inline const char* to_string(SnapType t) noexcept {
  switch (t) {
    case SnapType::Rectangular: return "Grid (Rectangular)";
    case SnapType::Isometric: return "Grid (Isometric 30°)";
    case SnapType::Polar: return "Polar (Angle Tracking)";
  }
  return "Rectangular";
}

inline const char* to_string(GridSpacingMode m) noexcept {
  switch (m) {
    case GridSpacingMode::Variable: return "Variable (Zoom-adaptive)";
    case GridSpacingMode::Fixed: return "Fixed (Custom dimensions)";
  }
  return "Variable";
}

constexpr SnapFlags operator|(SnapFlags a, SnapFlags b) noexcept {
  return static_cast<SnapFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr SnapFlags operator&(SnapFlags a, SnapFlags b) noexcept {
  return static_cast<SnapFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool has_flag(SnapFlags mask, SnapFlags flag) noexcept {
  return (mask & flag) == flag;
}

struct GridConfig {
  double major_spacing_x{20.0};
  double major_spacing_y{20.0};
  uint32_t subdivisions_x{4};
  uint32_t subdivisions_y{4};
  double snap_spacing_x{5.0};
  double snap_spacing_y{5.0};
  bool match_grid_to_snap{false};
  GridSpacingMode spacing_mode{GridSpacingMode::Variable};
  SnapType snap_type{SnapType::Rectangular};
  double polar_angle_deg{45.0};
  bool visible{true};
  bool show_subdivisions{true};

  // Backwards compatibility legacy fields
  double major_spacing{20.0};
  uint32_t subdivisions{4};

  [[nodiscard]] double minor_spacing_x() const noexcept {
    if (match_grid_to_snap) return snap_spacing_x;
    return (subdivisions_x > 0) ? (major_spacing_x / static_cast<double>(subdivisions_x)) : major_spacing_x;
  }

  [[nodiscard]] double minor_spacing_y() const noexcept {
    if (match_grid_to_snap) return snap_spacing_y;
    return (subdivisions_y > 0) ? (major_spacing_y / static_cast<double>(subdivisions_y)) : major_spacing_y;
  }

  [[nodiscard]] double minor_spacing() const noexcept {
    return minor_spacing_x();
  }

  void sync_legacy() noexcept {
    major_spacing = major_spacing_x;
    subdivisions = subdivisions_x;
  }
};

struct SnapGuideLine {
  core::Vec2d p1;
  core::Vec2d p2;
  bool is_vertical{false};
};

struct SnapResult {
  core::Vec2d snapped_point;
  bool snapped{false};
  SnapFlags triggered_flag{SnapFlags::None};
  uint64_t target_entity_id{0};
  uint64_t target_port_id{0};
  std::vector<SnapGuideLine> guide_lines;
};

class GridManager {
public:
  explicit GridManager(GridConfig config = {}) : config_(config) {}

  [[nodiscard]] const GridConfig& config() const noexcept { return config_; }
  void set_config(const GridConfig& cfg) noexcept {
    config_ = cfg;
    config_.sync_legacy();
  }

  void set_snap_spacing(double x, double y) noexcept {
    config_.snap_spacing_x = std::max(1e-4, x);
    config_.snap_spacing_y = std::max(1e-4, y);
  }
  [[nodiscard]] double snap_spacing_x() const noexcept { return config_.snap_spacing_x; }
  [[nodiscard]] double snap_spacing_y() const noexcept { return config_.snap_spacing_y; }

  void set_snap_type(SnapType t) noexcept { config_.snap_type = t; }
  [[nodiscard]] SnapType snap_type() const noexcept { return config_.snap_type; }

  void set_grid_spacing_mode(GridSpacingMode m) noexcept { config_.spacing_mode = m; }
  [[nodiscard]] GridSpacingMode grid_spacing_mode() const noexcept { return config_.spacing_mode; }

  void set_match_grid_to_snap(bool match) noexcept { config_.match_grid_to_snap = match; }
  [[nodiscard]] bool match_grid_to_snap() const noexcept { return config_.match_grid_to_snap; }

  void set_polar_angle_deg(double deg) noexcept { config_.polar_angle_deg = std::max(1.0, deg); }
  [[nodiscard]] double polar_angle_deg() const noexcept { return config_.polar_angle_deg; }

  [[nodiscard]] SnapFlags snap_flags() const noexcept { return snap_flags_; }
  void set_snap_flags(SnapFlags flags) noexcept { snap_flags_ = flags; }
  void enable_flag(SnapFlags flag) noexcept { snap_flags_ = snap_flags_ | flag; }
  void disable_flag(SnapFlags flag) noexcept {
    snap_flags_ = static_cast<SnapFlags>(static_cast<uint32_t>(snap_flags_) & ~static_cast<uint32_t>(flag));
  }

  [[nodiscard]] SnapStrength snap_strength() const noexcept { return snap_strength_; }
  void set_snap_strength(SnapStrength strength) noexcept {
    snap_strength_ = strength;
    switch (strength) {
      case SnapStrength::Off:
        snap_tolerance_ = 0.0;
        break;
      case SnapStrength::Weak:
        snap_tolerance_ = 3.0;
        break;
      case SnapStrength::Normal:
        snap_tolerance_ = 6.0;
        break;
      case SnapStrength::Strong:
        snap_tolerance_ = 12.0;
        break;
      case SnapStrength::Custom:
        break;
    }
  }

  [[nodiscard]] bool is_snap_enabled() const noexcept {
    return snap_tolerance_ > 1e-4 && snap_flags_ != SnapFlags::None;
  }

  void set_snap_enabled(bool enabled) noexcept {
    if (enabled) {
      if (snap_tolerance_ <= 1e-4) {
        set_snap_strength(SnapStrength::Normal);
      }
      if (snap_flags_ == SnapFlags::None) {
        snap_flags_ = SnapFlags::All;
      }
    } else {
      set_snap_strength(SnapStrength::Off);
    }
  }

  [[nodiscard]] double snap_tolerance() const noexcept { return snap_tolerance_; }
  void set_snap_tolerance(double tol) noexcept {
    snap_tolerance_ = std::max(0.0, tol);
    if (std::abs(snap_tolerance_) < 1e-4) {
      snap_strength_ = SnapStrength::Off;
    } else if (std::abs(snap_tolerance_ - 3.0) < 1e-4) {
      snap_strength_ = SnapStrength::Weak;
    } else if (std::abs(snap_tolerance_ - 6.0) < 1e-4) {
      snap_strength_ = SnapStrength::Normal;
    } else if (std::abs(snap_tolerance_ - 12.0) < 1e-4) {
      snap_strength_ = SnapStrength::Strong;
    } else {
      snap_strength_ = SnapStrength::Custom;
    }
  }

  // Snaps world coordinates strictly to the grid
  [[nodiscard]] core::Vec2d snap_to_grid(core::Vec2d world_pos) const noexcept {
    if (config_.snap_type == SnapType::Isometric) {
      const double step = config_.snap_spacing_x;
      if (step <= 1e-6) return world_pos;
      constexpr double cos30 = 0.8660254037844386; // sqrt(3)/2
      constexpr double sin30 = 0.5;
      const double u = world_pos.x * cos30 + world_pos.y * sin30;
      const double v = -world_pos.x * cos30 + world_pos.y * sin30;
      const double su = std::round(u / step) * step;
      const double sv = std::round(v / step) * step;
      const double x = (su - sv) / (2.0 * cos30);
      const double y = (su + sv) / (2.0 * sin30);
      return {x, y};
    } else if (config_.snap_type == SnapType::Polar) {
      const double rad_inc = config_.polar_angle_deg * (M_PI / 180.0);
      if (rad_inc <= 1e-6) return world_pos;
      const double r = std::hypot(world_pos.x, world_pos.y);
      const double theta = std::atan2(world_pos.y, world_pos.x);
      const double snapped_theta = std::round(theta / rad_inc) * rad_inc;
      const double step_r = config_.snap_spacing_x;
      const double snapped_r = (step_r > 1e-6) ? (std::round(r / step_r) * step_r) : r;
      return {snapped_r * std::cos(snapped_theta), snapped_r * std::sin(snapped_theta)};
    } else {
      const double step_x = config_.snap_spacing_x;
      const double step_y = config_.snap_spacing_y;
      const double sx = (step_x > 1e-6) ? (std::round(world_pos.x / step_x) * step_x) : world_pos.x;
      const double sy = (step_y > 1e-6) ? (std::round(world_pos.y / step_y) * step_y) : world_pos.y;
      return {sx, sy};
    }
  }

  // Snaps an angle in degrees with cardinal axis priority
  [[nodiscard]] double snap_angle(double raw_angle_deg, double step_deg = 15.0, double cardinal_tolerance_deg = 4.0) const noexcept {
    double a = std::fmod(raw_angle_deg, 360.0);
    if (a < 0.0) a += 360.0;

    // Cardinal angles (0, 90, 180, 270) priority snap
    const double cardinals[] = {0.0, 90.0, 180.0, 270.0, 360.0};
    for (double c : cardinals) {
      if (std::abs(a - c) <= cardinal_tolerance_deg || std::abs(a - (c - 360.0)) <= cardinal_tolerance_deg) {
        return (c >= 360.0) ? 0.0 : c;
      }
    }

    if (step_deg > 1e-4) {
      double snapped = std::round(a / step_deg) * step_deg;
      if (snapped >= 360.0) snapped = 0.0;
      return snapped;
    }
    return a;
  }

  // Snaps world coordinates to grid only if within snap tolerance
  [[nodiscard]] std::optional<core::Vec2d> snap_to_grid_if_close(core::Vec2d world_pos) const noexcept {
    if (!is_snap_enabled() || !has_flag(snap_flags_, SnapFlags::Grid)) return std::nullopt;
    const core::Vec2d pt = snap_to_grid(world_pos);
    if (world_pos.distance_to(pt) <= snap_tolerance_) {
      return pt;
    }
    return std::nullopt;
  }

  // Snaps cursor position against grid, shapes, interface ports, and guides
  [[nodiscard]] SnapResult resolve_snap(
      core::Vec2d world_pos,
      const core::GraphModel& graph,
      const core::ISpatialIndex* spatial_index = nullptr,
      std::optional<core::Vec2d> orthogonal_anchor = std::nullopt) const {
    SnapResult result;
    result.snapped_point = world_pos;
    result.snapped = false;
    if (!is_snap_enabled()) {
      return result;
    }
    double best_dist_sq = snap_tolerance_ * snap_tolerance_;

    // 1. Priority 1: Snap to Interface Ports
    if (has_flag(snap_flags_, SnapFlags::InterfacePort)) {
      for (const auto& [pid, port] : graph.ports()) {
        const auto* parent_node = graph.find_node(port.parent_node);
        if (!parent_node || !parent_node->visible) continue;

        const core::Vec2d port_pos = parent_node->resolve_port_position(port);
        const double d_sq = world_pos.distance_sq_to(port_pos);
        if (d_sq < best_dist_sq) {
          best_dist_sq = d_sq;
          result.snapped_point = port_pos;
          result.snapped = true;
          result.triggered_flag = SnapFlags::InterfacePort;
          result.target_entity_id = port.parent_node;
          result.target_port_id = pid;
        }
      }
    }

    if (result.snapped && result.triggered_flag == SnapFlags::InterfacePort) {
      return result;
    }

    // 2. Priority 2: Snap to Shape Center or Bounds
    if (has_flag(snap_flags_, SnapFlags::ShapeBounds) || has_flag(snap_flags_, SnapFlags::ShapeCenter)) {
      const core::Rect2d search_box = core::Rect2d(
          world_pos.x - snap_tolerance_, world_pos.y - snap_tolerance_,
          snap_tolerance_ * 2.0, snap_tolerance_ * 2.0);

      std::vector<uint64_t> candidate_nodes;
      if (spatial_index) {
        candidate_nodes = spatial_index->query_box(search_box);
      } else {
        for (const auto& [nid, node] : graph.nodes()) {
          candidate_nodes.push_back(nid);
        }
      }

      for (uint64_t nid : candidate_nodes) {
        const auto* node = graph.find_node(nid);
        if (!node || !node->visible) continue;

        const core::Rect2d b = node->bounds;

        if (has_flag(snap_flags_, SnapFlags::ShapeCenter)) {
          const core::Vec2d center = b.center();
          const double d_sq = world_pos.distance_sq_to(center);
          if (d_sq < best_dist_sq) {
            best_dist_sq = d_sq;
            result.snapped_point = center;
            result.snapped = true;
            result.triggered_flag = SnapFlags::ShapeCenter;
            result.target_entity_id = nid;
          }
        }

        if (has_flag(snap_flags_, SnapFlags::ShapeBounds)) {
          const std::array<core::Vec2d, 8> key_points = {
            b.top_left(), b.top_right(), b.bottom_left(), b.bottom_right(),
            core::Vec2d{b.center().x, b.top()}, core::Vec2d{b.center().x, b.bottom()},
            core::Vec2d{b.left(), b.center().y}, core::Vec2d{b.right(), b.center().y}
          };

          for (const auto& kp : key_points) {
            const double d_sq = world_pos.distance_sq_to(kp);
            if (d_sq < best_dist_sq) {
              best_dist_sq = d_sq;
              result.snapped_point = kp;
              result.snapped = true;
              result.triggered_flag = SnapFlags::ShapeBounds;
              result.target_entity_id = nid;
            }
          }
        }
      }
    }

    // 3. Priority 3: Connector Orthogonal Alignment
    if (has_flag(snap_flags_, SnapFlags::ConnectorOrthogonal) && orthogonal_anchor.has_value()) {
      const core::Vec2d anchor = *orthogonal_anchor;
      const double dx = std::abs(world_pos.x - anchor.x);
      const double dy = std::abs(world_pos.y - anchor.y);

      if (dx < snap_tolerance_ && dx * dx < best_dist_sq) {
        result.snapped_point.x = anchor.x;
        result.snapped = true;
        result.triggered_flag = SnapFlags::ConnectorOrthogonal;
        result.guide_lines.push_back(SnapGuideLine{
          .p1 = {anchor.x, std::min(anchor.y, world_pos.y) - 50.0},
          .p2 = {anchor.x, std::max(anchor.y, world_pos.y) + 50.0},
          .is_vertical = true
        });
      } else if (dy < snap_tolerance_ && dy * dy < best_dist_sq) {
        result.snapped_point.y = anchor.y;
        result.snapped = true;
        result.triggered_flag = SnapFlags::ConnectorOrthogonal;
        result.guide_lines.push_back(SnapGuideLine{
          .p1 = {std::min(anchor.x, world_pos.x) - 50.0, anchor.y},
          .p2 = {std::max(anchor.x, world_pos.x) + 50.0, anchor.y},
          .is_vertical = false
        });
      }
    }

    // 4. Priority 4: Snap to Grid
    if (!result.snapped && has_flag(snap_flags_, SnapFlags::Grid)) {
      const core::Vec2d grid_pt = snap_to_grid(world_pos);
      const double d_sq = world_pos.distance_sq_to(grid_pt);
      if (d_sq <= snap_tolerance_ * snap_tolerance_) {
        result.snapped_point = grid_pt;
        result.snapped = true;
        result.triggered_flag = SnapFlags::Grid;
      }
    }

    return result;
  }

private:
  GridConfig config_;
  SnapFlags snap_flags_{SnapFlags::All};
  SnapStrength snap_strength_{SnapStrength::Normal};
  double snap_tolerance_{6.0};
};

} // namespace vector::grid

#endif // VECTOR_GRID_GRID_MANAGER_HPP
