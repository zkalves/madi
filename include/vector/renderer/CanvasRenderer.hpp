// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_RENDERER_CANVAS_RENDERER_HPP
#define VECTOR_RENDERER_CANVAS_RENDERER_HPP

#include "../core/Geometry.hpp"
#include "../core/GraphModel.hpp"
#include "../grid/GridManager.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vector::renderer {

enum class RendererBackend : uint8_t {
  OpenGL,
  Vulkan,
  SoftwareRaster
};

enum class LODLevel : uint8_t {
  High,    // Full detail, text labels, subpixel antialiasing, port pins
  Medium,  // Simplified shapes, labels, solid connectors
  Proxy    // Bounding box / wireframe proxies during high-speed drag/pan
};

struct RenderStyle {
  uint32_t fill_color{0xFFFFFFFF};    // ARGB8888
  uint32_t stroke_color{0xFF2D3748};  // ARGB8888
  double stroke_width{1.5};
  uint32_t text_color{0xFF1A202C};
  double font_size{12.0};
  uint32_t selection_color{0x403182CE};
  uint32_t selection_border{0xFF3182CE};
  uint32_t grid_minor{0xFFE2E8F0};
  uint32_t grid_major{0xFFCBD5E1};
  uint32_t wire_color{0xFF2B6CB0};
  uint32_t banner_color{0xFF3182CE};
  uint32_t banner_selected{0xFF2B6CB0};
};

struct RenderContext {
  core::Transform2d world_to_screen;
  core::Transform2d screen_to_world;
  core::Rect2d visible_frustum;
  double zoom{1.0};
  core::Vec2d pan_offset{0.0, 0.0};
  LODLevel lod{LODLevel::High};
  int viewport_width{800};
  int viewport_height{600};
  double device_pixel_ratio{1.0};
};

class ICanvasRenderer {
public:
  virtual ~ICanvasRenderer() = default;

  [[nodiscard]] virtual RendererBackend backend_type() const noexcept = 0;

  virtual bool initialize_gpu(void* gl_context = nullptr) = 0;
  virtual bool initialize_software(int width, int height) = 0;
  virtual void resize(int width, int height, double dpr = 1.0) = 0;

  virtual void begin_frame(const RenderContext& ctx) = 0;
  virtual void end_frame() = 0;

  virtual void draw_grid(const grid::GridManager& grid, const RenderContext& ctx, const RenderStyle& style = {}) = 0;
  virtual void draw_node(const core::Node& node, const RenderContext& ctx, const RenderStyle& style) = 0;
  virtual void draw_edge(const core::Edge& edge, const RenderContext& ctx, const RenderStyle& style) = 0;
  virtual void draw_port(const core::Port& port, core::Vec2d world_pos, const RenderContext& ctx) = 0;
  virtual void draw_snap_guides(const std::vector<grid::SnapGuideLine>& guides, const RenderContext& ctx) = 0;
  virtual void draw_selection_marquee(const core::Rect2d& marquee, const RenderContext& ctx) = 0;
  virtual void draw_pending_wire(core::Vec2d source, core::Vec2d target, const RenderContext& ctx) = 0;

  [[nodiscard]] virtual const uint8_t* software_buffer() const noexcept = 0;
  [[nodiscard]] virtual size_t software_buffer_pitch() const noexcept = 0;
};

// High-performance reference SIMD / Skia dual-backend renderer
class CanvasRenderer final : public ICanvasRenderer {
public:
  CanvasRenderer();
  ~CanvasRenderer() override;

  [[nodiscard]] RendererBackend backend_type() const noexcept override {
    return active_backend_;
  }

  bool initialize_gpu(void* gl_context = nullptr) override;
  bool initialize_software(int width, int height) override;
  void resize(int width, int height, double dpr = 1.0) override;

  void begin_frame(const RenderContext& ctx) override;
  void end_frame() override;

  void draw_grid(const grid::GridManager& grid, const RenderContext& ctx, const RenderStyle& style = {}) override;
  void draw_node(const core::Node& node, const RenderContext& ctx, const RenderStyle& style) override;
  void draw_edge(const core::Edge& edge, const RenderContext& ctx, const RenderStyle& style) override;
  void draw_port(const core::Port& port, core::Vec2d world_pos, const RenderContext& ctx) override;
  void draw_snap_guides(const std::vector<grid::SnapGuideLine>& guides, const RenderContext& ctx) override;
  void draw_selection_marquee(const core::Rect2d& marquee, const RenderContext& ctx) override;
  void draw_pending_wire(core::Vec2d source, core::Vec2d target, const RenderContext& ctx) override;

  [[nodiscard]] const uint8_t* software_buffer() const noexcept override;
  [[nodiscard]] size_t software_buffer_pitch() const noexcept override;

private:
  RendererBackend active_backend_{RendererBackend::SoftwareRaster};
  int width_{800};
  int height_{600};
  double dpr_{1.0};
  std::vector<uint32_t> software_framebuffer_;
  struct SkiaBackendOpaque;
  std::unique_ptr<SkiaBackendOpaque> skia_backend_;
};

} // namespace vector::renderer

#endif // VECTOR_RENDERER_CANVAS_RENDERER_HPP
