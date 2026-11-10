// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_RENDERER_CANVAS_WIDGET_HPP
#define VECTOR_RENDERER_CANVAS_WIDGET_HPP

#include "CanvasRenderer.hpp"
#include "../core/GraphModel.hpp"
#include "../core/HierarchicalBlock.hpp"
#include "../core/SpatialIndex.hpp"
#include "../core/Command.hpp"
#include "../grid/GridManager.hpp"

#ifdef VECTOR_ENABLE_QT
#include <QOpenGLWidget>
#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QTimer>
#include <QLineEdit>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#endif

#include <memory>
#include <unordered_map>
#include <vector>

namespace vector::renderer {

enum class ToolMode : uint8_t {
  Select,
  AddRectangle,
  AddRoundedRect,
  AddEllipse,
  AddHierarchicalBlock,
  AddConnector,
  AddPolygon,
  AddInputPort,
  AddOutputPort,
  AddCustomShape
};

enum class PlacementCategory : uint8_t {
  ShapeType,
  RegularPolygon,
  CustomPolygon,
  HierarchicalPort,
  SubsystemBuiltin,
  SubsystemFile
};

struct ShapePlacementTemplate {
  PlacementCategory category{PlacementCategory::ShapeType};
  core::ShapeType shape_type{core::ShapeType::Rectangle};
  int polygon_sides{6};
  std::vector<core::Vec2d> custom_vertices;
  std::string name;
  double width{120.0};
  double height{70.0};
  bool is_input_port{true};
  std::string port_type{"in"};
  std::string file_path;
  core::Vec2d anchor_point{0.0, 0.0}; // Normalized [0..1] insertion origin
  core::Vec2d resize_point{1.0, 1.0}; // Normalized [0..1] stretch/control point
};

#ifdef VECTOR_ENABLE_QT
inline QByteArray serialize_placement_template(const ShapePlacementTemplate& tmpl) {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << static_cast<quint8>(tmpl.category)
         << static_cast<quint8>(tmpl.shape_type)
         << static_cast<qint32>(tmpl.polygon_sides)
         << QString::fromStdString(tmpl.name)
         << tmpl.width
         << tmpl.height
         << tmpl.is_input_port
         << QString::fromStdString(tmpl.port_type)
         << QString::fromStdString(tmpl.file_path);
  stream << static_cast<qint32>(tmpl.custom_vertices.size());
  for (const auto& v : tmpl.custom_vertices) {
    stream << v.x << v.y;
  }
  stream << tmpl.anchor_point.x << tmpl.anchor_point.y
         << tmpl.resize_point.x << tmpl.resize_point.y;
  return bytes;
}

inline bool deserialize_placement_template(const QByteArray& bytes, ShapePlacementTemplate& out_tmpl) {
  if (bytes.isEmpty()) return false;
  QDataStream stream(bytes);
  quint8 cat = 0, stype = 0;
  qint32 sides = 0;
  QString name, port_type, file_path;
  double w = 0.0, h = 0.0;
  bool is_in = false;
  qint32 num_vertices = 0;

  stream >> cat >> stype >> sides >> name >> w >> h >> is_in >> port_type >> file_path >> num_vertices;
  out_tmpl.category = static_cast<PlacementCategory>(cat);
  out_tmpl.shape_type = static_cast<core::ShapeType>(stype);
  out_tmpl.polygon_sides = sides;
  out_tmpl.name = name.toStdString();
  out_tmpl.width = w;
  out_tmpl.height = h;
  out_tmpl.is_input_port = is_in;
  out_tmpl.port_type = port_type.toStdString();
  out_tmpl.file_path = file_path.toStdString();
  out_tmpl.custom_vertices.clear();
  if (num_vertices > 0 && num_vertices < 10000) {
    out_tmpl.custom_vertices.reserve(num_vertices);
    for (qint32 i = 0; i < num_vertices; ++i) {
      double vx = 0.0, vy = 0.0;
      stream >> vx >> vy;
      out_tmpl.custom_vertices.push_back({vx, vy});
    }
  }
  if (!stream.atEnd()) {
    double ax = 0.0, ay = 0.0, rx = 1.0, ry = 1.0;
    stream >> ax >> ay >> rx >> ry;
    out_tmpl.anchor_point = {ax, ay};
    out_tmpl.resize_point = {rx, ry};
  } else {
    out_tmpl.anchor_point = {0.0, 0.0};
    out_tmpl.resize_point = {1.0, 1.0};
  }
  return true;
}
#endif

enum class PagePreset : uint8_t {
  Letter,
  Legal,
  Tabloid,
  A4,
  A3,
  A2,
  A1,
  A0,
  Custom
};

enum class PageOrientation : uint8_t {
  Portrait,
  Landscape
};

inline const char* to_string(PagePreset p) noexcept {
  switch (p) {
    case PagePreset::Letter: return "Letter (8.5 × 11 in)";
    case PagePreset::Legal: return "Legal (8.5 × 14 in)";
    case PagePreset::Tabloid: return "Tabloid (11 × 17 in)";
    case PagePreset::A4: return "A4 (210 × 297 mm)";
    case PagePreset::A3: return "A3 (297 × 420 mm)";
    case PagePreset::A2: return "A2 (420 × 594 mm)";
    case PagePreset::A1: return "A1 (594 × 841 mm)";
    case PagePreset::A0: return "A0 (841 × 1189 mm)";
    case PagePreset::Custom: return "Custom";
  }
  return "Custom";
}

inline const char* to_string(PageOrientation o) noexcept {
  switch (o) {
    case PageOrientation::Portrait: return "Portrait";
    case PageOrientation::Landscape: return "Landscape";
  }
  return "Landscape";
}

inline core::Vec2d get_page_preset_dimensions(PagePreset preset, PageOrientation orientation) {
  double w = 1100.0, h = 850.0;
  switch (preset) {
    case PagePreset::Letter:  w = 850.0; h = 1100.0; break;
    case PagePreset::Legal:   w = 850.0; h = 1400.0; break;
    case PagePreset::Tabloid: w = 1100.0; h = 1700.0; break;
    case PagePreset::A4:      w = 827.0; h = 1169.0; break;
    case PagePreset::A3:      w = 1169.0; h = 1654.0; break;
    case PagePreset::A2:      w = 1654.0; h = 2339.0; break;
    case PagePreset::A1:      w = 2339.0; h = 3311.0; break;
    case PagePreset::A0:      w = 3311.0; h = 4681.0; break;
    case PagePreset::Custom:  w = 1100.0; h = 850.0; break;
  }
  if (orientation == PageOrientation::Landscape) {
    return {std::max(w, h), std::min(w, h)};
  } else {
    return {std::min(w, h), std::max(w, h)};
  }
}

enum class HandleType : uint8_t {
  None,
  TopLeft,
  TopCenter,
  TopRight,
  MiddleRight,
  BottomRight,
  BottomCenter,
  BottomLeft,
  MiddleLeft,
  RotationStalk,
  QuickConnectNorth,
  QuickConnectEast,
  QuickConnectSouth,
  QuickConnectWest
};

enum class WireCrossingStyle : uint8_t {
  DotPriority,        // Style 1 (Modern EDA): Wires that cross do not connect unless there is a junction ball/dot
  BridgeLoopPriority  // Style 2 (Legacy Schematic): Wires that cross connect, unless there is a loop/bridge jumping over
};

#ifdef VECTOR_ENABLE_QT
class CanvasWidget : public QOpenGLWidget {
  Q_OBJECT
#else
class CanvasWidget {
#endif

public:
#ifdef VECTOR_ENABLE_QT
  explicit CanvasWidget(QWidget* parent = nullptr);
  ~CanvasWidget() override;
#else
  CanvasWidget();
  virtual ~CanvasWidget();
#endif

  void set_graph_model(core::GraphModel* model) {
    graph_model_ = model;
    refresh_spatial_index();
  }

  [[nodiscard]] core::GraphModel* graph_model() const noexcept {
    return graph_model_;
  }

  void set_spatial_index(core::ISpatialIndex* index) noexcept {
    spatial_index_ = index;
  }

  void set_command_history(core::CommandHistory* history) noexcept {
    command_history_ = history;
  }

  [[nodiscard]] core::CommandHistory* command_history() const noexcept {
    return command_history_;
  }

  void set_grid_manager(grid::GridManager* grid) noexcept {
    grid_manager_ = grid;
  }

  [[nodiscard]] grid::GridManager* grid_manager() const noexcept {
    return grid_manager_;
  }

  void set_hierarchy_context(core::HierarchyContext* ctx) noexcept {
    hierarchy_context_ = ctx;
  }

  [[nodiscard]] core::HierarchyContext* hierarchy_context() const noexcept {
    return hierarchy_context_;
  }

  [[nodiscard]] std::string current_breadcrumb_path() const {
    return hierarchy_context_ ? hierarchy_context_->breadcrumb_path() : "Top";
  }

  // Tool Mode & Unified Shape Placement Management
  void set_tool_mode(ToolMode mode);
  [[nodiscard]] ToolMode tool_mode() const noexcept { return tool_mode_; }
  [[nodiscard]] bool is_placement_tool_active() const noexcept;
  [[nodiscard]] bool is_polygon_drawing_active() const noexcept;
  [[nodiscard]] core::Rect2d get_placement_bounds(ToolMode mode, core::Vec2d center_world) const;
  [[nodiscard]] const core::Rect2d& placement_preview_bounds() const noexcept { return placement_preview_bounds_; }
  void set_pending_polygon_sides(int sides) noexcept { pending_polygon_sides_ = std::max(3, sides); }
  [[nodiscard]] int pending_polygon_sides() const noexcept { return pending_polygon_sides_; }
  void cancel_placement();

  void start_shape_placement(const ShapePlacementTemplate& tmpl);
  [[nodiscard]] const ShapePlacementTemplate& active_placement_template() const noexcept { return active_template_; }
  core::NodeId instantiate_template_at(const ShapePlacementTemplate& tmpl, core::Vec2d center_world);

  // Interactive Point-and-Click Polygon Creation
  [[nodiscard]] const std::vector<core::Vec2d>& in_progress_polygon_points() const noexcept { return in_progress_polygon_points_; }
  bool finish_polygon_creation();
  void cancel_polygon_creation();

  // Shape and Wiring Actions
  core::NodeId add_shape(core::ShapeType type, core::Vec2d world_pos, const std::string& name = {}, bool snap_pos = true);
  core::NodeId add_polygon(core::Vec2d world_pos, int sides = 6, const std::string& name = {}, bool snap_pos = true);
  core::NodeId add_custom_polygon_shape(
      core::Vec2d world_pos,
      const std::vector<core::Vec2d>& normalized_vertices,
      const std::string& name = {},
      double width = 120.0,
      double height = 100.0,
      bool snap_pos = true);
  void delete_selected();
  void select_all();
  void clear_selection();

  // Clipboard & Nudging Engine
  void copy_selected();
  void cut_selected();
  void paste_clipboard(core::Vec2d target_pos = {0.0, 0.0}, bool use_target_pos = false);
  void duplicate_selected();
  void nudge_selected(core::Vec2d delta);
  [[nodiscard]] bool has_clipboard() const noexcept { return !clipboard_nodes_.empty(); }
  [[nodiscard]] core::EdgeId selected_edge() const noexcept { return selected_edge_; }
  void select_edge(core::EdgeId id, bool clear_previous = true);
  core::EdgeId find_hit_edge(core::Vec2d world_pt, double tolerance) const;

  // Subsystem Management (Built-in or Separate File)
  core::NodeId add_subsystem(core::Vec2d center, const std::string& name = {}, bool is_file = false, const std::string& file_path = {});
  bool convert_subsystem_to_file(core::NodeId nid, const std::string& file_path);
  bool convert_subsystem_to_builtin(core::NodeId nid);
  bool reload_subsystem_file(core::NodeId nid);
  bool save_subsystem_file(core::NodeId nid);
  bool drill_down_node(core::NodeId nid);
  core::GraphModel* ensure_subsystem_graph(core::NodeId nid);
  core::GraphModel* get_subsystem_graph(core::NodeId nid);

  // Hierarchical Diagram Port Management & Automatic Symbol Sync
  core::NodeId add_hierarchical_port(core::Vec2d pos, bool is_input, const std::string& name = {}, bool snap_pos = true);
  void sync_current_subsystem_symbol();

  // Vector drawing and manipulation operations
  core::EdgeId add_wire_by_nodes(core::NodeId src_node, core::NodeId tgt_node, const std::string& name = {});
  core::EdgeId add_wire_by_points(core::Vec2d p1, core::Vec2d p2, const std::string& name = {});
  bool move_selected(core::Vec2d delta);
  bool move_node(core::NodeId id, core::Vec2d delta);
  void select_node(core::NodeId id, bool clear_previous = true);
  bool select_node_by_name(const std::string& name);
  bool delete_node_by_name(const std::string& name);

  // Figure Merging, Splitting, and 2D Boolean Logic Operations
  using BooleanOp = core::BooleanOp;
  bool boolean_combine_selected(BooleanOp op);
  bool boolean_union_selected() { return boolean_combine_selected(BooleanOp::Union); }
  bool boolean_intersect_selected() { return boolean_combine_selected(BooleanOp::Intersection); }
  bool boolean_subtract_selected() { return boolean_combine_selected(BooleanOp::Difference); }
  bool boolean_xor_selected() { return boolean_combine_selected(BooleanOp::Xor); }
  bool merge_selected_figures();
  bool split_selected_figure();
  [[nodiscard]] const std::vector<core::NodeId>& selected_nodes() const noexcept { return selected_nodes_; }
  [[nodiscard]] size_t selected_count() const noexcept { return selected_nodes_.size(); }
  [[nodiscard]] bool can_merge_selected() const noexcept { return selected_nodes_.size() >= 2; }
  [[nodiscard]] bool can_split_selected() const noexcept;

  // Stacking Order (Z-Order) Operations
  bool bring_to_front_selected();
  bool send_to_back_selected();
  bool bring_forward_selected();
  bool send_backward_selected();
  void normalize_z_orders();
  [[nodiscard]] core::NodeId pick_topmost_node(const std::vector<core::NodeId>& hits, std::optional<core::Vec2d> click_pt = std::nullopt) const;

  // Figure Rotation & Angle Snapping
  void rotate_selected(double delta_deg);
  void set_selected_nodes_rotation(double absolute_deg);
  void set_node_rotation(core::NodeId id, double absolute_deg);
  [[nodiscard]] double get_node_rotation(core::NodeId id) const;

  void set_angle_snapping_enabled(bool enabled) noexcept {
    if (angle_snapping_enabled_ != enabled) {
      angle_snapping_enabled_ = enabled;
      emit angle_snap_toggled(enabled);
    }
  }
  [[nodiscard]] bool is_angle_snapping_enabled() const noexcept { return angle_snapping_enabled_; }
  void set_angle_snap_step(double step_deg) noexcept {
    step_deg = std::max(0.5, step_deg);
    if (std::abs(angle_snap_step_deg_ - step_deg) > 1e-4) {
      angle_snap_step_deg_ = step_deg;
      emit angle_snap_step_changed(step_deg);
    }
  }
  [[nodiscard]] double angle_snap_step() const noexcept { return angle_snap_step_deg_; }
  void set_cardinal_snap_priority(bool enabled) noexcept { cardinal_snap_priority_ = enabled; }
  [[nodiscard]] bool cardinal_snap_priority() const noexcept { return cardinal_snap_priority_; }

  // Quick Styling & Typography Operations
  void set_selected_nodes_fill_color(const std::string& color);
  void set_selected_nodes_stroke_color(const std::string& color);
  void set_selected_nodes_text_color(const std::string& color);
  void set_selected_nodes_stroke_width(double width);
  void set_selected_nodes_font_family(const std::string& family);
  void set_selected_nodes_font_size(int size);
  void set_selected_nodes_font_bold(bool bold);
  void set_selected_nodes_font_italic(bool italic);
  void set_selected_nodes_font_underline(bool underline);
  void reset_selected_nodes_style();

  void set_selected_nodes_fill_color(const char* color) { set_selected_nodes_fill_color(std::string(color)); }
  void set_selected_nodes_stroke_color(const char* color) { set_selected_nodes_stroke_color(std::string(color)); }
  void set_selected_nodes_text_color(const char* color) { set_selected_nodes_text_color(std::string(color)); }
  void set_selected_nodes_font_family(const char* family) { set_selected_nodes_font_family(std::string(family)); }

#ifdef VECTOR_ENABLE_QT
  void set_selected_nodes_fill_color(const QString& color) { set_selected_nodes_fill_color(color.toStdString()); }
  void set_selected_nodes_stroke_color(const QString& color) { set_selected_nodes_stroke_color(color.toStdString()); }
  void set_selected_nodes_text_color(const QString& color) { set_selected_nodes_text_color(color.toStdString()); }
  void set_selected_nodes_font_family(const QString& family) { set_selected_nodes_font_family(family.toStdString()); }
#endif

  // Hierarchy drill-down and navigation
  bool drill_down_selected();
  bool pop_hierarchy_scope();
  void reset_hierarchy(core::GraphModel* root_graph);
  void sync_all_hierarchy_symbols();
  [[nodiscard]] bool can_drill_down() const noexcept;
  [[nodiscard]] bool can_pop_scope() const noexcept;
  [[nodiscard]] bool is_inside_subsystem() const noexcept { return can_pop_scope(); }

  // Camera navigation
  void pan_by(core::Vec2d delta);
  void zoom_at(core::Vec2d screen_pivot, double zoom_factor);
  void reset_view();
  void fit_content();
  void set_zoom_level(double z);
  [[nodiscard]] double zoom_level() const noexcept { return zoom_level_; }

  // Display & Diagram Features
  void set_show_rulers(bool show) { show_rulers_ = show; update(); }
  [[nodiscard]] bool show_rulers() const noexcept { return show_rulers_; }

  struct DiagramStyle {
    uint32_t canvas_bg{0xFFEAEEF2};
    uint32_t page_bg{0xFFFFFFFF};
    uint32_t page_border{0xFFCBD5E1};
    uint32_t ruler_bg{0xFFF8F9FA};
    uint32_t ruler_fg{0xFF718096};
    uint32_t ruler_border{0xFFCBD5E1};
    uint32_t jump_bridge_color{0xFF2B6CB0};
    uint32_t guide_color{0xFF00BCD4};
    RenderStyle render_style;
  };

  void set_diagram_style(const DiagramStyle& style);
  [[nodiscard]] const DiagramStyle& diagram_style() const noexcept { return diagram_style_; }
  void apply_theme_to_entire_diagram(const DiagramStyle& style);
  void apply_theme_to_selection(const DiagramStyle& style);
  void reset_selection_to_defaults();
  void reset_all_to_defaults();
  void set_show_line_jumps(bool show) { show_line_jumps_ = show; update(); }
  [[nodiscard]] bool show_line_jumps() const noexcept { return show_line_jumps_; }
  void set_show_smart_guides(bool show) { show_smart_guides_ = show; update(); }
  [[nodiscard]] bool show_smart_guides() const noexcept { return show_smart_guides_; }
  [[nodiscard]] const std::vector<grid::SnapGuideLine>& active_smart_guides() const noexcept { return active_smart_guides_; }
  void set_page_bounds(core::Rect2d b) { page_bounds_ = b; update(); }
  [[nodiscard]] const core::Rect2d& page_bounds() const noexcept { return page_bounds_; }
  void set_show_page_sheet(bool show) { show_page_sheet_ = show; update(); }
  [[nodiscard]] bool show_page_sheet() const noexcept { return show_page_sheet_; }
  void set_page_size(double width, double height);
  void set_page_preset(PagePreset preset, PageOrientation orientation = PageOrientation::Landscape);
  void set_page_orientation(PageOrientation orientation);
  [[nodiscard]] PagePreset page_preset() const noexcept { return page_preset_; }
  [[nodiscard]] PageOrientation page_orientation() const noexcept { return page_orientation_; }
  void set_page_margin(double margin) { page_margin_ = std::max(0.0, margin); update(); }
  [[nodiscard]] double page_margin() const noexcept { return page_margin_; }
  core::Vec2d compute_smart_alignment(core::NodeId dragged_node, const core::Rect2d& proposed_bounds);
  void compute_resize_alignment(core::NodeId resizing_node, HandleType handle, core::Rect2d& in_out_bounds, bool from_center = false, bool keep_ratio = false);

  // Wire Crossing Styles & Node Junctions
  void set_wire_crossing_style(WireCrossingStyle style);
  [[nodiscard]] WireCrossingStyle wire_crossing_style() const noexcept { return wire_crossing_style_; }

  // Multipoint Arrows (1 Source to N Destinations)
  bool add_arrow_branch(core::EdgeId edge_id, core::NodeId target_node, core::PortId target_port = core::InvalidPortId);
  bool add_arrow_branch_to_selected(core::NodeId target_node, core::PortId target_port = core::InvalidPortId);
  void set_edge_branches_aligned(core::EdgeId edge_id, bool aligned);
  [[nodiscard]] bool is_edge_branches_aligned(core::EdgeId edge_id) const;
  void set_selected_edge_branches_aligned(bool aligned);

  // Auto-Routing of Arrows with Connect Points
  core::EdgeId auto_route_arrow(core::NodeId src_node, core::PortId src_port, core::NodeId dst_node, core::PortId dst_port);

  void start_inline_text_edit(core::NodeId id);
  void commit_inline_text_edit();

  [[nodiscard]] core::Vec2d screen_to_world(core::Vec2d screen_pt) const noexcept;
  [[nodiscard]] core::Vec2d world_to_screen(core::Vec2d world_pt) const noexcept;
  [[nodiscard]] core::Rect2d visible_world_frustum() const noexcept;

  void refresh_spatial_index();
  void render_frame_for_test() { render_canvas(); }

#ifdef VECTOR_ENABLE_QT
signals:
  void tool_mode_changed(ToolMode mode);
  void selection_changed();
  void diagram_modified();
  void hierarchy_scope_changed(const QString& breadcrumb);
  void status_message_requested(const QString& msg);
  void focus_console_requested();
  void cursor_position_changed(const QString& pos_str);
  void zoom_level_changed(double zoom);
  void ruler_context_menu_requested(const QPoint& global_pos);
  void snap_settings_requested();
  void snap_mode_toggled(bool enabled);
  void wire_crossing_style_changed(WireCrossingStyle style);
  void edge_branches_aligned_changed(core::EdgeId edge_id, bool aligned);
  void node_rotation_changed(core::NodeId id, double deg);
  void angle_snap_toggled(bool enabled);
  void angle_snap_step_changed(double step_deg);

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
#endif

private:
  void update_lod_state();
  void render_canvas();
  core::PortId find_hit_port(core::Vec2d world_pt, double tolerance, core::NodeId* out_node = nullptr) const;
  HandleType hit_test_handles(core::Vec2d screen_pt, core::NodeId* out_node = nullptr) const;
  void update_smart_alignment_guides(core::NodeId dragged_node);
  void finalize_instantiation();

  core::GraphModel* graph_model_{nullptr};
  core::ISpatialIndex* spatial_index_{nullptr};
  core::CommandHistory* command_history_{nullptr};
  grid::GridManager* grid_manager_{nullptr};
  core::HierarchyContext* hierarchy_context_{nullptr};

  std::unique_ptr<ICanvasRenderer> renderer_;

  // Active Tool Mode
  ToolMode tool_mode_{ToolMode::Select};

  // Display Options
  bool show_rulers_{true};
  bool show_page_sheet_{true};
  bool show_line_jumps_{true};
  bool show_smart_guides_{true};
  core::Rect2d page_bounds_{80.0, 60.0, 1100.0, 850.0};
  PagePreset page_preset_{PagePreset::Letter};
  PageOrientation page_orientation_{PageOrientation::Landscape};
  double page_margin_{25.0};
  DiagramStyle diagram_style_;

  // Camera viewport transforms
  core::Vec2d camera_offset_{0.0, 0.0};
  double zoom_level_{1.0};
  int viewport_width_{800};
  int viewport_height_{600};
  double dpr_{1.0};

  // Interactive interaction states
  enum class InteractionState {
    None,
    Panning,
    SelectingMarquee,
    DraggingNodes,
    ResizingNode,
    RotatingNode,
    WiringConnector,
    InstantiatingResize
  };

  InteractionState interaction_state_{InteractionState::None};
  core::Vec2d last_mouse_screen_{0.0, 0.0};
  core::Vec2d interaction_start_world_{0.0, 0.0};
  core::Rect2d selection_marquee_;
  std::vector<core::NodeId> selected_nodes_;
  core::EdgeId selected_edge_{core::InvalidEdgeId};
  std::unordered_map<core::NodeId, core::Rect2d> drag_initial_bounds_;
  core::Vec2d drag_accumulated_delta_{0.0, 0.0};

  // Clipboard cache
  std::vector<core::Node> clipboard_nodes_;
  std::vector<core::Port> clipboard_ports_;
  std::vector<core::Edge> clipboard_edges_;
  core::Vec2d clipboard_center_{0.0, 0.0};

  // Selection Manipulators & Handles
  HandleType active_handle_{HandleType::None};
  HandleType hovered_handle_{HandleType::None};
  core::NodeId active_handle_node_{core::InvalidNodeId};
  core::NodeId hovered_node_{core::InvalidNodeId};
  core::Rect2d resize_initial_bounds_;
  core::Vec2d resize_start_world_{0.0, 0.0};
  std::vector<grid::SnapGuideLine> active_smart_guides_;
  // Interactive Rotation & Angle Snapping
  double rotation_initial_deg_{0.0};
  core::Vec2d rotation_center_world_{0.0, 0.0};
  double current_rotating_angle_deg_{0.0};
  bool is_current_angle_snapped_{false};
  bool angle_snapping_enabled_{true};
  double angle_snap_step_deg_{15.0};
  bool cardinal_snap_priority_{true};
  double cardinal_snap_tolerance_deg_{4.0};
  core::Rect2d placement_preview_bounds_{0.0, 0.0, 0.0, 0.0};
  core::Vec2d instantiation_anchor_world_{0.0, 0.0};
  core::Vec2d instantiation_press_screen_{0.0, 0.0};
  bool instantiation_drag_active_{false};
  bool instantiation_two_click_active_{false};
  ShapePlacementTemplate active_template_;
  ShapePlacementTemplate drag_template_;
  bool is_drag_active_{false};
  int pending_polygon_sides_{6};
  std::vector<core::Vec2d> in_progress_polygon_points_;
  core::Vec2d polygon_cursor_world_{0.0, 0.0};

  // Inline text editing
#ifdef VECTOR_ENABLE_QT
  QLineEdit* inline_editor_{nullptr};
#endif
  core::NodeId editing_node_{core::InvalidNodeId};

  // Wiring state
  core::NodeId wiring_source_node_{core::InvalidNodeId};
  core::PortId wiring_source_port_{core::InvalidPortId};
  core::Vec2d wiring_current_target_{0.0, 0.0};
  bool wiring_click_origin_locked_{false};
  bool wiring_source_is_specific_port_{false};

  // Wire Crossing Style & Spacebar Pan
  WireCrossingStyle wire_crossing_style_{WireCrossingStyle::DotPriority};
  bool is_space_panning_{false};

  // LOD metrics
  LODLevel current_lod_{LODLevel::High};
  bool is_rapid_interacting_{false};

  // Hierarchical Subsystem graphs & scope tracking (keyed by unique subsystem_uuid)
  std::unordered_map<std::string, std::unique_ptr<core::GraphModel>> subsystem_graphs_;
  std::vector<core::NodeId> scope_node_stack_;

#ifdef VECTOR_ENABLE_QT
  QTimer lod_restore_timer_;
#endif
};

} // namespace vector::renderer

#endif // VECTOR_RENDERER_CANVAS_WIDGET_HPP
