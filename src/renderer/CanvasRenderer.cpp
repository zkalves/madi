// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/renderer/CanvasRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vector::renderer {

namespace {

[[maybe_unused]] void draw_line_dda(core::Vec2d p1, core::Vec2d p2, uint32_t color, uint32_t* buffer, int width, int height) {
  int x0 = static_cast<int>(std::round(p1.x));
  int y0 = static_cast<int>(std::round(p1.y));
  const int x1 = static_cast<int>(std::round(p2.x));
  const int y1 = static_cast<int>(std::round(p2.y));

  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height) {
      buffer[static_cast<size_t>(y0) * width + x0] = color;
    }
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

} // namespace

struct CanvasRenderer::SkiaBackendOpaque {
  // Opaque handle for Skia GrDirectContext & SkSurface when compiled with VECTOR_ENABLE_SKIA
  void* gr_context{nullptr};
  void* sk_surface{nullptr};
};

CanvasRenderer::CanvasRenderer()
    : skia_backend_(std::make_unique<SkiaBackendOpaque>()) {
  initialize_software(width_, height_);
}

CanvasRenderer::~CanvasRenderer() = default;

bool CanvasRenderer::initialize_gpu(void* gl_context) {
#ifdef VECTOR_ENABLE_SKIA
  // GrDirectContext::MakeGL(...) initialization
  skia_backend_->gr_context = gl_context;
  active_backend_ = RendererBackend::OpenGL;
  return true;
#else
  (void)gl_context;
  active_backend_ = RendererBackend::SoftwareRaster;
  return false;
#endif
}

bool CanvasRenderer::initialize_software(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  software_framebuffer_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0xFFFFFFFF);
  active_backend_ = RendererBackend::SoftwareRaster;
  return true;
}

void CanvasRenderer::resize(int width, int height, double dpr) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  dpr_ = (dpr > 0.0) ? dpr : 1.0;
  if (active_backend_ == RendererBackend::SoftwareRaster) {
    software_framebuffer_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0xFFFFFFFF);
  }
}

void CanvasRenderer::begin_frame(const RenderContext& ctx) {
  (void)ctx;
  if (active_backend_ == RendererBackend::SoftwareRaster) {
    // Fill canvas buffer with transparent black so underlying desk and page sheet composite cleanly
    std::fill(software_framebuffer_.begin(), software_framebuffer_.end(), 0x00000000);
  }
}

void CanvasRenderer::end_frame() {
  // Flush rendering queue / swap buffers
}

namespace {

uint32_t parse_color_string(std::string_view str, uint32_t fallback) {
  if (str.empty()) return fallback;
  if (str.front() == '#') str.remove_prefix(1);
  if (str.starts_with("0x") || str.starts_with("0X")) str.remove_prefix(2);

  if (str.size() == 3) {
    std::string s6;
    s6.push_back(str[0]); s6.push_back(str[0]);
    s6.push_back(str[1]); s6.push_back(str[1]);
    s6.push_back(str[2]); s6.push_back(str[2]);
    char* end = nullptr;
    unsigned long val = std::strtoul(s6.c_str(), &end, 16);
    if (end != s6.c_str()) return 0xFF000000 | static_cast<uint32_t>(val);
  } else if (str.size() == 6) {
    char* end = nullptr;
    unsigned long val = std::strtoul(std::string(str).c_str(), &end, 16);
    if (end != std::string(str).c_str()) return 0xFF000000 | static_cast<uint32_t>(val);
  } else if (str.size() == 8) {
    char* end = nullptr;
    unsigned long val = std::strtoul(std::string(str).c_str(), &end, 16);
    if (end != std::string(str).c_str()) return static_cast<uint32_t>(val);
  }
  return fallback;
}

}  // namespace

void CanvasRenderer::draw_grid(const grid::GridManager& grid_mgr, const RenderContext& ctx, const RenderStyle& style) {
  if (!grid_mgr.config().visible || active_backend_ != RendererBackend::SoftwareRaster) {
    return;
  }

  const auto& cfg = grid_mgr.config();
  if (ctx.zoom <= 1e-4) return;

  const uint32_t minor_color = style.grid_minor;
  const uint32_t major_color = style.grid_major;
  const core::Rect2d frustum = ctx.visible_frustum;

  if (cfg.snap_type == grid::SnapType::Isometric) {
    // 30-degree isometric grid lines
    constexpr double tan30 = 0.5773502691896257; // 1 / sqrt(3)
    double base_iso_step = cfg.minor_spacing_x() / tan30;
    if (base_iso_step <= 1e-4) base_iso_step = 20.0 / tan30;

    // Decimate isometric lines so screen spacing is at least 20px
    double iso_step = base_iso_step;
    while (iso_step * ctx.zoom < 20.0) {
      iso_step *= 2.0;
    }

    const double diag_start = std::floor((frustum.top() - frustum.right() * tan30) / iso_step) * iso_step;
    const double diag_end = std::ceil((frustum.bottom() - frustum.left() * tan30) / iso_step) * iso_step;
    for (double d = diag_start; d <= diag_end; d += iso_step) {
      for (int sx = 0; sx < width_; sx += 4) {
        const double wx = (sx - ctx.pan_offset.x) / ctx.zoom;
        const double wy = wx * tan30 + d;
        const core::Vec2d s = ctx.world_to_screen.map_point({wx, wy});
        const int sy = static_cast<int>(std::round(s.y));
        if (sy >= 0 && sy < height_) {
          software_framebuffer_[static_cast<size_t>(sy) * width_ + sx] = minor_color;
        }
      }
    }
    return;
  }

  // Rectangular / Polar dot lattice grid.
  // Dynamic Grid Decimation (LOD):
  // Screen distance between drawn points must be at least 20 pixels.
  // When zooming out:
  // 1. If minor subdivisions get closer than 15px on screen, drop them and display only major grid points.
  // 2. If major grid points get closer than 20px on screen, scale displayed spacing by factors of 2.
  // This completely eliminates distortion, moiré patterns, and performance slowdowns on zoom out.
  constexpr double kMinScreenSpacing = 20.0;

  const double base_minor_x = std::max(1e-4, cfg.minor_spacing_x());
  const double base_minor_y = std::max(1e-4, cfg.minor_spacing_y());
  const double major_x = std::max(1e-4, cfg.major_spacing_x);
  const double major_y = std::max(1e-4, cfg.major_spacing_y);

  const bool minor_too_dense = (base_minor_x * ctx.zoom < 15.0) ||
                               (base_minor_y * ctx.zoom < 15.0) ||
                               !cfg.show_subdivisions;

  double draw_step_x = minor_too_dense ? major_x : base_minor_x;
  double draw_step_y = minor_too_dense ? major_y : base_minor_y;

  while (draw_step_x * ctx.zoom < kMinScreenSpacing) {
    draw_step_x *= 2.0;
  }
  while (draw_step_y * ctx.zoom < kMinScreenSpacing) {
    draw_step_y *= 2.0;
  }

  // Guard against pathological viewport ranges
  const double max_cols = std::max(10.0, static_cast<double>(width_) / 10.0);
  if ((frustum.width / draw_step_x) > max_cols) {
    draw_step_x = frustum.width / max_cols;
  }
  const double max_rows = std::max(10.0, static_cast<double>(height_) / 10.0);
  if ((frustum.height / draw_step_y) > max_rows) {
    draw_step_y = frustum.height / max_rows;
  }

  const double start_x = std::floor(frustum.left() / draw_step_x) * draw_step_x;
  const double end_x = std::ceil(frustum.right() / draw_step_x) * draw_step_x;
  const double start_y = std::floor(frustum.top() / draw_step_y) * draw_step_y;
  const double end_y = std::ceil(frustum.bottom() / draw_step_y) * draw_step_y;

  for (double wx = start_x; wx <= end_x; wx += draw_step_x) {
    const int sx = static_cast<int>(std::round(wx * ctx.zoom + ctx.pan_offset.x));
    if (sx < 0 || sx >= width_) continue;

    const bool wx_is_major = (major_x > 1e-4) &&
        (std::abs(std::fmod(std::abs(wx) + 1e-4, major_x)) < 1e-2 ||
         std::abs(std::fmod(std::abs(wx) + 1e-4, major_x) - major_x) < 1e-2);

    for (double wy = start_y; wy <= end_y; wy += draw_step_y) {
      const int sy = static_cast<int>(std::round(wy * ctx.zoom + ctx.pan_offset.y));
      if (sy < 0 || sy >= height_) continue;

      const bool wy_is_major = (major_y > 1e-4) &&
          (std::abs(std::fmod(std::abs(wy) + 1e-4, major_y)) < 1e-2 ||
           std::abs(std::fmod(std::abs(wy) + 1e-4, major_y) - major_y) < 1e-2);

      const uint32_t col = (wx_is_major && wy_is_major) ? major_color : minor_color;
      software_framebuffer_[static_cast<size_t>(sy) * width_ + sx] = col;
    }
  }
}

void CanvasRenderer::draw_node(const core::Node& node, const RenderContext& ctx, const RenderStyle& style) {
  if (active_backend_ != RendererBackend::SoftwareRaster) return;

  const core::Rect2d screen_rect = ctx.world_to_screen.map_rect(node.bounds);
  const int min_x = std::clamp(static_cast<int>(std::floor(screen_rect.left())), 0, width_ - 1);
  const int max_x = std::clamp(static_cast<int>(std::ceil(screen_rect.right())), 0, width_ - 1);
  const int min_y = std::clamp(static_cast<int>(std::floor(screen_rect.top())), 0, height_ - 1);
  const int max_y = std::clamp(static_cast<int>(std::ceil(screen_rect.bottom())), 0, height_ - 1);

  if (min_x >= max_x || min_y >= max_y) return;

  uint32_t effective_fill = style.fill_color;
  auto it_fill = node.properties.find("fill_color");
  if (it_fill != node.properties.end()) {
    effective_fill = parse_color_string(it_fill->second, style.fill_color);
  }
  uint32_t effective_stroke = style.stroke_color;
  auto it_stroke = node.properties.find("stroke_color");
  if (it_stroke != node.properties.end()) {
    effective_stroke = parse_color_string(it_stroke->second, style.stroke_color);
  }

  const uint32_t fill_color = effective_fill;
  const uint32_t stroke_color = node.selected ? style.selection_border : effective_stroke;

  if (node.shape == core::ShapeType::Ellipse) {
    const double cx = (min_x + max_x) * 0.5;
    const double cy = (min_y + max_y) * 0.5;
    const double rx = (max_x - min_x) * 0.5;
    const double ry = (max_y - min_y) * 0.5;
    const double rx_sq = rx * rx;
    const double ry_sq = ry * ry;

    for (int y = min_y; y <= max_y; ++y) {
      const double dy = y - cy;
      const size_t row_offset = static_cast<size_t>(y) * width_;
      for (int x = min_x; x <= max_x; ++x) {
        const double dx = x - cx;
        const double norm = (dx * dx) / rx_sq + (dy * dy) / ry_sq;
        if (norm <= 1.0) {
          if (norm >= 0.88) {
            software_framebuffer_[row_offset + x] = stroke_color;
          } else {
            software_framebuffer_[row_offset + x] = fill_color;
          }
        }
      }
    }
  } else if (node.shape == core::ShapeType::CustomPolygon) {
    const auto world_contours = node.get_world_subpath_polygons();
    std::vector<core::Polygon2d> screen_polys;
    std::vector<std::vector<core::Vec2d>> all_screen_pts;
    screen_polys.reserve(world_contours.size());
    all_screen_pts.reserve(world_contours.size());

    for (const auto& w_contour : world_contours) {
      if (w_contour.empty()) continue;
      std::vector<core::Vec2d> s_pts;
      s_pts.reserve(w_contour.size());
      for (const auto& wp : w_contour) {
        s_pts.push_back(ctx.world_to_screen.map_point(wp));
      }
      screen_polys.emplace_back(s_pts);
      all_screen_pts.push_back(std::move(s_pts));
    }

    for (int y = min_y; y <= max_y; ++y) {
      const size_t row_offset = static_cast<size_t>(y) * width_;
      for (int x = min_x; x <= max_x; ++x) {
        const core::Vec2d pt(static_cast<double>(x), static_cast<double>(y));
        int inside_count = 0;
        for (const auto& poly : screen_polys) {
          if (poly.contains(pt)) {
            inside_count++;
          }
        }
        if ((inside_count % 2) == 1) {
          software_framebuffer_[row_offset + x] = fill_color;
        }
      }
    }
#ifndef VECTOR_ENABLE_QT
    for (const auto& s_pts : all_screen_pts) {
      const size_t pt_count = s_pts.size();
      if (pt_count < 2) continue;
      for (size_t i = 0; i < pt_count; ++i) {
        draw_line_dda(s_pts[i], s_pts[(i + 1) % pt_count], stroke_color,
                      software_framebuffer_.data(), width_, height_);
      }
    }
#endif
  } else {
    uint32_t effective_fill = fill_color;
    if (node.shape == core::ShapeType::InputPort) {
      auto it_f = node.properties.find("fill_color");
      effective_fill = (it_f != node.properties.end()) ? fill_color : (node.selected ? style.selection_border : 0xFF10B981);
    } else if (node.shape == core::ShapeType::OutputPort) {
      auto it_f = node.properties.find("fill_color");
      effective_fill = (it_f != node.properties.end()) ? fill_color : (node.selected ? style.selection_border : 0xFFF59E0B);
    }

    // Solid fill
    for (int y = min_y + 1; y < max_y; ++y) {
      const size_t row_offset = static_cast<size_t>(y) * width_;
      for (int x = min_x + 1; x < max_x; ++x) {
        software_framebuffer_[row_offset + x] = effective_fill;
      }
    }

    // Top banner for Hierarchical Blocks
    if (node.shape == core::ShapeType::HierarchicalBlock) {
      const int header_h = std::min(22, max_y - min_y);
      const uint32_t banner_color = node.selected ? style.banner_selected : style.banner_color;
      for (int y = min_y; y < min_y + header_h; ++y) {
        const size_t row_offset = static_cast<size_t>(y) * width_;
        for (int x = min_x; x <= max_x; ++x) {
          software_framebuffer_[row_offset + x] = banner_color;
        }
      }
      // Inner line indicating dual-border block encapsulation
      if (max_x - min_x > 8 && max_y - min_y > 8) {
        for (int x = min_x + 3; x <= max_x - 3; ++x) {
          software_framebuffer_[static_cast<size_t>(max_y - 3) * width_ + x] = stroke_color;
        }
        for (int y = min_y + header_h; y <= max_y - 3; ++y) {
          software_framebuffer_[static_cast<size_t>(y) * width_ + min_x + 3] = stroke_color;
          software_framebuffer_[static_cast<size_t>(y) * width_ + max_x - 3] = stroke_color;
        }
      }
    }

    // Border outline
    for (int x = min_x; x <= max_x; ++x) {
      software_framebuffer_[static_cast<size_t>(min_y) * width_ + x] = stroke_color;
      software_framebuffer_[static_cast<size_t>(max_y) * width_ + x] = stroke_color;
    }
    for (int y = min_y; y <= max_y; ++y) {
      software_framebuffer_[static_cast<size_t>(y) * width_ + min_x] = stroke_color;
      software_framebuffer_[static_cast<size_t>(y) * width_ + max_x] = stroke_color;
    }
  }

  // Draw 4 corner handles if selected
  if (node.selected) {
    const int handle_r = 3;
    const std::array<std::pair<int, int>, 4> corners = {
      std::pair{min_x, min_y}, std::pair{max_x, min_y},
      std::pair{min_x, max_y}, std::pair{max_x, max_y}
    };
    for (const auto& [cx, cy] : corners) {
      for (int dy = -handle_r; dy <= handle_r; ++dy) {
        for (int dx = -handle_r; dx <= handle_r; ++dx) {
          const int px = cx + dx;
          const int py = cy + dy;
          if (px >= 0 && px < width_ && py >= 0 && py < height_) {
            software_framebuffer_[static_cast<size_t>(py) * width_ + px] = style.selection_border;
          }
        }
      }
    }
  }
}

void CanvasRenderer::draw_edge(const core::Edge& edge, const RenderContext& ctx, const RenderStyle& style) {
  if (edge.waypoints.size() < 2 || active_backend_ != RendererBackend::SoftwareRaster) return;

  uint32_t effective_wire = style.wire_color;
  auto it_wire = edge.properties.find("stroke_color");
  if (it_wire != edge.properties.end()) {
    effective_wire = parse_color_string(it_wire->second, style.wire_color);
  }
  const uint32_t color = edge.selected ? style.selection_border : effective_wire;

  auto draw_segment_list = [&](const std::vector<core::Vec2d>& wpts, bool is_branch) {
    if (wpts.size() < 2) return;
    for (size_t i = 0; i + 1 < wpts.size(); ++i) {
      const core::Vec2d p1 = ctx.world_to_screen.map_point(wpts[i]);
      const core::Vec2d p2 = ctx.world_to_screen.map_point(wpts[i + 1]);

      // Bresenham's line rasterization
      int x0 = static_cast<int>(std::round(p1.x));
      int y0 = static_cast<int>(std::round(p1.y));
      const int x1 = static_cast<int>(std::round(p2.x));
      const int y1 = static_cast<int>(std::round(p2.y));

      const int dx = std::abs(x1 - x0);
      const int sx = x0 < x1 ? 1 : -1;
      const int dy = -std::abs(y1 - y0);
      const int sy = y0 < y1 ? 1 : -1;
      int err = dx + dy;

      while (true) {
        if (x0 >= 0 && x0 < width_ && y0 >= 0 && y0 < height_) {
          software_framebuffer_[static_cast<size_t>(y0) * width_ + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
    }

    // Draw connection dot (anchor or branch junction ball)
    const core::Vec2d p_start = ctx.world_to_screen.map_point(wpts.front());
    const int cx_s = static_cast<int>(std::round(p_start.x));
    const int cy_s = static_cast<int>(std::round(p_start.y));
    const int r_dot = is_branch ? 4 : 3;
    for (int dy = -r_dot; dy <= r_dot; ++dy) {
      for (int dx = -r_dot; dx <= r_dot; ++dx) {
        if (dx * dx + dy * dy <= r_dot * r_dot) {
          const int px = cx_s + dx;
          const int py = cy_s + dy;
          if (px >= 0 && px < width_ && py >= 0 && py < height_) {
            software_framebuffer_[static_cast<size_t>(py) * width_ + px] = color;
          }
        }
      }
    }

    // Draw terminal arrowhead at destination endpoint
    const core::Vec2d p_end = ctx.world_to_screen.map_point(wpts.back());
    const core::Vec2d p_penult = ctx.world_to_screen.map_point(wpts[wpts.size() - 2]);
    core::Vec2d d = p_end - p_penult;
    const double len = d.length();
    if (len > 1e-3) {
      d = d / len;
      const core::Vec2d norm{-d.y, d.x};
      const double arrow_len = 10.0;
      const double arrow_half_w = 5.0;

      const core::Vec2d tip = p_end;
      const core::Vec2d base = p_end - d * arrow_len;
      const core::Vec2d w1 = base + norm * arrow_half_w;
      const core::Vec2d w2 = base - norm * arrow_half_w;

      const int min_ax = std::clamp(static_cast<int>(std::floor(std::min({tip.x, w1.x, w2.x}))), 0, width_ - 1);
      const int max_ax = std::clamp(static_cast<int>(std::ceil(std::max({tip.x, w1.x, w2.x}))), 0, width_ - 1);
      const int min_ay = std::clamp(static_cast<int>(std::floor(std::min({tip.y, w1.y, w2.y}))), 0, height_ - 1);
      const int max_ay = std::clamp(static_cast<int>(std::ceil(std::max({tip.y, w1.y, w2.y}))), 0, height_ - 1);

      auto sign = [](core::Vec2d pt1, core::Vec2d pt2, core::Vec2d pt3) -> double {
        return (pt1.x - pt3.x) * (pt2.y - pt3.y) - (pt2.x - pt3.x) * (pt1.y - pt3.y);
      };

      for (int y = min_ay; y <= max_ay; ++y) {
        const size_t row = static_cast<size_t>(y) * width_;
        for (int x = min_ax; x <= max_ax; ++x) {
          const core::Vec2d pt{static_cast<double>(x), static_cast<double>(y)};
          const double d1 = sign(pt, tip, w1);
          const double d2 = sign(pt, w1, w2);
          const double d3 = sign(pt, w2, tip);
          const bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
          const bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
          if (!(has_neg && has_pos)) {
            software_framebuffer_[row + x] = color;
          }
        }
      }
    }
  };

  // Draw primary trunk
  draw_segment_list(edge.waypoints, false);

  // Draw multipoint destination branches
  for (const auto& br : edge.extra_destinations) {
    draw_segment_list(br.waypoints, true);
  }
}

void CanvasRenderer::draw_port(const core::Port& port, core::Vec2d world_pos, const RenderContext& ctx) {
  (void)port;
  if (active_backend_ != RendererBackend::SoftwareRaster) return;

  const core::Vec2d s = ctx.world_to_screen.map_point(world_pos);
  const int cx = static_cast<int>(std::round(s.x));
  const int cy = static_cast<int>(std::round(s.y));
  const int r = 3;

  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      if (dx * dx + dy * dy <= r * r) {
        const int px = cx + dx;
        const int py = cy + dy;
        if (px >= 0 && px < width_ && py >= 0 && py < height_) {
          software_framebuffer_[static_cast<size_t>(py) * width_ + px] = 0xFF3182CE;
        }
      }
    }
  }
}

void CanvasRenderer::draw_snap_guides(const std::vector<grid::SnapGuideLine>& guides, const RenderContext& ctx) {
  if (active_backend_ != RendererBackend::SoftwareRaster) return;
  const uint32_t guide_col = 0xFFE53E3E; // Red alignment guide

  for (const auto& g : guides) {
    const core::Vec2d p1 = ctx.world_to_screen.map_point(g.p1);
    const core::Vec2d p2 = ctx.world_to_screen.map_point(g.p2);

    int x0 = static_cast<int>(std::round(p1.x));
    int y0 = static_cast<int>(std::round(p1.y));
    const int x1 = static_cast<int>(std::round(p2.x));
    const int y1 = static_cast<int>(std::round(p2.y));

    if (g.is_vertical && x0 >= 0 && x0 < width_) {
      const int start_y = std::clamp(std::min(y0, y1), 0, height_ - 1);
      const int end_y = std::clamp(std::max(y0, y1), 0, height_ - 1);
      for (int y = start_y; y <= end_y; y += 4) {
        software_framebuffer_[static_cast<size_t>(y) * width_ + x0] = guide_col;
      }
    } else if (!g.is_vertical && y0 >= 0 && y0 < height_) {
      const int start_x = std::clamp(std::min(x0, x1), 0, width_ - 1);
      const int end_x = std::clamp(std::max(x0, x1), 0, width_ - 1);
      for (int x = start_x; x <= end_x; x += 4) {
        software_framebuffer_[static_cast<size_t>(y0) * width_ + x] = guide_col;
      }
    }
  }
}

void CanvasRenderer::draw_selection_marquee(const core::Rect2d& marquee, const RenderContext& ctx) {
  if (active_backend_ != RendererBackend::SoftwareRaster) return;

  const core::Rect2d screen_rect = ctx.world_to_screen.map_rect(marquee);
  const int min_x = std::clamp(static_cast<int>(std::floor(screen_rect.left())), 0, width_ - 1);
  const int max_x = std::clamp(static_cast<int>(std::ceil(screen_rect.right())), 0, width_ - 1);
  const int min_y = std::clamp(static_cast<int>(std::floor(screen_rect.top())), 0, height_ - 1);
  const int max_y = std::clamp(static_cast<int>(std::ceil(screen_rect.bottom())), 0, height_ - 1);

  const uint32_t border_col = 0xFF3182CE;
  for (int x = min_x; x <= max_x; x += 2) {
    software_framebuffer_[static_cast<size_t>(min_y) * width_ + x] = border_col;
    software_framebuffer_[static_cast<size_t>(max_y) * width_ + x] = border_col;
  }
  for (int y = min_y; y <= max_y; y += 2) {
    software_framebuffer_[static_cast<size_t>(y) * width_ + min_x] = border_col;
    software_framebuffer_[static_cast<size_t>(y) * width_ + max_x] = border_col;
  }
}

void CanvasRenderer::draw_pending_wire(core::Vec2d source, core::Vec2d target, const RenderContext& ctx) {
  if (active_backend_ != RendererBackend::SoftwareRaster) return;

  const core::Vec2d p_src = ctx.world_to_screen.map_point(source);
  const core::Vec2d p_dst = ctx.world_to_screen.map_point(target);
  const core::Vec2d p_mid{p_dst.x, p_src.y};

  const std::array<std::pair<core::Vec2d, core::Vec2d>, 2> segments = {
    std::pair{p_src, p_mid},
    std::pair{p_mid, p_dst}
  };

  const uint32_t wire_color = 0xFF00B4D8; // Bright cyan/blue rubberband

  for (const auto& [seg_p1, seg_p2] : segments) {
    int x0 = static_cast<int>(std::round(seg_p1.x));
    int y0 = static_cast<int>(std::round(seg_p1.y));
    const int x1 = static_cast<int>(std::round(seg_p2.x));
    const int y1 = static_cast<int>(std::round(seg_p2.y));

    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int step = 0;

    while (true) {
      if ((step++ % 6) < 4) { // Dashed pattern
        if (x0 >= 0 && x0 < width_ && y0 >= 0 && y0 < height_) {
          software_framebuffer_[static_cast<size_t>(y0) * width_ + x0] = wire_color;
        }
      }
      if (x0 == x1 && y0 == y1) break;
      const int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
}

const uint8_t* CanvasRenderer::software_buffer() const noexcept {
  return reinterpret_cast<const uint8_t*>(software_framebuffer_.data());
}

size_t CanvasRenderer::software_buffer_pitch() const noexcept {
  return static_cast<size_t>(width_) * sizeof(uint32_t);
}

} // namespace vector::renderer
