// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/renderer/CanvasWidget.hpp"

#ifdef VECTOR_ENABLE_QT
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <tuple>
#include <limits>
#include <unordered_set>

#include "vector/io/DrawioParser.hpp"
#include "vector/io/StorageEngine.hpp"
#include "vector/core/SymbolGenerator.hpp"
#include <iostream>

namespace vector::renderer {

namespace {
inline QColor argb_to_qcolor(uint32_t c) {
  return QColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
}

inline std::string generate_subsystem_uuid() {
  static std::atomic<uint64_t> counter{1};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return "sub_" + std::to_string(now) + "_" + std::to_string(counter.fetch_add(1));
}
}

#ifdef VECTOR_ENABLE_QT
CanvasWidget::CanvasWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      renderer_(std::make_unique<CanvasRenderer>()) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAcceptDrops(true);

  inline_editor_ = new QLineEdit(this);
  inline_editor_->setVisible(false);
  inline_editor_->setAlignment(Qt::AlignCenter);
  inline_editor_->setStyleSheet(
      "QLineEdit { background: #FFFFFF; color: #1A202C; border: 2px solid #0078D4; border-radius: 3px; font-weight: bold; padding: 2px; }"
  );
  connect(inline_editor_, &QLineEdit::editingFinished, this, &CanvasWidget::commit_inline_text_edit);

  connect(&lod_restore_timer_, &QTimer::timeout, this, [this]() {
    is_rapid_interacting_ = false;
    current_lod_ = LODLevel::High;
    update();
  });
  lod_restore_timer_.setSingleShot(true);
}

CanvasWidget::~CanvasWidget() = default;

void CanvasWidget::initializeGL() {
  renderer_->initialize_gpu(nullptr);
}

void CanvasWidget::paintGL() {
  render_canvas();
}

void CanvasWidget::resizeGL(int w, int h) {
  viewport_width_ = w;
  viewport_height_ = h;
  dpr_ = devicePixelRatioF();
  renderer_->resize(w, h, dpr_);
}

void CanvasWidget::set_tool_mode(ToolMode mode) {
  tool_mode_ = mode;
  interaction_state_ = InteractionState::None;
  wiring_source_node_ = core::InvalidNodeId;
  wiring_source_port_ = core::InvalidPortId;
  in_progress_polygon_points_.clear();

  switch (mode) {
    case ToolMode::AddRectangle:
      active_template_ = {PlacementCategory::ShapeType, core::ShapeType::Rectangle, 0, {}, "Rectangle", 120.0, 70.0, true, "in", ""};
      break;
    case ToolMode::AddRoundedRect:
      active_template_ = {PlacementCategory::ShapeType, core::ShapeType::RoundedRect, 0, {}, "Rounded Rectangle", 120.0, 70.0, true, "in", ""};
      break;
    case ToolMode::AddEllipse:
      active_template_ = {PlacementCategory::ShapeType, core::ShapeType::Ellipse, 0, {}, "Ellipse", 120.0, 70.0, true, "in", ""};
      break;
    case ToolMode::AddHierarchicalBlock:
      active_template_ = {PlacementCategory::ShapeType, core::ShapeType::HierarchicalBlock, 0, {}, "Subsystem", 180.0, 100.0, true, "in", ""};
      break;
    case ToolMode::AddInputPort:
      active_template_ = {PlacementCategory::HierarchicalPort, core::ShapeType::InputPort, 0, {}, "in_port", 84.0, 28.0, true, "in", ""};
      break;
    case ToolMode::AddOutputPort:
      active_template_ = {PlacementCategory::HierarchicalPort, core::ShapeType::OutputPort, 0, {}, "out_port", 84.0, 28.0, false, "out", ""};
      break;
    default:
      break;
  }

  if (is_placement_tool_active()) {
    core::Vec2d mouse_screen = last_mouse_screen_;
    if (mouse_screen.x <= 0.0 && mouse_screen.y <= 0.0) {
      mouse_screen = {static_cast<double>(width()) * 0.5, static_cast<double>(height()) * 0.5};
    }
    const core::Vec2d world_pos = screen_to_world(mouse_screen);
    core::Rect2d cand_bounds = get_placement_bounds(mode, world_pos);
    const core::Vec2d snap_offset = compute_smart_alignment(core::InvalidNodeId, cand_bounds);
    cand_bounds.x += snap_offset.x;
    cand_bounds.y += snap_offset.y;
    placement_preview_bounds_ = cand_bounds;
  } else {
    placement_preview_bounds_ = {0.0, 0.0, 0.0, 0.0};
    active_smart_guides_.clear();
  }

  switch (mode) {
    case ToolMode::Select:
      setCursor(Qt::ArrowCursor);
      emit status_message_requested("Tool: Select (Click to select, drag to move, drag canvas for marquee)");
      break;
    case ToolMode::AddConnector:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Connector (Click a source port/shape and drag to target port)");
      break;
    case ToolMode::AddRectangle:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Rectangle Block (Move pointer to position with snap, click to place, Right-click/Esc to cancel)");
      break;
    case ToolMode::AddRoundedRect:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Rounded Rectangle (Move pointer to position with snap, click to place, Right-click/Esc to cancel)");
      break;
    case ToolMode::AddEllipse:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Ellipse (Move pointer to position with snap, click to place, Right-click/Esc to cancel)");
      break;
    case ToolMode::AddHierarchicalBlock:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Hierarchical Block (Move pointer to position with snap, click to place, Right-click/Esc to cancel)");
      break;
    case ToolMode::AddPolygon:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Polygon (Click canvas to place vertices. Right-click to finish, Escape to cancel)");
      break;
    case ToolMode::AddInputPort:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Input Port (Move pointer to position with snap, click to place, Right-click/Esc to cancel)");
      break;
    case ToolMode::AddOutputPort:
      setCursor(Qt::CrossCursor);
      emit status_message_requested("Tool: Output Port (Move pointer to position with snap, click to place, Right-click/Esc to cancel)");
      break;
    case ToolMode::AddCustomShape:
      setCursor(Qt::CrossCursor);
      emit status_message_requested(QString("Tool: Place %1 (Move pointer to position with snap, click to place, Right-click/Esc to cancel)").arg(QString::fromStdString(active_template_.name.empty() ? "Shape" : active_template_.name)));
      break;
  }
  emit tool_mode_changed(mode);
  update();
}

void CanvasWidget::start_shape_placement(const ShapePlacementTemplate& tmpl) {
  active_template_ = tmpl;
  set_tool_mode(ToolMode::AddCustomShape);
}

core::NodeId CanvasWidget::instantiate_template_at(const ShapePlacementTemplate& tmpl, core::Vec2d center_world) {
  core::NodeId nid = core::InvalidNodeId;
  switch (tmpl.category) {
    case PlacementCategory::RegularPolygon:
      nid = add_polygon(center_world, tmpl.polygon_sides, tmpl.name, false);
      break;

    case PlacementCategory::CustomPolygon:
      nid = add_custom_polygon_shape(center_world, tmpl.custom_vertices, tmpl.name, tmpl.width, tmpl.height, false);
      break;

    case PlacementCategory::HierarchicalPort:
      nid = add_hierarchical_port(center_world, tmpl.is_input_port, tmpl.port_type, false);
      break;

    case PlacementCategory::SubsystemBuiltin:
      nid = add_subsystem(center_world, tmpl.name, false);
      break;

    case PlacementCategory::SubsystemFile:
      nid = add_subsystem(center_world, tmpl.name, true, tmpl.file_path);
      break;

    case PlacementCategory::ShapeType:
    default:
      if (tmpl.shape_type == core::ShapeType::HierarchicalBlock) {
        nid = add_subsystem(center_world, tmpl.name, false);
      } else {
        nid = add_shape(tmpl.shape_type, center_world, tmpl.name, false);
      }
      break;
  }

  if (nid != core::InvalidNodeId && graph_model_) {
    if (auto* node = graph_model_->find_node(nid)) {
      if (tmpl.width > 0.0 && tmpl.height > 0.0) {
        node->bounds = {center_world.x - tmpl.width * 0.5, center_world.y - tmpl.height * 0.5, tmpl.width, tmpl.height};
      }
      node->set_symbol_anchor(tmpl.anchor_point);
      node->set_symbol_resize_point(tmpl.resize_point);
      if (spatial_index_) {
        spatial_index_->update(nid, node->bounds);
      }
      emit diagram_modified();
    }
  }

  return nid;
}

void CanvasWidget::finalize_instantiation() {
  if (!is_placement_tool_active()) return;

  core::Rect2d final_bounds = placement_preview_bounds_;
  if (!final_bounds.is_valid() || final_bounds.width <= 0.0) {
    final_bounds = get_placement_bounds(tool_mode_, instantiation_anchor_world_);
  }

  ShapePlacementTemplate tmpl = active_template_;
  tmpl.width = final_bounds.width;
  tmpl.height = final_bounds.height;

  instantiate_template_at(tmpl, final_bounds.center());

  cancel_placement();
}

core::PortId CanvasWidget::find_hit_port(core::Vec2d world_pt, double tolerance, core::NodeId* out_node) const {
  if (!graph_model_) return core::InvalidPortId;
  const double tol_sq = tolerance * tolerance;

  // 1. Direct port proximity check
  core::PortId best_port = core::InvalidPortId;
  double best_dist_sq = tol_sq;

  for (const auto& [pid, port] : graph_model_->ports()) {
    if (const auto* parent = graph_model_->find_node(port.parent_node)) {
      if (!parent->visible) continue;
      const core::Vec2d port_world = parent->resolve_port_position(port);
      const double d_sq = world_pt.distance_sq_to(port_world);
      if (d_sq < best_dist_sq) {
        best_dist_sq = d_sq;
        best_port = pid;
        if (out_node) *out_node = port.parent_node;
      }
    }
  }

  if (best_port != core::InvalidPortId) {
    return best_port;
  }

  // 2. If point is within a node boundary, find the closest port on that node!
  for (const auto& [nid, n] : graph_model_->nodes()) {
    if (!n.visible) continue;
    if (n.bounds.expanded(tolerance).contains(world_pt)) {
      double min_d = 1e18;
      for (core::PortId pid : n.port_ids) {
        if (const auto* p = graph_model_->find_port(pid)) {
          const core::Vec2d p_pos = n.resolve_port_position(*p);
          const double d = world_pt.distance_sq_to(p_pos);
          if (d < min_d) {
            min_d = d;
            best_port = pid;
            if (out_node) *out_node = nid;
          }
        }
      }
      if (best_port != core::InvalidPortId) {
        return best_port;
      }
    }
  }

  return core::InvalidPortId;
}

core::NodeId CanvasWidget::add_shape(core::ShapeType type, core::Vec2d world_pos, const std::string& name, bool snap_pos) {
  if (!graph_model_) return core::InvalidNodeId;

  if (snap_pos && grid_manager_ && grid_manager_->is_snap_enabled() && grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid)) {
    const auto snapped = grid_manager_->snap_to_grid(world_pos);
    const double snap_dist = grid_manager_->snap_tolerance() / std::max(0.2, zoom_level_);
    if (world_pos.distance_to(snapped) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_x() * 0.5) {
      world_pos = snapped;
    }
  }

  core::Node node;
  node.shape = type;
  if (!name.empty()) {
    node.name = name;
  } else {
    switch (type) {
      case core::ShapeType::Rectangle:
        node.name = "Block " + std::to_string(graph_model_->node_count() + 1);
        break;
      case core::ShapeType::RoundedRect:
        node.name = "Rounded " + std::to_string(graph_model_->node_count() + 1);
        break;
      case core::ShapeType::Ellipse:
        node.name = "State " + std::to_string(graph_model_->node_count() + 1);
        break;
      case core::ShapeType::HierarchicalBlock:
        node.name = "Subsystem " + std::to_string(graph_model_->node_count() + 1);
        break;
      case core::ShapeType::CustomPolygon:
        node.name = "Polygon " + std::to_string(graph_model_->node_count() + 1);
        node.set_regular_polygon(6);
        break;
      default:
        node.name = "Node " + std::to_string(graph_model_->node_count() + 1);
        break;
    }
  }

  const double w = (type == core::ShapeType::HierarchicalBlock) ? 180.0 : 120.0;
  const double h = (type == core::ShapeType::HierarchicalBlock) ? 100.0 : 70.0;
  node.bounds = {world_pos.x - w * 0.5, world_pos.y - h * 0.5, w, h};
  node.z_order = graph_model_->get_max_z_order() + 1;
  if (type == core::ShapeType::HierarchicalBlock) {
    node.set_subsystem_builtin();
    node.properties["subsystem_uuid"] = generate_subsystem_uuid();
  }

  const std::string node_name_copy = node.name;
  core::NodeId nid = core::InvalidNodeId;
  if (command_history_) {
    auto cmd = std::make_unique<core::AddNodeCommand>(*graph_model_, std::move(node));
    auto* cmd_ptr = cmd.get();
    command_history_->execute(std::move(cmd));
    nid = cmd_ptr->created_id();
  } else {
    nid = graph_model_->add_node(std::move(node));
  }

  // Attach 4 cardinal connection ports (North, East, South, West)
  const std::array<std::tuple<std::string, core::Vec2d, core::PortDirection>, 4> port_defs = {
    std::tuple{"N", core::Vec2d{0.5, 0.0}, core::PortDirection::North},
    std::tuple{"E", core::Vec2d{1.0, 0.5}, core::PortDirection::East},
    std::tuple{"S", core::Vec2d{0.5, 1.0}, core::PortDirection::South},
    std::tuple{"W", core::Vec2d{0.0, 0.5}, core::PortDirection::West}
  };

  for (const auto& [pname, anchor, dir] : port_defs) {
    core::Port p;
    p.parent_node = nid;
    p.name = pname;
    p.local_anchor = anchor;
    p.direction = dir;
    p.is_boundary_interface = (type == core::ShapeType::HierarchicalBlock);
    graph_model_->add_port(std::move(p));
  }

  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit status_message_requested(QString("Added %1 at (%2, %3)")
      .arg(QString::fromStdString(node_name_copy))
      .arg(static_cast<int>(world_pos.x))
      .arg(static_cast<int>(world_pos.y)));
  return nid;
}

core::NodeId CanvasWidget::add_polygon(core::Vec2d world_pos, int sides, const std::string& name, bool snap_pos) {
  if (!graph_model_) return core::InvalidNodeId;
  if (snap_pos && grid_manager_ && grid_manager_->is_snap_enabled() && grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid)) {
    const auto snapped = grid_manager_->snap_to_grid(world_pos);
    const double snap_dist = grid_manager_->snap_tolerance() / std::max(0.2, zoom_level_);
    if (world_pos.distance_to(snapped) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_x() * 0.5) {
      world_pos = snapped;
    }
  }
  core::Node node;
  node.shape = core::ShapeType::CustomPolygon;
  node.name = name.empty() ? ("Polygon " + std::to_string(graph_model_->node_count() + 1)) : name;
  node.set_regular_polygon(sides);
  const double w = 120.0;
  const double h = 100.0;
  node.bounds = {world_pos.x - w * 0.5, world_pos.y - h * 0.5, w, h};
  node.z_order = graph_model_->get_max_z_order() + 1;

  const std::string node_name_copy = node.name;
  core::NodeId nid = graph_model_->add_node(std::move(node));

  const std::array<std::tuple<std::string, core::Vec2d, core::PortDirection>, 4> port_defs = {
    std::tuple{"N", core::Vec2d{0.5, 0.0}, core::PortDirection::North},
    std::tuple{"E", core::Vec2d{1.0, 0.5}, core::PortDirection::East},
    std::tuple{"S", core::Vec2d{0.5, 1.0}, core::PortDirection::South},
    std::tuple{"W", core::Vec2d{0.0, 0.5}, core::PortDirection::West}
  };

  for (const auto& [pname, anchor, dir] : port_defs) {
    core::Port p;
    p.parent_node = nid;
    p.name = pname;
    p.local_anchor = anchor;
    p.direction = dir;
    graph_model_->add_port(std::move(p));
  }

  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit status_message_requested(QString("Added %1 (%2-gon) at (%3, %4)")
      .arg(QString::fromStdString(node_name_copy))
      .arg(sides)
      .arg(static_cast<int>(world_pos.x))
      .arg(static_cast<int>(world_pos.y)));
  return nid;
}

core::NodeId CanvasWidget::add_custom_polygon_shape(
    core::Vec2d world_pos,
    const std::vector<core::Vec2d>& normalized_vertices,
    const std::string& name,
    double width,
    double height,
    bool snap_pos) {
  if (!graph_model_) return core::InvalidNodeId;
  if (snap_pos && grid_manager_ && grid_manager_->is_snap_enabled() && grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid)) {
    const auto snapped = grid_manager_->snap_to_grid(world_pos);
    const double snap_dist = grid_manager_->snap_tolerance() / std::max(0.2, zoom_level_);
    if (world_pos.distance_to(snapped) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_x() * 0.5) {
      world_pos = snapped;
    }
  }
  core::Node node;
  node.shape = core::ShapeType::CustomPolygon;
  node.name = name.empty() ? ("Figure " + std::to_string(graph_model_->node_count() + 1)) : name;
  node.polygon_vertices = normalized_vertices;
  const double w = width > 0.0 ? width : 120.0;
  const double h = height > 0.0 ? height : 100.0;
  node.bounds = {world_pos.x - w * 0.5, world_pos.y - h * 0.5, w, h};
  node.z_order = graph_model_->get_max_z_order() + 1;

  const std::string node_name_copy = node.name;
  core::NodeId nid = graph_model_->add_node(std::move(node));

  const std::array<std::tuple<std::string, core::Vec2d, core::PortDirection>, 4> port_defs = {
    std::tuple{"N", core::Vec2d{0.5, 0.0}, core::PortDirection::North},
    std::tuple{"E", core::Vec2d{1.0, 0.5}, core::PortDirection::East},
    std::tuple{"S", core::Vec2d{0.5, 1.0}, core::PortDirection::South},
    std::tuple{"W", core::Vec2d{0.0, 0.5}, core::PortDirection::West}
  };

  for (const auto& [pname, anchor, dir] : port_defs) {
    core::Port p;
    p.parent_node = nid;
    p.name = pname;
    p.local_anchor = anchor;
    p.direction = dir;
    graph_model_->add_port(std::move(p));
  }

  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit status_message_requested(QString("Added %1 at (%2, %3)")
      .arg(QString::fromStdString(node_name_copy))
      .arg(static_cast<int>(world_pos.x))
      .arg(static_cast<int>(world_pos.y)));
  return nid;
}

void CanvasWidget::delete_selected() {
  if (!graph_model_ || (selected_nodes_.empty() && selected_edge_ == core::InvalidEdgeId)) return;

  std::vector<core::EdgeId> explicit_edges;
  if (selected_edge_ != core::InvalidEdgeId) {
    explicit_edges.push_back(selected_edge_);
  }

  if (command_history_) {
    command_history_->execute(std::make_unique<core::DeleteElementsCommand>(
        *graph_model_, selected_nodes_, explicit_edges));
  } else {
    core::DeleteElementsCommand cmd(*graph_model_, selected_nodes_, explicit_edges);
    cmd.execute();
  }

  selected_nodes_.clear();
  selected_edge_ = core::InvalidEdgeId;
  sync_current_subsystem_symbol();
  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit selection_changed();
  emit status_message_requested("Selection deleted.");
}

void CanvasWidget::select_all() {
  if (!graph_model_) return;
  selected_nodes_.clear();
  for (auto& [id, n] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
    n.selected = true;
    selected_nodes_.push_back(id);
  }
  update();
  emit selection_changed();
  emit status_message_requested(QString("Selected all %1 nodes.").arg(selected_nodes_.size()));
}

void CanvasWidget::clear_selection() {
  if (!graph_model_) return;
  for (auto& [id, n] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
    n.selected = false;
  }
  for (auto& [id, e] : const_cast<std::unordered_map<core::EdgeId, core::Edge>&>(graph_model_->edges())) {
    e.selected = false;
  }
  selected_nodes_.clear();
  selected_edge_ = core::InvalidEdgeId;
  update();
  emit selection_changed();
}

core::EdgeId CanvasWidget::find_hit_edge(core::Vec2d world_pt, double tolerance) const {
  if (!graph_model_) return core::InvalidEdgeId;

  auto point_to_segment_dist = [](core::Vec2d p, core::Vec2d a, core::Vec2d b) -> double {
    core::Vec2d ab = b - a;
    double len_sq = ab.length_sq();
    if (len_sq < 1e-8) {
      return (p - a).length();
    }
    double t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len_sq, 0.0, 1.0);
    core::Vec2d proj = a + ab * t;
    return (p - proj).length();
  };

  core::EdgeId best_edge = core::InvalidEdgeId;
  double best_dist = tolerance;

  for (const auto& [eid, edge] : graph_model_->edges()) {
    if (!edge.visible || edge.waypoints.size() < 2) continue;
    for (size_t i = 0; i + 1 < edge.waypoints.size(); ++i) {
      double dist = point_to_segment_dist(world_pt, edge.waypoints[i], edge.waypoints[i + 1]);
      if (dist <= best_dist) {
        best_dist = dist;
        best_edge = eid;
      }
    }
  }

  return best_edge;
}

void CanvasWidget::select_edge(core::EdgeId id, bool clear_previous) {
  if (clear_previous) {
    clear_selection();
  }
  selected_edge_ = id;
  if (graph_model_) {
    for (auto& [eid, e] : const_cast<std::unordered_map<core::EdgeId, core::Edge>&>(graph_model_->edges())) {
      e.selected = (eid == id);
    }
  }
  update();
  emit selection_changed();
  if (id != core::InvalidEdgeId) {
    emit status_message_requested(QString("Selected wire Net %1").arg(id));
  }
}

void CanvasWidget::set_wire_crossing_style(WireCrossingStyle style) {
  if (wire_crossing_style_ != style) {
    wire_crossing_style_ = style;
#ifdef VECTOR_ENABLE_QT
    emit wire_crossing_style_changed(style);
#endif
    update();
  }
}

namespace {
core::PortId find_closest_or_create_port(core::GraphModel& model, core::NodeId node_id, const core::Vec2d& target_pt) {
  const auto* node = model.find_node(node_id);
  if (!node) return core::InvalidPortId;

  // Check if an existing port is close
  core::PortId best_existing = core::InvalidPortId;
  double best_dist = std::numeric_limits<double>::max();
  for (core::PortId pid : node->port_ids) {
    if (const auto* p = model.find_port(pid)) {
      core::Vec2d pos = node->resolve_port_position(*p);
      double dist = (pos - target_pt).length();
      if (dist < best_dist) {
        best_dist = dist;
        best_existing = pid;
      }
    }
  }
  if (best_existing != core::InvalidPortId && best_dist < 25.0) {
    return best_existing;
  }

  // Determine direction based on vector from center to target_pt
  core::Vec2d center = node->bounds.center();
  double dx = target_pt.x - center.x;
  double dy = target_pt.y - center.y;

  core::PortDirection target_dir = core::PortDirection::East;
  core::Vec2d anchor = {1.0, 0.5};
  std::string name = "E";

  if (std::abs(dx) >= std::abs(dy)) {
    if (dx >= 0.0) {
      target_dir = core::PortDirection::East;
      anchor = {1.0, 0.5};
      name = "E";
    } else {
      target_dir = core::PortDirection::West;
      anchor = {0.0, 0.5};
      name = "W";
    }
  } else {
    if (dy >= 0.0) {
      target_dir = core::PortDirection::South;
      anchor = {0.5, 1.0};
      name = "S";
    } else {
      target_dir = core::PortDirection::North;
      anchor = {0.5, 0.0};
      name = "N";
    }
  }

  // Check if a port with that direction already exists
  for (core::PortId pid : node->port_ids) {
    if (const auto* p = model.find_port(pid)) {
      if (p->direction == target_dir) {
        return pid;
      }
    }
  }

  // Create new port
  core::Port p;
  p.parent_node = node_id;
  p.name = name;
  p.local_anchor = anchor;
  p.direction = target_dir;
  return model.add_port(std::move(p));
}
} // namespace

core::EdgeId CanvasWidget::auto_route_arrow(core::NodeId src_node, core::PortId src_port,
                                            core::NodeId dst_node, core::PortId dst_port) {
  if (!graph_model_) return core::InvalidEdgeId;
  const auto* s_node = graph_model_->find_node(src_node);
  const auto* d_node = graph_model_->find_node(dst_node);
  if (!s_node || !d_node) return core::InvalidEdgeId;

  if (src_port == core::InvalidPortId) {
    src_port = find_closest_or_create_port(*graph_model_, src_node, d_node->bounds.center());
  }
  if (dst_port == core::InvalidPortId) {
    dst_port = find_closest_or_create_port(*graph_model_, dst_node, s_node->bounds.center());
  }

  core::Edge edge;
  edge.name = "Arrow " + std::to_string(graph_model_->edge_count() + 1);
  edge.source_node = src_node;
  edge.source_port = src_port;
  edge.target_node = dst_node;
  edge.target_port = dst_port;
  edge.routing_style = core::RoutingStyle::Orthogonal;
  edge.arrowhead = "filled";
  edge.properties["arrowhead"] = "filled";

  graph_model_->update_edge_route(edge);

  core::EdgeId eid = core::InvalidEdgeId;
  if (command_history_) {
    auto cmd = std::make_unique<core::ConnectPortsCommand>(*graph_model_, std::move(edge));
    auto* raw_cmd = cmd.get();
    command_history_->execute(std::move(cmd));
    eid = raw_cmd->created_id();
    if (eid == core::InvalidEdgeId && !graph_model_->edges().empty()) {
      for (const auto& [e_id, _] : graph_model_->edges()) {
        eid = std::max(eid, e_id);
      }
    }
  } else {
    eid = graph_model_->add_edge(std::move(edge));
  }

  select_edge(eid, true);
  update();
#ifdef VECTOR_ENABLE_QT
  emit status_message_requested(QString("Auto-routed arrow: %1 -> %2")
      .arg(QString::fromStdString(s_node->name))
      .arg(QString::fromStdString(d_node->name)));
#endif
  return eid;
}

bool CanvasWidget::add_arrow_branch(core::EdgeId edge_id, core::NodeId target_node, core::PortId target_port) {
  if (!graph_model_) return false;
  auto* edge = graph_model_->find_edge(edge_id);
  const auto* d_node = graph_model_->find_node(target_node);
  if (!edge || !d_node) return false;

  if (target_port == core::InvalidPortId) {
    core::Vec2d trunk_center = edge->waypoints.empty() ? d_node->bounds.center() : edge->waypoints[edge->waypoints.size() / 2];
    target_port = find_closest_or_create_port(*graph_model_, target_node, trunk_center);
  }

  edge->add_destination(target_node, target_port, "filled");
  graph_model_->update_edge_route(*edge);

  update();
#ifdef VECTOR_ENABLE_QT
  emit status_message_requested(QString("Added multipoint branch on arrow Net %1 to node %2")
      .arg(edge_id)
      .arg(QString::fromStdString(d_node->name)));
#endif
  return true;
}

bool CanvasWidget::add_arrow_branch_to_selected(core::NodeId target_node, core::PortId target_port) {
  if (selected_edge_ == core::InvalidEdgeId) {
#ifdef VECTOR_ENABLE_QT
    emit status_message_requested("No arrow/wire selected to add branch to");
#endif
    return false;
  }
  return add_arrow_branch(selected_edge_, target_node, target_port);
}

void CanvasWidget::set_edge_branches_aligned(core::EdgeId edge_id, bool aligned) {
  if (!graph_model_) return;
  if (auto* edge = graph_model_->find_edge(edge_id)) {
    edge->set_branches_aligned(aligned);
    graph_model_->update_edge_route(*edge);
    update();
#ifdef VECTOR_ENABLE_QT
    emit edge_branches_aligned_changed(edge_id, aligned);
    emit status_message_requested(QString("Multipoint arrow Net %1 lines: %2")
        .arg(edge_id)
        .arg(aligned ? "ALIGNED (common bus spine)" : "INDEPENDENT (closest tap)"));
#endif
  }
}

bool CanvasWidget::is_edge_branches_aligned(core::EdgeId edge_id) const {
  if (!graph_model_) return true;
  if (const auto* edge = graph_model_->find_edge(edge_id)) {
    return edge->is_branches_aligned();
  }
  return true;
}

void CanvasWidget::set_selected_edge_branches_aligned(bool aligned) {
  if (selected_edge_ != core::InvalidEdgeId) {
    set_edge_branches_aligned(selected_edge_, aligned);
  }
}

void CanvasWidget::copy_selected() {
  if (!graph_model_ || selected_nodes_.empty()) return;

  clipboard_nodes_.clear();
  clipboard_ports_.clear();
  clipboard_edges_.clear();

  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();

  std::unordered_set<core::NodeId> node_set(selected_nodes_.begin(), selected_nodes_.end());

  for (core::NodeId nid : selected_nodes_) {
    if (const auto* n = graph_model_->find_node(nid)) {
      clipboard_nodes_.push_back(*n);
      min_x = std::min(min_x, n->bounds.x);
      min_y = std::min(min_y, n->bounds.y);
      max_x = std::max(max_x, n->bounds.x + n->bounds.width);
      max_y = std::max(max_y, n->bounds.y + n->bounds.height);

      for (core::PortId pid : n->port_ids) {
        if (const auto* p = graph_model_->find_port(pid)) {
          clipboard_ports_.push_back(*p);
        }
      }
    }
  }

  clipboard_center_ = {(min_x + max_x) * 0.5, (min_y + max_y) * 0.5};

  for (const auto& [eid, e] : graph_model_->edges()) {
    if (node_set.contains(e.source_node) && node_set.contains(e.target_node)) {
      clipboard_edges_.push_back(e);
    }
  }

  emit status_message_requested(QString("Copied %1 node(s) to clipboard.").arg(clipboard_nodes_.size()));
}

void CanvasWidget::cut_selected() {
  if (selected_nodes_.empty()) return;
  copy_selected();
  delete_selected();
  emit status_message_requested("Cut selection to clipboard.");
}

void CanvasWidget::paste_clipboard(core::Vec2d target_pos, bool use_target_pos) {
  if (!graph_model_ || clipboard_nodes_.empty()) return;

  core::Vec2d offset{25.0, 25.0};
  if (use_target_pos) {
    offset = target_pos - clipboard_center_;
  }

  auto cmd = std::make_unique<core::PasteElementsCommand>(
      *graph_model_, clipboard_nodes_, clipboard_ports_, clipboard_edges_, offset);

  if (!use_target_pos) {
    clipboard_center_.x += 25.0;
    clipboard_center_.y += 25.0;
    for (auto& n : clipboard_nodes_) {
      n.bounds.x += 25.0;
      n.bounds.y += 25.0;
    }
    for (auto& e : clipboard_edges_) {
      for (auto& wp : e.waypoints) {
        wp.x += 25.0;
        wp.y += 25.0;
      }
    }
  }

  auto* cmd_raw = cmd.get();
  if (command_history_) {
    command_history_->execute(std::move(cmd));
  } else {
    cmd_raw->execute();
  }

  clear_selection();
  selected_nodes_ = cmd_raw->created_nodes();
  for (core::NodeId nid : selected_nodes_) {
    if (auto* n = graph_model_->find_node(nid)) {
      n->selected = true;
    }
  }

  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit selection_changed();
  emit status_message_requested(QString("Pasted %1 node(s).").arg(selected_nodes_.size()));
}

void CanvasWidget::duplicate_selected() {
  if (!graph_model_ || selected_nodes_.empty()) return;

  std::vector<core::Node> nodes;
  std::vector<core::Port> ports;
  std::vector<core::Edge> edges;
  std::unordered_set<core::NodeId> node_set(selected_nodes_.begin(), selected_nodes_.end());

  for (core::NodeId nid : selected_nodes_) {
    if (const auto* n = graph_model_->find_node(nid)) {
      nodes.push_back(*n);
      for (core::PortId pid : n->port_ids) {
        if (const auto* p = graph_model_->find_port(pid)) {
          ports.push_back(*p);
        }
      }
    }
  }

  for (const auto& [eid, e] : graph_model_->edges()) {
    if (node_set.contains(e.source_node) && node_set.contains(e.target_node)) {
      edges.push_back(e);
    }
  }

  auto cmd = std::make_unique<core::PasteElementsCommand>(
      *graph_model_, std::move(nodes), std::move(ports), std::move(edges), core::Vec2d{25.0, 25.0});

  auto* cmd_raw = cmd.get();
  if (command_history_) {
    command_history_->execute(std::move(cmd));
  } else {
    cmd_raw->execute();
  }

  clear_selection();
  selected_nodes_ = cmd_raw->created_nodes();
  for (core::NodeId nid : selected_nodes_) {
    if (auto* n = graph_model_->find_node(nid)) {
      n->selected = true;
    }
  }

  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit selection_changed();
  emit status_message_requested(QString("Duplicated %1 node(s).").arg(selected_nodes_.size()));
}

void CanvasWidget::nudge_selected(core::Vec2d delta) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  if (command_history_) {
    command_history_->execute(std::make_unique<core::MoveNodesCommand>(*graph_model_, selected_nodes_, delta));
  } else {
    for (core::NodeId id : selected_nodes_) {
      if (auto* n = graph_model_->find_node(id)) {
        n->bounds.x += delta.x;
        n->bounds.y += delta.y;
      }
    }
  }
  graph_model_->update_all_edge_routes();
  refresh_spatial_index();
  update();
  emit diagram_modified();
}

bool CanvasWidget::can_split_selected() const noexcept {
  if (!graph_model_ || selected_nodes_.size() != 1) return false;
  const auto* node = graph_model_->find_node(selected_nodes_.front());
  if (!node) return false;
  auto it = node->properties.find("is_merged_figure");
  return it != node->properties.end() && it->second == "true";
}

bool CanvasWidget::can_drill_down() const noexcept {
  return selected_nodes_.size() == 1;
}

bool CanvasWidget::can_pop_scope() const noexcept {
  return hierarchy_context_ && hierarchy_context_->depth() > 1;
}

static QPainterPath node_to_world_path(const core::Node& node) {
  QPainterPath path;
  path.setFillRule(Qt::OddEvenFill);
  const auto& b = node.bounds;
  const QRectF rf(b.x, b.y, b.width, b.height);

  if (node.shape == core::ShapeType::Rectangle ||
      node.shape == core::ShapeType::HierarchicalBlock ||
      node.shape == core::ShapeType::InputPort ||
      node.shape == core::ShapeType::OutputPort) {
    path.addRect(rf);
  } else if (node.shape == core::ShapeType::RoundedRect) {
    const double rx = std::min(8.0, std::min(b.width, b.height) * 0.2);
    path.addRoundedRect(rf, rx, rx);
  } else if (node.shape == core::ShapeType::Ellipse) {
    path.addEllipse(rf);
  } else if (node.shape == core::ShapeType::CustomPolygon) {
    const auto world_contours = node.get_world_subpath_polygons();
    for (const auto& w_contour : world_contours) {
      if (w_contour.empty()) continue;
      QPolygonF poly;
      poly.reserve(static_cast<qsizetype>(w_contour.size()));
      for (const auto& pt : w_contour) {
        poly << QPointF(pt.x, pt.y);
      }
      path.addPolygon(poly);
      path.closeSubpath();
    }
  } else {
    path.addRect(rf);
  }
  return path;
}

bool CanvasWidget::boolean_combine_selected(BooleanOp op) {
  if (!graph_model_ || selected_nodes_.size() < 2) {
    emit status_message_requested("Select at least 2 figures to perform boolean operations.");
    return false;
  }

  std::vector<core::Node> children;
  std::vector<QPainterPath> node_paths;
  for (core::NodeId id : selected_nodes_) {
    const auto* node = graph_model_->find_node(id);
    if (!node) continue;
    children.push_back(*node);
    node_paths.push_back(node_to_world_path(*node));
  }

  if (children.size() < 2) {
    emit status_message_requested("Select at least 2 valid figures.");
    return false;
  }

  const char* op_name = "Union";
  const char* op_title = "Union (OR)";
  const char* op_prop = "union";
  switch (op) {
    case BooleanOp::Union:
      op_name = "Union";
      op_title = "Union (OR)";
      op_prop = "union";
      break;
    case BooleanOp::Intersection:
      op_name = "Intersection";
      op_title = "Intersection (AND)";
      op_prop = "intersection";
      break;
    case BooleanOp::Difference:
      op_name = "Difference";
      op_title = "Subtract (Difference)";
      op_prop = "difference";
      break;
    case BooleanOp::Xor:
      op_name = "XOR";
      op_title = "XOR (Symmetric Difference)";
      op_prop = "xor";
      break;
  }

  QPainterPath combined = node_paths.front();
  for (size_t i = 1; i < node_paths.size(); ++i) {
    switch (op) {
      case BooleanOp::Union:
        combined = combined.united(node_paths[i]);
        break;
      case BooleanOp::Intersection:
        combined = combined.intersected(node_paths[i]);
        break;
      case BooleanOp::Difference:
        combined = combined.subtracted(node_paths[i]);
        break;
      case BooleanOp::Xor:
        combined = combined.united(node_paths[i]).subtracted(combined.intersected(node_paths[i]));
        break;
    }
  }

  combined = combined.simplified();
  const QRectF b_rect = combined.boundingRect();
  if (combined.isEmpty() || b_rect.width() <= 1e-4 || b_rect.height() <= 1e-4) {
    emit status_message_requested(QString("Boolean %1 resulted in an empty shape (no overlapping surface).").arg(op_name));
    return false;
  }

  QList<QPolygonF> raw_subpaths = combined.toSubpathPolygons();
  if (raw_subpaths.isEmpty()) {
    raw_subpaths = combined.toFillPolygons();
  }

  auto calc_poly_area = [](const QPolygonF& p) -> double {
    if (p.size() < 3) return 0.0;
    double a = 0.0;
    const qsizetype n = p.size();
    for (qsizetype i = 0; i < n; ++i) {
      const qsizetype j = (i + 1) % n;
      a += p[i].x() * p[j].y() - p[j].x() * p[i].y();
    }
    return std::abs(a) * 0.5;
  };

  auto simplify_colinear = [](const QPolygonF& poly) -> QPolygonF {
    if (poly.size() < 3) return poly;
    QPolygonF snapped;
    snapped.reserve(poly.size());
    const qsizetype n = poly.size();
    for (qsizetype i = 0; i < n; ++i) {
      QPointF pt = poly[i];
      pt.setX(std::round(pt.x() * 10000.0) / 10000.0);
      pt.setY(std::round(pt.y() * 10000.0) / 10000.0);
      snapped.append(pt);
    }

    QPolygonF result;
    result.reserve(snapped.size());
    for (qsizetype i = 0; i < n; ++i) {
      const QPointF& prev = snapped[(i + n - 1) % n];
      const QPointF& cur  = snapped[i];
      const QPointF& next = snapped[(i + 1) % n];

      const double dx1 = cur.x() - prev.x();
      const double dy1 = cur.y() - prev.y();
      const double dx2 = next.x() - cur.x();
      const double dy2 = next.y() - cur.y();

      const double len1 = std::hypot(dx1, dy1);
      const double len2 = std::hypot(dx2, dy2);

      if (len1 < 1e-4 || len2 < 1e-4) continue;

      const double cross = std::abs(dx1 * dy2 - dy1 * dx2);
      const double dot   = dx1 * dx2 + dy1 * dy2;

      if ((cross / (len1 * len2)) < 1e-3 && dot > 0.0) {
        continue; // Drop redundant colinear point
      }
      result.append(cur);
    }
    return result.size() >= 3 ? result : snapped;
  };

  QList<QPolygonF> valid_subpaths;
  for (const auto& sp : raw_subpaths) {
    QPolygonF clean_sp = sp;
    if (clean_sp.size() >= 2 && clean_sp.first() == clean_sp.last()) {
      clean_sp.removeLast();
    }
    if (clean_sp.size() >= 3) {
      clean_sp = simplify_colinear(clean_sp);
    }
    if (clean_sp.size() >= 3 && calc_poly_area(clean_sp) > 1e-4) {
      valid_subpaths.append(clean_sp);
    }
  }

  if (valid_subpaths.isEmpty()) {
    emit status_message_requested(QString("Boolean %1 produced no valid closed polygon area.").arg(op_name));
    return false;
  }

  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();

  for (const auto& sp : valid_subpaths) {
    for (const auto& pt : sp) {
      min_x = std::min(min_x, pt.x());
      min_y = std::min(min_y, pt.y());
      max_x = std::max(max_x, pt.x());
      max_y = std::max(max_y, pt.y());
    }
  }
  const double w = max_x - min_x;
  const double h = max_y - min_y;
  if (w <= 1e-4 || h <= 1e-4) {
    emit status_message_requested("Boolean operation produced degenerate bounds.");
    return false;
  }

  const core::Rect2d union_bounds{min_x, min_y, w, h};

  std::vector<std::vector<core::Vec2d>> norm_subpaths;
  norm_subpaths.reserve(valid_subpaths.size());
  double max_area = -1.0;
  size_t primary_idx = 0;

  for (size_t i = 0; i < static_cast<size_t>(valid_subpaths.size()); ++i) {
    const auto& sp = valid_subpaths[static_cast<qsizetype>(i)];
    const double a = calc_poly_area(sp);
    if (a > max_area) {
      max_area = a;
      primary_idx = i;
    }
    std::vector<core::Vec2d> norm_pts;
    norm_pts.reserve(sp.size());
    for (const auto& pt : sp) {
      const double nx = (pt.x() - min_x) / w;
      const double ny = (pt.y() - min_y) / h;
      norm_pts.push_back({std::clamp(nx, 0.0, 1.0), std::clamp(ny, 0.0, 1.0)});
    }
    norm_subpaths.push_back(std::move(norm_pts));
  }

  core::Node merged;
  merged.name = QString("%1 (%2 figures)").arg(op_name).arg(children.size()).toStdString();
  merged.bounds = union_bounds;
  merged.shape = core::ShapeType::CustomPolygon;
  merged.polygon_vertices = norm_subpaths[primary_idx];
  merged.subpath_polygons = norm_subpaths;
  merged.properties["is_merged_figure"] = "true";
  merged.properties["boolean_op"] = op_prop;
  merged.properties["child_count"] = std::to_string(children.size());

  // Inherit appearance from primary constituent figure
  if (auto it = children[0].properties.find("fill_color"); it != children[0].properties.end()) {
    merged.properties["fill_color"] = it->second;
  }
  if (auto it = children[0].properties.find("stroke_color"); it != children[0].properties.end()) {
    merged.properties["stroke_color"] = it->second;
  }
  if (auto it = children[0].properties.find("stroke_width"); it != children[0].properties.end()) {
    merged.properties["stroke_width"] = it->second;
  }
  if (auto it = children[0].properties.find("text_color"); it != children[0].properties.end()) {
    merged.properties["text_color"] = it->second;
  }

  for (size_t i = 0; i < children.size(); ++i) {
    const std::string prefix = "child_" + std::to_string(i) + "_";
    merged.properties[prefix + "name"] = children[i].name;
    merged.properties[prefix + "rel_x"] = std::to_string(children[i].bounds.x - union_bounds.x);
    merged.properties[prefix + "rel_y"] = std::to_string(children[i].bounds.y - union_bounds.y);
    merged.properties[prefix + "w"] = std::to_string(children[i].bounds.width);
    merged.properties[prefix + "h"] = std::to_string(children[i].bounds.height);
    merged.properties[prefix + "shape"] = std::to_string(static_cast<int>(children[i].shape));
    if (!children[i].polygon_vertices.empty()) {
      std::string poly_str;
      for (const auto& v : children[i].polygon_vertices) {
        poly_str += std::to_string(v.x) + "," + std::to_string(v.y) + ";";
      }
      merged.properties[prefix + "poly"] = poly_str;
    }
    if (!children[i].subpath_polygons.empty()) {
      std::string subpaths_str;
      for (size_t s = 0; s < children[i].subpath_polygons.size(); ++s) {
        if (s > 0) subpaths_str += "|";
        for (const auto& v : children[i].subpath_polygons[s]) {
          subpaths_str += std::to_string(v.x) + "," + std::to_string(v.y) + ";";
        }
      }
      merged.properties[prefix + "subpaths"] = subpaths_str;
    }
  }

  if (command_history_) {
    command_history_->begin_transaction(std::string("Boolean ") + op_title);
  }

  for (core::NodeId id : selected_nodes_) {
    graph_model_->remove_node(id);
  }

  core::NodeId merged_id = graph_model_->add_node(std::move(merged));

  const std::array<std::tuple<std::string, core::Vec2d, core::PortDirection>, 4> port_defs = {
    std::tuple{"N", core::Vec2d{0.5, 0.0}, core::PortDirection::North},
    std::tuple{"E", core::Vec2d{1.0, 0.5}, core::PortDirection::East},
    std::tuple{"S", core::Vec2d{0.5, 1.0}, core::PortDirection::South},
    std::tuple{"W", core::Vec2d{0.0, 0.5}, core::PortDirection::West}
  };
  for (const auto& [pname, anchor, dir] : port_defs) {
    core::Port p;
    p.parent_node = merged_id;
    p.name = pname;
    p.local_anchor = anchor;
    p.direction = dir;
    graph_model_->add_port(std::move(p));
  }

  if (command_history_) {
    command_history_->commit();
  }

  selected_nodes_ = {merged_id};
  if (auto* n = graph_model_->find_node(merged_id)) {
    n->selected = true;
  }

  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit selection_changed();
  emit status_message_requested(QString("Created %1 from %2 figures.").arg(op_name).arg(children.size()));
  return true;
}

bool CanvasWidget::merge_selected_figures() {
  return boolean_combine_selected(BooleanOp::Union);
}

bool CanvasWidget::split_selected_figure() {
  if (!graph_model_ || selected_nodes_.size() != 1) {
    emit status_message_requested("Select a single merged composite figure to split.");
    return false;
  }

  const core::NodeId mid = selected_nodes_.front();
  auto* merged_node = graph_model_->find_node(mid);
  if (!merged_node) return false;

  auto it_merged = merged_node->properties.find("is_merged_figure");
  if (it_merged == merged_node->properties.end() || it_merged->second != "true") {
    emit status_message_requested("Selected figure is not a merged group.");
    return false;
  }

  const int count = std::stoi(merged_node->properties["child_count"]);
  const core::Rect2d cur_bounds = merged_node->bounds;

  if (command_history_) {
    command_history_->begin_transaction("Split Figure");
  }

  std::vector<core::NodeId> restored_ids;
  for (int i = 0; i < count; ++i) {
    const std::string prefix = "child_" + std::to_string(i) + "_";
    core::Node child;
    child.name = merged_node->properties[prefix + "name"];
    const double rel_x = std::stod(merged_node->properties[prefix + "rel_x"]);
    const double rel_y = std::stod(merged_node->properties[prefix + "rel_y"]);
    const double w = std::stod(merged_node->properties[prefix + "w"]);
    const double h = std::stod(merged_node->properties[prefix + "h"]);
    child.bounds = {cur_bounds.x + rel_x, cur_bounds.y + rel_y, w, h};
    child.shape = static_cast<core::ShapeType>(std::stoi(merged_node->properties[prefix + "shape"]));
    child.selected = true;

    auto it_poly = merged_node->properties.find(prefix + "poly");
    if (it_poly != merged_node->properties.end() && !it_poly->second.empty()) {
      std::stringstream ss(it_poly->second);
      std::string token;
      while (std::getline(ss, token, ';')) {
        if (token.empty()) continue;
        auto comma = token.find(',');
        if (comma != std::string::npos) {
          double vx = std::stod(token.substr(0, comma));
          double vy = std::stod(token.substr(comma + 1));
          child.polygon_vertices.push_back({vx, vy});
        }
      }
    }

    auto it_sub = merged_node->properties.find(prefix + "subpaths");
    if (it_sub != merged_node->properties.end() && !it_sub->second.empty()) {
      std::stringstream ss_subs(it_sub->second);
      std::string contour_str;
      while (std::getline(ss_subs, contour_str, '|')) {
        if (contour_str.empty()) continue;
        std::stringstream ss_c(contour_str);
        std::string token;
        std::vector<core::Vec2d> contour_pts;
        while (std::getline(ss_c, token, ';')) {
          if (token.empty()) continue;
          auto comma = token.find(',');
          if (comma != std::string::npos) {
            double vx = std::stod(token.substr(0, comma));
            double vy = std::stod(token.substr(comma + 1));
            contour_pts.push_back({vx, vy});
          }
        }
        if (!contour_pts.empty()) {
          child.subpath_polygons.push_back(std::move(contour_pts));
        }
      }
    }

    core::NodeId cid = graph_model_->add_node(std::move(child));

    const std::array<std::tuple<std::string, core::Vec2d, core::PortDirection>, 4> port_defs = {
      std::tuple{"N", core::Vec2d{0.5, 0.0}, core::PortDirection::North},
      std::tuple{"E", core::Vec2d{1.0, 0.5}, core::PortDirection::East},
      std::tuple{"S", core::Vec2d{0.5, 1.0}, core::PortDirection::South},
      std::tuple{"W", core::Vec2d{0.0, 0.5}, core::PortDirection::West}
    };
    for (const auto& [pname, anchor, dir] : port_defs) {
      core::Port p;
      p.parent_node = cid;
      p.name = pname;
      p.local_anchor = anchor;
      p.direction = dir;
      graph_model_->add_port(std::move(p));
    }

    restored_ids.push_back(cid);
  }

  graph_model_->remove_node(mid);

  if (command_history_) {
    command_history_->commit();
  }

  selected_nodes_ = restored_ids;
  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit selection_changed();
  emit status_message_requested(QString("Split figure into %1 individual figures.").arg(restored_ids.size()));
  return true;
}

core::NodeId CanvasWidget::pick_topmost_node(const std::vector<core::NodeId>& hits, std::optional<core::Vec2d> click_pt) const {
  if (hits.empty() || !graph_model_) return core::InvalidNodeId;
  core::NodeId best_id = core::InvalidNodeId;
  int32_t best_z = std::numeric_limits<int32_t>::min();
  for (core::NodeId id : hits) {
    if (const auto* n = graph_model_->find_node(id)) {
      if (click_pt.has_value() && !n->contains_point(*click_pt)) {
        continue;
      }
      if (n->z_order > best_z || (n->z_order == best_z && n->id > best_id)) {
        best_z = n->z_order;
        best_id = id;
      }
    }
  }
  if (best_id == core::InvalidNodeId && !hits.empty() && !click_pt.has_value()) {
    return hits.front();
  }
  return best_id;
}

void CanvasWidget::normalize_z_orders() {
  if (!graph_model_ || graph_model_->nodes().empty()) return;

  std::vector<core::Node*> sorted_nodes;
  sorted_nodes.reserve(graph_model_->node_count());
  for (auto& [id, node] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
    sorted_nodes.push_back(&node);
  }
  std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const core::Node* a, const core::Node* b) {
    if (a->z_order != b->z_order) return a->z_order < b->z_order;
    return a->id < b->id;
  });

  for (size_t i = 0; i < sorted_nodes.size(); ++i) {
    sorted_nodes[i]->z_order = static_cast<int32_t>(i + 1);
  }
}

bool CanvasWidget::bring_to_front_selected() {
  if (!graph_model_ || selected_nodes_.empty()) return false;

  std::vector<core::Node*> sorted_nodes;
  sorted_nodes.reserve(graph_model_->node_count());
  for (auto& [id, node] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
    sorted_nodes.push_back(&node);
  }
  std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const core::Node* a, const core::Node* b) {
    if (a->z_order != b->z_order) return a->z_order < b->z_order;
    return a->id < b->id;
  });

  std::unordered_set<core::NodeId> sel_set(selected_nodes_.begin(), selected_nodes_.end());
  std::vector<core::Node*> unselected;
  std::vector<core::Node*> selected;
  for (auto* n : sorted_nodes) {
    if (sel_set.find(n->id) != sel_set.end()) {
      selected.push_back(n);
    } else {
      unselected.push_back(n);
    }
  }

  std::vector<core::Node*> reordered = std::move(unselected);
  reordered.insert(reordered.end(), selected.begin(), selected.end());

  std::vector<std::pair<core::NodeId, int32_t>> old_z;
  std::vector<std::pair<core::NodeId, int32_t>> new_z;
  bool changed = false;

  for (size_t i = 0; i < reordered.size(); ++i) {
    const int32_t target_z = static_cast<int32_t>(i + 1);
    if (reordered[i]->z_order != target_z) {
      changed = true;
    }
    old_z.emplace_back(reordered[i]->id, reordered[i]->z_order);
    new_z.emplace_back(reordered[i]->id, target_z);
  }

  if (!changed) return true;

  if (command_history_) {
    auto cmd = std::make_unique<core::ChangeZOrderCommand>(
        *graph_model_, std::move(old_z), std::move(new_z), "Bring to Front");
    command_history_->execute(std::move(cmd));
  } else {
    for (const auto& [id, z] : new_z) {
      if (auto* n = graph_model_->find_node(id)) {
        n->z_order = z;
      }
    }
  }

#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
  emit status_message_requested("Brought selected shape(s) to front.");
#endif
  return true;
}

bool CanvasWidget::send_to_back_selected() {
  if (!graph_model_ || selected_nodes_.empty()) return false;

  std::vector<core::Node*> sorted_nodes;
  sorted_nodes.reserve(graph_model_->node_count());
  for (auto& [id, node] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
    sorted_nodes.push_back(&node);
  }
  std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const core::Node* a, const core::Node* b) {
    if (a->z_order != b->z_order) return a->z_order < b->z_order;
    return a->id < b->id;
  });

  std::unordered_set<core::NodeId> sel_set(selected_nodes_.begin(), selected_nodes_.end());
  std::vector<core::Node*> unselected;
  std::vector<core::Node*> selected;
  for (auto* n : sorted_nodes) {
    if (sel_set.find(n->id) != sel_set.end()) {
      selected.push_back(n);
    } else {
      unselected.push_back(n);
    }
  }

  std::vector<core::Node*> reordered = std::move(selected);
  reordered.insert(reordered.end(), unselected.begin(), unselected.end());

  std::vector<std::pair<core::NodeId, int32_t>> old_z;
  std::vector<std::pair<core::NodeId, int32_t>> new_z;
  bool changed = false;

  for (size_t i = 0; i < reordered.size(); ++i) {
    const int32_t target_z = static_cast<int32_t>(i + 1);
    if (reordered[i]->z_order != target_z) {
      changed = true;
    }
    old_z.emplace_back(reordered[i]->id, reordered[i]->z_order);
    new_z.emplace_back(reordered[i]->id, target_z);
  }

  if (!changed) return true;

  if (command_history_) {
    auto cmd = std::make_unique<core::ChangeZOrderCommand>(
        *graph_model_, std::move(old_z), std::move(new_z), "Send to Back");
    command_history_->execute(std::move(cmd));
  } else {
    for (const auto& [id, z] : new_z) {
      if (auto* n = graph_model_->find_node(id)) {
        n->z_order = z;
      }
    }
  }

#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
  emit status_message_requested("Sent selected shape(s) to back.");
#endif
  return true;
}

bool CanvasWidget::bring_forward_selected() {
  if (!graph_model_ || selected_nodes_.empty()) return false;

  std::vector<core::Node*> sorted_nodes;
  sorted_nodes.reserve(graph_model_->node_count());
  for (auto& [id, node] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
    sorted_nodes.push_back(&node);
  }
  std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const core::Node* a, const core::Node* b) {
    if (a->z_order != b->z_order) return a->z_order < b->z_order;
    return a->id < b->id;
  });

  std::unordered_set<core::NodeId> sel_set(selected_nodes_.begin(), selected_nodes_.end());
  bool changed = false;

  if (sorted_nodes.size() >= 2) {
    for (size_t i = sorted_nodes.size() - 1; i > 0; --i) {
      size_t curr = i - 1;
      size_t above = i;
      if (sel_set.find(sorted_nodes[curr]->id) != sel_set.end() &&
          sel_set.find(sorted_nodes[above]->id) == sel_set.end()) {
        std::swap(sorted_nodes[curr], sorted_nodes[above]);
        changed = true;
      }
    }
  }

  if (!changed) return true;

  std::vector<std::pair<core::NodeId, int32_t>> old_z;
  std::vector<std::pair<core::NodeId, int32_t>> new_z;

  for (size_t i = 0; i < sorted_nodes.size(); ++i) {
    const int32_t target_z = static_cast<int32_t>(i + 1);
    old_z.emplace_back(sorted_nodes[i]->id, sorted_nodes[i]->z_order);
    new_z.emplace_back(sorted_nodes[i]->id, target_z);
  }

  if (command_history_) {
    auto cmd = std::make_unique<core::ChangeZOrderCommand>(
        *graph_model_, std::move(old_z), std::move(new_z), "Bring Forward");
    command_history_->execute(std::move(cmd));
  } else {
    for (const auto& [id, z] : new_z) {
      if (auto* n = graph_model_->find_node(id)) {
        n->z_order = z;
      }
    }
  }

#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
  emit status_message_requested("Brought selected shape(s) forward.");
#endif
  return true;
}

bool CanvasWidget::send_backward_selected() {
  if (!graph_model_ || selected_nodes_.empty()) return false;

  std::vector<core::Node*> sorted_nodes;
  sorted_nodes.reserve(graph_model_->node_count());
  for (auto& [id, node] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
    sorted_nodes.push_back(&node);
  }
  std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const core::Node* a, const core::Node* b) {
    if (a->z_order != b->z_order) return a->z_order < b->z_order;
    return a->id < b->id;
  });

  std::unordered_set<core::NodeId> sel_set(selected_nodes_.begin(), selected_nodes_.end());
  bool changed = false;

  for (size_t i = 1; i < sorted_nodes.size(); ++i) {
    size_t below = i - 1;
    size_t curr = i;
    if (sel_set.find(sorted_nodes[curr]->id) != sel_set.end() &&
        sel_set.find(sorted_nodes[below]->id) == sel_set.end()) {
      std::swap(sorted_nodes[curr], sorted_nodes[below]);
      changed = true;
    }
  }

  if (!changed) return true;

  std::vector<std::pair<core::NodeId, int32_t>> old_z;
  std::vector<std::pair<core::NodeId, int32_t>> new_z;

  for (size_t i = 0; i < sorted_nodes.size(); ++i) {
    const int32_t target_z = static_cast<int32_t>(i + 1);
    old_z.emplace_back(sorted_nodes[i]->id, sorted_nodes[i]->z_order);
    new_z.emplace_back(sorted_nodes[i]->id, target_z);
  }

  if (command_history_) {
    auto cmd = std::make_unique<core::ChangeZOrderCommand>(
        *graph_model_, std::move(old_z), std::move(new_z), "Send Backward");
    command_history_->execute(std::move(cmd));
  } else {
    for (const auto& [id, z] : new_z) {
      if (auto* n = graph_model_->find_node(id)) {
        n->z_order = z;
      }
    }
  }

#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
  emit status_message_requested("Sent selected shape(s) backward.");
#endif
  return true;
}

void CanvasWidget::rotate_selected(double delta_deg) {
  if (!graph_model_ || selected_nodes_.empty()) return;

  std::vector<std::pair<core::NodeId, double>> old_rots;
  std::vector<std::pair<core::NodeId, double>> new_rots;
  old_rots.reserve(selected_nodes_.size());
  new_rots.reserve(selected_nodes_.size());

  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      const double old_deg = n->rotation_deg;
      double new_deg = std::fmod(old_deg + delta_deg, 360.0);
      if (new_deg < 0.0) new_deg += 360.0;
      old_rots.emplace_back(id, old_deg);
      new_rots.emplace_back(id, new_deg);
      n->rotation_deg = new_deg;
#ifdef VECTOR_ENABLE_QT
      emit node_rotation_changed(id, new_deg);
#endif
    }
  }

  if (command_history_ && !old_rots.empty()) {
    command_history_->execute(std::make_unique<core::RotateNodesCommand>(
        *graph_model_, std::move(old_rots), std::move(new_rots)));
  }

  graph_model_->update_all_edge_routes();
  refresh_spatial_index();
#ifdef VECTOR_ENABLE_QT
  emit diagram_modified();
  update();
#endif
}

void CanvasWidget::set_selected_nodes_rotation(double absolute_deg) {
  if (!graph_model_ || selected_nodes_.empty()) return;

  double target_deg = std::fmod(absolute_deg, 360.0);
  if (target_deg < 0.0) target_deg += 360.0;

  std::vector<std::pair<core::NodeId, double>> old_rots;
  std::vector<std::pair<core::NodeId, double>> new_rots;
  old_rots.reserve(selected_nodes_.size());
  new_rots.reserve(selected_nodes_.size());

  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      if (std::abs(n->rotation_deg - target_deg) > 1e-4) {
        old_rots.emplace_back(id, n->rotation_deg);
        new_rots.emplace_back(id, target_deg);
        n->rotation_deg = target_deg;
#ifdef VECTOR_ENABLE_QT
        emit node_rotation_changed(id, target_deg);
#endif
      }
    }
  }

  if (command_history_ && !old_rots.empty()) {
    command_history_->execute(std::make_unique<core::RotateNodesCommand>(
        *graph_model_, std::move(old_rots), std::move(new_rots)));
  }

  graph_model_->update_all_edge_routes();
  refresh_spatial_index();
#ifdef VECTOR_ENABLE_QT
  emit diagram_modified();
  update();
#endif
}

void CanvasWidget::set_node_rotation(core::NodeId id, double absolute_deg) {
  if (!graph_model_) return;
  auto* n = graph_model_->find_node(id);
  if (!n) return;

  double target_deg = std::fmod(absolute_deg, 360.0);
  if (target_deg < 0.0) target_deg += 360.0;

  if (std::abs(n->rotation_deg - target_deg) < 1e-4) return;

  const double old_deg = n->rotation_deg;
  n->rotation_deg = target_deg;

  if (command_history_) {
    command_history_->execute(std::make_unique<core::RotateNodeCommand>(
        *graph_model_, id, old_deg, target_deg));
  }

  graph_model_->update_all_edge_routes();
  refresh_spatial_index();
#ifdef VECTOR_ENABLE_QT
  emit node_rotation_changed(id, target_deg);
  emit diagram_modified();
  update();
#endif
}

double CanvasWidget::get_node_rotation(core::NodeId id) const {
  if (!graph_model_) return 0.0;
  if (const auto* n = graph_model_->find_node(id)) {
    return n->rotation_deg;
  }
  return 0.0;
}

void CanvasWidget::set_selected_nodes_fill_color(const std::string& color) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["fill_color"] = color;
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_stroke_color(const std::string& color) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["stroke_color"] = color;
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_text_color(const std::string& color) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["text_color"] = color;
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_stroke_width(double width) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  std::ostringstream oss;
  oss << width;
  const std::string width_str = oss.str();
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["stroke_width"] = width_str;
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_font_family(const std::string& family) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["font_family"] = family;
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_font_size(int size) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["font_size"] = std::to_string(size);
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_font_bold(bool bold) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["font_bold"] = bold ? "true" : "false";
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_font_italic(bool italic) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["font_italic"] = italic ? "true" : "false";
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::set_selected_nodes_font_underline(bool underline) {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties["font_underline"] = underline ? "true" : "false";
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

void CanvasWidget::reset_selected_nodes_style() {
  if (!graph_model_ || selected_nodes_.empty()) return;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->properties.erase("fill_color");
      n->properties.erase("stroke_color");
      n->properties.erase("text_color");
      n->properties.erase("stroke_width");
      n->properties.erase("font_family");
      n->properties.erase("font_size");
      n->properties.erase("font_bold");
      n->properties.erase("font_italic");
      n->properties.erase("font_underline");
    }
  }
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
#endif
}

bool CanvasWidget::drill_down_selected() {
  if (!hierarchy_context_ || !graph_model_ || selected_nodes_.size() != 1) {
    emit status_message_requested("Select a figure or block to drill down into.");
    return false;
  }
  return drill_down_node(selected_nodes_.front());
}

core::GraphModel* CanvasWidget::get_subsystem_graph(core::NodeId nid) {
  if (!graph_model_) return nullptr;
  auto* node = graph_model_->find_node(nid);
  if (!node) return nullptr;
  auto it_id = node->properties.find("subsystem_uuid");
  if (it_id == node->properties.end() || it_id->second.empty()) return nullptr;
  auto it = subsystem_graphs_.find(it_id->second);
  return (it != subsystem_graphs_.end()) ? it->second.get() : nullptr;
}

core::GraphModel* CanvasWidget::ensure_subsystem_graph(core::NodeId nid) {
  if (!graph_model_) return nullptr;
  auto* node = graph_model_->find_node(nid);
  if (!node) return nullptr;

  std::string uuid = node->properties["subsystem_uuid"];
  if (uuid.empty()) {
    uuid = generate_subsystem_uuid();
    node->properties["subsystem_uuid"] = uuid;
  }

  auto it_cached = subsystem_graphs_.find(uuid);
  if (it_cached != subsystem_graphs_.end() && (it_cached->second->node_count() > 0 || node->port_ids.empty())) {
    return it_cached->second.get();
  }

  const bool is_file = node->is_file_subsystem();
  const std::string file_path = node->subsystem_file();
  auto child_graph = std::make_unique<core::GraphModel>();
  bool loaded_from_source = false;

  if (is_file && !file_path.empty()) {
    std::ifstream in(file_path);
    if (in.is_open()) {
      std::string content((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
      if (file_path.ends_with(".drawio") || file_path.ends_with(".xml")) {
        io::DrawioParser parser;
        if (parser.import_xml(content, *child_graph).success) {
          loaded_from_source = true;
        }
      } else {
        io::StorageEngine storage;
        if (storage.deserialize_yaml(content, *child_graph).success) {
          loaded_from_source = true;
        }
      }
    }
  } else {
    auto it_embed = node->properties.find("embedded_graph_yaml");
    if (it_embed != node->properties.end() && !it_embed->second.empty()) {
      io::StorageEngine storage;
      if (storage.deserialize_yaml(it_embed->second, *child_graph).success) {
        loaded_from_source = true;
      }
    }
  }

  if (!loaded_from_source) {
    // Subsystem internal diagram starts empty: Center all new ports in the sheet edges
    std::vector<const core::Port*> in_ports;
    std::vector<const core::Port*> out_ports;
    std::unordered_set<core::PortId> seen_pids;
    for (core::PortId pid : node->port_ids) {
      if (!seen_pids.insert(pid).second) continue;
      if (const auto* parent_p = graph_model_->find_port(pid)) {
        const bool is_in = (parent_p->pin_type == "input" || parent_p->direction == core::PortDirection::West || parent_p->local_anchor.x <= 0.5);
        if (is_in) in_ports.push_back(parent_p);
        else out_ports.push_back(parent_p);
      }
    }

    const double port_w = 84.0;
    const double port_h = 28.0;
    const double pitch_y = 50.0;

    // Center input ports on West (left) sheet edge
    const double in_x = page_bounds_.x + page_margin_;
    const double in_total_h = in_ports.empty() ? 0.0 : ((in_ports.size() - 1) * pitch_y + port_h);
    const double in_start_y = (page_bounds_.y + page_bounds_.height * 0.5) - (in_total_h * 0.5);

    for (size_t i = 0; i < in_ports.size(); ++i) {
      const auto* parent_p = in_ports[i];
      core::Node port_marker;
      port_marker.name = parent_p->name;
      port_marker.shape = core::ShapeType::InputPort;
      port_marker.properties["interface_kind"] = "input";
      port_marker.bounds = {in_x, in_start_y + i * pitch_y, port_w, port_h};

      core::NodeId marker_id = child_graph->add_node(std::move(port_marker));
      core::Port mp;
      mp.parent_node = marker_id;
      mp.name = parent_p->name;
      mp.local_anchor = core::Vec2d{1.0, 0.5};
      mp.direction = core::PortDirection::East;
      mp.pin_type = "input";
      mp.is_boundary_interface = true;
      child_graph->add_port(std::move(mp));
    }

    // Center output ports on East (right) sheet edge
    const double out_x = page_bounds_.x + page_bounds_.width - page_margin_ - port_w;
    const double out_total_h = out_ports.empty() ? 0.0 : ((out_ports.size() - 1) * pitch_y + port_h);
    const double out_start_y = (page_bounds_.y + page_bounds_.height * 0.5) - (out_total_h * 0.5);

    for (size_t j = 0; j < out_ports.size(); ++j) {
      const auto* parent_p = out_ports[j];
      core::Node port_marker;
      port_marker.name = parent_p->name;
      port_marker.shape = core::ShapeType::OutputPort;
      port_marker.properties["interface_kind"] = "output";
      port_marker.bounds = {out_x, out_start_y + j * pitch_y, port_w, port_h};

      core::NodeId marker_id = child_graph->add_node(std::move(port_marker));
      core::Port mp;
      mp.parent_node = marker_id;
      mp.name = parent_p->name;
      mp.local_anchor = core::Vec2d{0.0, 0.5};
      mp.direction = core::PortDirection::West;
      mp.pin_type = "output";
      mp.is_boundary_interface = true;
      child_graph->add_port(std::move(mp));
    }
  }

  auto* target_graph = child_graph.get();
  subsystem_graphs_[uuid] = std::move(child_graph);
  return target_graph;
}

bool CanvasWidget::drill_down_node(core::NodeId nid) {
  if (!hierarchy_context_ || !graph_model_) return false;
  auto* node = graph_model_->find_node(nid);
  if (!node) return false;

  if (node->shape != core::ShapeType::HierarchicalBlock) {
    node->shape = core::ShapeType::HierarchicalBlock;
    if (node->properties.find("subsystem_type") == node->properties.end()) {
      node->properties["subsystem_type"] = "builtin";
    }
  }

  const bool is_file = node->is_file_subsystem();
  const std::string file_path = node->subsystem_file();

  core::GraphModel* target_graph = ensure_subsystem_graph(nid);
  if (!target_graph) return false;

  if (target_graph == graph_model_) {
    emit status_message_requested("Cannot descend: target subsystem graph matches current diagram.");
    return false;
  }

  std::string scope_uri = is_file ? ("file://" + file_path) : ("vector://subsystem/" + node->name);
  hierarchy_context_->push_scope(nid, scope_uri, node->name, target_graph);
  scope_node_stack_.push_back(nid);

  set_graph_model(target_graph);
  refresh_spatial_index();
  fit_content();
  update();
  emit hierarchy_scope_changed(QString::fromStdString(hierarchy_context_->breadcrumb_path()));
  emit status_message_requested(QString("Drilled down into subsystem '%1' (%2)")
      .arg(QString::fromStdString(node->name))
      .arg(is_file ? QString("File: %1").arg(QString::fromStdString(file_path)) : "Built-in"));
  return true;
}

bool CanvasWidget::pop_hierarchy_scope() {
  if (!hierarchy_context_ || hierarchy_context_->depth() <= 1) {
    emit status_message_requested("Already at top root diagram level.");
    return false;
  }

  if (!scope_node_stack_.empty()) {
    core::NodeId sub_nid = scope_node_stack_.back();
    scope_node_stack_.pop_back();

    core::GraphModel* current_sub_graph = hierarchy_context_->current_graph();
    hierarchy_context_->pop_scope();
    core::GraphModel* parent_graph = hierarchy_context_->current_graph();

    if (parent_graph && current_sub_graph) {
      if (auto* parent_node = parent_graph->find_node(sub_nid)) {
        core::SymbolGenerator::sync_symbol_from_diagram(*parent_node, *parent_graph, *current_sub_graph);
        parent_graph->update_all_edge_routes();

        if (parent_node->is_file_subsystem() && !parent_node->subsystem_file().empty()) {
          const std::string fpath = parent_node->subsystem_file();
          std::ofstream out(fpath);
          if (out.is_open()) {
            if (fpath.ends_with(".drawio") || fpath.ends_with(".xml")) {
              io::DrawioParser parser;
              out << parser.export_xml(*current_sub_graph);
            } else {
              io::StorageEngine storage;
              out << storage.serialize_yaml(*current_sub_graph);
            }
          }
        } else {
          io::StorageEngine storage;
          parent_node->properties["embedded_graph_yaml"] = storage.serialize_yaml(*current_sub_graph);
        }
      }
    }
  } else {
    hierarchy_context_->pop_scope();
  }

  set_graph_model(hierarchy_context_->current_graph());
  reset_view();
  update();
  emit hierarchy_scope_changed(QString::fromStdString(hierarchy_context_->breadcrumb_path()));
  emit status_message_requested("Returned to parent scope.");
  return true;
}

void CanvasWidget::reset_hierarchy(core::GraphModel* root_graph) {
  scope_node_stack_.clear();
  subsystem_graphs_.clear();
  if (hierarchy_context_) {
    hierarchy_context_->reset(root_graph);
    emit hierarchy_scope_changed(QString::fromStdString(hierarchy_context_->breadcrumb_path()));
  }
  set_graph_model(root_graph);
  clear_selection();
  refresh_spatial_index();
  reset_view();
  update();
}

void CanvasWidget::sync_all_hierarchy_symbols() {
  if (!hierarchy_context_ || hierarchy_context_->depth() <= 1) return;
  for (size_t d = hierarchy_context_->depth() - 1; d >= 1; --d) {
    const auto& child_scope = hierarchy_context_->scope_at(d);
    const auto& parent_scope = hierarchy_context_->scope_at(d - 1);
    core::GraphModel* child_g = child_scope.graph_model;
    core::GraphModel* parent_g = parent_scope.graph_model;
    if (!child_g || !parent_g) continue;

    auto* symbol_node = parent_g->find_node(child_scope.instance_node_id);
    if (!symbol_node) continue;

    core::SymbolGenerator::sync_symbol_from_diagram(*symbol_node, *parent_g, *child_g);
    parent_g->update_all_edge_routes();

    if (symbol_node->is_file_subsystem() && !symbol_node->subsystem_file().empty()) {
      const std::string fpath = symbol_node->subsystem_file();
      std::ofstream out(fpath);
      if (out.is_open()) {
        if (fpath.ends_with(".drawio") || fpath.ends_with(".xml")) {
          io::DrawioParser parser;
          out << parser.export_xml(*child_g);
        } else {
          io::StorageEngine storage;
          out << storage.serialize_yaml(*child_g);
        }
      }
    } else {
      io::StorageEngine storage;
      symbol_node->properties["embedded_graph_yaml"] = storage.serialize_yaml(*child_g);
    }
  }
}

core::NodeId CanvasWidget::add_subsystem(core::Vec2d center, const std::string& name, bool is_file, const std::string& file_path) {
  if (!graph_model_) return core::InvalidNodeId;
  const std::string sname = name.empty() ? ("Subsystem " + std::to_string(graph_model_->node_count() + 1)) : name;

  if (grid_manager_ && grid_manager_->is_snap_enabled() && grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid)) {
    const auto snapped = grid_manager_->snap_to_grid(center);
    const double snap_dist = grid_manager_->snap_tolerance() / std::max(0.2, zoom_level_);
    if (center.distance_to(snapped) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_x() * 0.5) {
      center = snapped;
    }
  }

  // If the symbol is not created, a rectangle is set by default at creation
  core::Node node = core::SymbolGenerator::create_default_rectangle_symbol(sname, center);
  node.z_order = graph_model_->get_max_z_order() + 1;
  const std::string sub_uuid = generate_subsystem_uuid();
  node.properties["subsystem_uuid"] = sub_uuid;

  if (is_file) {
    node.set_subsystem_file(file_path.empty() ? (sname + ".yaml") : file_path);
  } else {
    node.set_subsystem_builtin();
  }

  core::NodeId nid = core::InvalidNodeId;
  if (command_history_) {
    auto cmd = std::make_unique<core::AddNodeCommand>(*graph_model_, std::move(node));
    auto* cmd_ptr = cmd.get();
    command_history_->execute(std::move(cmd));
    nid = cmd_ptr->created_id();
  } else {
    nid = graph_model_->add_node(std::move(node));
  }

  refresh_spatial_index();
  update();
  emit diagram_modified();
  emit status_message_requested(QString("Created empty subsystem '%1' (%2)")
      .arg(QString::fromStdString(sname))
      .arg(is_file ? QString("File: %1").arg(QString::fromStdString(file_path)) : "Built-in"));
  return nid;
}

core::NodeId CanvasWidget::add_hierarchical_port(core::Vec2d pos, bool is_input, const std::string& name, bool snap_pos) {
  if (!graph_model_) return core::InvalidNodeId;

  if (snap_pos && grid_manager_ && grid_manager_->is_snap_enabled() && grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid)) {
    const auto snapped = grid_manager_->snap_to_grid(pos);
    const double snap_dist = grid_manager_->snap_tolerance() / std::max(0.2, zoom_level_);
    if (pos.distance_to(snapped) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_x() * 0.5) {
      pos = snapped;
    }
  }

  core::Node node;
  node.shape = is_input ? core::ShapeType::InputPort : core::ShapeType::OutputPort;
  node.properties["interface_kind"] = is_input ? "input" : "output";

  size_t count = 0;
  for (const auto& [nid, n] : graph_model_->nodes()) {
    if (is_input && n.is_input_port()) count++;
    else if (!is_input && n.is_output_port()) count++;
  }

  if (!name.empty()) {
    node.name = name;
  } else {
    node.name = is_input ? ("in_" + std::to_string(count)) : ("out_" + std::to_string(count));
  }

  const double w = 84.0;
  const double h = 28.0;
  node.bounds = {pos.x - w * 0.5, pos.y - h * 0.5, w, h};
  node.z_order = graph_model_->get_max_z_order() + 1;

  const std::string port_node_name = node.name;
  const core::NodeId nid = graph_model_->add_node(std::move(node));

  core::Port p;
  p.parent_node = nid;
  p.name = port_node_name;
  p.local_anchor = is_input ? core::Vec2d{1.0, 0.5} : core::Vec2d{0.0, 0.5};
  p.direction = is_input ? core::PortDirection::East : core::PortDirection::West;
  p.pin_type = is_input ? "input" : "output";
  p.is_boundary_interface = true;
  graph_model_->add_port(std::move(p));

  refresh_spatial_index();
  sync_current_subsystem_symbol();
  update();
  emit diagram_modified();

  emit status_message_requested(QString("Added %1 Port '%2' at (%3, %4)")
      .arg(is_input ? "Input" : "Output")
      .arg(QString::fromStdString(port_node_name))
      .arg(static_cast<int>(pos.x))
      .arg(static_cast<int>(pos.y)));

  return nid;
}

void CanvasWidget::sync_current_subsystem_symbol() {
  if (!hierarchy_context_ || hierarchy_context_->depth() <= 1) return;
  core::GraphModel* parent_g = hierarchy_context_->parent_graph();
  if (!parent_g) return;

  const core::NodeId sub_nid = hierarchy_context_->current_instance_node_id();
  if (sub_nid == core::InvalidNodeId) return;

  auto* symbol_node = parent_g->find_node(sub_nid);
  if (!symbol_node) return;

  core::SymbolGenerator::sync_symbol_from_diagram(*symbol_node, *parent_g, *graph_model_);
  parent_g->update_all_edge_routes();

  if (symbol_node->is_file_subsystem() && !symbol_node->subsystem_file().empty()) {
    const std::string fpath = symbol_node->subsystem_file();
    std::ofstream out(fpath);
    if (out.is_open()) {
      if (fpath.ends_with(".drawio") || fpath.ends_with(".xml")) {
        io::DrawioParser parser;
        out << parser.export_xml(*graph_model_);
      } else {
        io::StorageEngine storage;
        out << storage.serialize_yaml(*graph_model_);
      }
    }
  } else {
    io::StorageEngine storage;
    symbol_node->properties["embedded_graph_yaml"] = storage.serialize_yaml(*graph_model_);
  }
}

bool CanvasWidget::convert_subsystem_to_file(core::NodeId nid, const std::string& file_path) {
  if (!graph_model_) return false;
  auto* node = graph_model_->find_node(nid);
  if (!node) return false;

  std::string fpath = file_path.empty() ? (node->name + ".yaml") : file_path;
  node->set_subsystem_file(fpath);

  std::string uuid = node->properties["subsystem_uuid"];
  auto it_cached = uuid.empty() ? subsystem_graphs_.end() : subsystem_graphs_.find(uuid);
  if (it_cached != subsystem_graphs_.end()) {
    std::ofstream out(fpath);
    if (out.is_open()) {
      io::StorageEngine storage;
      out << storage.serialize_yaml(*it_cached->second);
    }
  } else {
    auto it_embed = node->properties.find("embedded_graph_yaml");
    if (it_embed != node->properties.end() && !it_embed->second.empty()) {
      std::ofstream out(fpath);
      if (out.is_open()) {
        out << it_embed->second;
      }
    }
  }
  update();
  emit status_message_requested(QString("Converted subsystem '%1' to external file '%2'")
      .arg(QString::fromStdString(node->name))
      .arg(QString::fromStdString(fpath)));
  return true;
}

bool CanvasWidget::convert_subsystem_to_builtin(core::NodeId nid) {
  if (!graph_model_) return false;
  auto* node = graph_model_->find_node(nid);
  if (!node) return false;

  std::string fpath = node->subsystem_file();
  if (!fpath.empty()) {
    std::ifstream in(fpath);
    if (in.is_open()) {
      std::string content((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
      node->properties["embedded_graph_yaml"] = content;
    }
  }
  node->set_subsystem_builtin();
  node->properties.erase("subsystem_file");
  update();
  emit status_message_requested(QString("Converted subsystem '%1' to built-in embedded diagram.")
      .arg(QString::fromStdString(node->name)));
  return true;
}

bool CanvasWidget::reload_subsystem_file(core::NodeId nid) {
  if (!graph_model_) return false;
  auto* node = graph_model_->find_node(nid);
  if (!node || !node->is_file_subsystem()) return false;

  std::string fpath = node->subsystem_file();
  if (fpath.empty()) return false;

  std::ifstream in(fpath);
  if (!in.is_open()) return false;

  std::string content((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());

  auto new_graph = std::make_unique<core::GraphModel>();
  io::StorageEngine storage;
  if (!storage.deserialize_yaml(content, *new_graph).success) {
    return false;
  }
  std::string uuid = node->properties["subsystem_uuid"];
  if (uuid.empty()) {
    uuid = generate_subsystem_uuid();
    node->properties["subsystem_uuid"] = uuid;
  }
  subsystem_graphs_[uuid] = std::move(new_graph);
  update();
  emit status_message_requested(QString("Reloaded subsystem '%1' from '%2'")
      .arg(QString::fromStdString(node->name))
      .arg(QString::fromStdString(fpath)));
  return true;
}

bool CanvasWidget::save_subsystem_file(core::NodeId nid) {
  if (!graph_model_) return false;
  auto* node = graph_model_->find_node(nid);
  if (!node || !node->is_file_subsystem()) return false;

  std::string fpath = node->subsystem_file();
  if (fpath.empty()) return false;

  std::string uuid = node->properties["subsystem_uuid"];
  if (uuid.empty()) return false;

  auto it_cached = subsystem_graphs_.find(uuid);
  if (it_cached == subsystem_graphs_.end()) return false;

  std::ofstream out(fpath);
  if (!out.is_open()) return false;

  io::StorageEngine storage;
  out << storage.serialize_yaml(*it_cached->second);
  emit status_message_requested(QString("Saved subsystem '%1' to '%2'")
      .arg(QString::fromStdString(node->name))
      .arg(QString::fromStdString(fpath)));
  return true;
}

core::EdgeId CanvasWidget::add_wire_by_nodes(core::NodeId src_node, core::NodeId tgt_node, const std::string& name) {
  if (!graph_model_) return core::InvalidEdgeId;
  const auto* n1 = graph_model_->find_node(src_node);
  const auto* n2 = graph_model_->find_node(tgt_node);
  if (!n1 || !n2) return core::InvalidEdgeId;

  core::PortId p1 = n1->port_ids.empty() ? core::InvalidPortId : n1->port_ids.front();
  core::PortId p2 = n2->port_ids.empty() ? core::InvalidPortId : n2->port_ids.front();

  core::Edge edge;
  edge.name = name.empty() ? ("Net " + std::to_string(graph_model_->edge_count() + 1)) : name;
  edge.source_node = src_node;
  edge.source_port = p1;
  edge.target_node = tgt_node;
  edge.target_port = p2;

  core::Vec2d p_src = n1->bounds.center();
  core::Vec2d p_tgt = n2->bounds.center();
  edge.waypoints = {p_src, {(p_src.x + p_tgt.x) * 0.5, p_src.y}, {(p_src.x + p_tgt.x) * 0.5, p_tgt.y}, p_tgt};

  core::EdgeId eid = graph_model_->add_edge(std::move(edge));
  update();
  emit diagram_modified();
  emit status_message_requested(QString("Connected %1 to %2")
      .arg(QString::fromStdString(n1->name), QString::fromStdString(n2->name)));
  return eid;
}

core::EdgeId CanvasWidget::add_wire_by_points(core::Vec2d p1, core::Vec2d p2, const std::string& name) {
  if (!graph_model_) return core::InvalidEdgeId;
  core::Edge edge;
  edge.name = name.empty() ? ("Line " + std::to_string(graph_model_->edge_count() + 1)) : name;
  edge.waypoints = {p1, p2};
  core::EdgeId eid = graph_model_->add_edge(std::move(edge));
  update();
  emit diagram_modified();
  emit status_message_requested(QString("Created Line from (%1, %2) to (%3, %4)")
      .arg(static_cast<int>(p1.x)).arg(static_cast<int>(p1.y))
      .arg(static_cast<int>(p2.x)).arg(static_cast<int>(p2.y)));
  return eid;
}

bool CanvasWidget::move_selected(core::Vec2d delta) {
  if (!graph_model_ || selected_nodes_.empty()) return false;
  for (core::NodeId id : selected_nodes_) {
    if (auto* n = graph_model_->find_node(id)) {
      n->bounds.x += delta.x;
      n->bounds.y += delta.y;
    }
  }
  refresh_spatial_index();
  update();
  return true;
}

bool CanvasWidget::move_node(core::NodeId id, core::Vec2d delta) {
  if (!graph_model_) return false;
  if (auto* n = graph_model_->find_node(id)) {
    n->bounds.x += delta.x;
    n->bounds.y += delta.y;
    refresh_spatial_index();
    update();
    return true;
  }
  return false;
}

void CanvasWidget::select_node(core::NodeId id, bool clear_previous) {
  if (!graph_model_) return;
  if (clear_previous) clear_selection();
  if (auto* n = graph_model_->find_node(id)) {
    n->selected = true;
    selected_nodes_.push_back(id);
    update();
    emit selection_changed();
  }
}

bool CanvasWidget::select_node_by_name(const std::string& name) {
  if (!graph_model_) return false;
  for (const auto& [id, n] : graph_model_->nodes()) {
    if (n.name == name) {
      select_node(id, true);
      return true;
    }
  }
  return false;
}

bool CanvasWidget::delete_node_by_name(const std::string& name) {
  if (!graph_model_) return false;
  for (const auto& [id, n] : graph_model_->nodes()) {
    if (n.name == name) {
      graph_model_->remove_node(id);
      refresh_spatial_index();
      update();
      emit diagram_modified();
      emit selection_changed();
      return true;
    }
  }
  return false;
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (tool_mode_ == ToolMode::AddPolygon) {
    if (in_progress_polygon_points_.size() >= 3) {
      finish_polygon_creation();
      event->accept();
      return;
    }
  }

  if (event->button() == Qt::LeftButton) {
    const core::Vec2d click_world = screen_to_world({
      static_cast<double>(event->position().x()),
      static_cast<double>(event->position().y())
    });

    if (spatial_index_) {
      auto hits = spatial_index_->query_point(click_world);
      if (!hits.empty()) {
        const core::NodeId clicked_node = pick_topmost_node(hits);
        if (graph_model_) {
          if (const auto* n = graph_model_->find_node(clicked_node)) {
            if (n->is_subsystem()) {
              drill_down_node(clicked_node);
              return;
            } else {
              start_inline_text_edit(clicked_node);
              return;
            }
          }
        }
      }
    }
  }
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
  last_mouse_screen_ = {static_cast<double>(event->position().x()),
                        static_cast<double>(event->position().y())};
  interaction_start_world_ = screen_to_world(last_mouse_screen_);

  if (is_space_panning_ && event->button() == Qt::LeftButton) {
    interaction_state_ = InteractionState::Panning;
    setCursor(Qt::ClosedHandCursor);
    return;
  }

  if (event->button() == Qt::MiddleButton ||
      (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier))) {
    interaction_state_ = InteractionState::Panning;
    return;
  }

  // Right-click cancels candidate placement mode or active wiring
  if (event->button() == Qt::RightButton) {
    if (wiring_click_origin_locked_ || interaction_state_ == InteractionState::WiringConnector) {
      wiring_click_origin_locked_ = false;
      wiring_source_node_ = core::InvalidNodeId;
      wiring_source_port_ = core::InvalidPortId;
      interaction_state_ = InteractionState::None;
      update();
      event->accept();
      return;
    }
    if (is_placement_tool_active()) {
      cancel_placement();
      event->accept();
      return;
    }
  }

  // Interactive Point-and-Click Polygon Creation Tool
  if (tool_mode_ == ToolMode::AddPolygon) {
    if (event->button() == Qt::LeftButton) {
      core::Vec2d pt_to_add = polygon_cursor_world_;
      if (in_progress_polygon_points_.empty() && pt_to_add.x == 0.0 && pt_to_add.y == 0.0) {
        pt_to_add = interaction_start_world_;
      }

      if (in_progress_polygon_points_.size() >= 3 &&
          pt_to_add.distance_sq_to(in_progress_polygon_points_.front()) < 1e-4) {
        finish_polygon_creation();
        event->accept();
        return;
      }

      if (in_progress_polygon_points_.empty() ||
          pt_to_add.distance_sq_to(in_progress_polygon_points_.back()) > 1e-4) {
        in_progress_polygon_points_.push_back(pt_to_add);
        emit status_message_requested(QString("Polygon: Vertex %1 placed at (%2, %3). Click next point or Right-click to finish.")
            .arg(in_progress_polygon_points_.size())
            .arg(static_cast<int>(pt_to_add.x))
            .arg(static_cast<int>(pt_to_add.y)));
      }
      update();
      event->accept();
      return;
    } else if (event->button() == Qt::RightButton) {
      if (in_progress_polygon_points_.size() >= 3) {
        finish_polygon_creation();
      } else {
        cancel_polygon_creation();
        set_tool_mode(ToolMode::Select);
      }
      event->accept();
      return;
    }
  }

  if (event->button() != Qt::LeftButton) {
    return;
  }

  // Handle placement of new shapes (Unified Shape Placement Engine with Drag-to-Resize)
  if (is_placement_tool_active()) {
    if (interaction_state_ == InteractionState::InstantiatingResize && instantiation_two_click_active_) {
      finalize_instantiation();
      event->accept();
      return;
    }

    core::Vec2d anchor_pos = interaction_start_world_;
    if (grid_manager_ && !(event->modifiers() & Qt::AltModifier)) {
      auto snap_res = grid_manager_->resolve_snap(anchor_pos, *graph_model_, spatial_index_);
      if (snap_res.snapped) {
        anchor_pos = snap_res.snapped_point;
      }
    }

    instantiation_anchor_world_ = anchor_pos;
    instantiation_press_screen_ = {static_cast<double>(event->position().x()),
                                   static_cast<double>(event->position().y())};
    instantiation_drag_active_ = true;
    instantiation_two_click_active_ = false;
    interaction_state_ = InteractionState::InstantiatingResize;

    placement_preview_bounds_ = get_placement_bounds(tool_mode_, anchor_pos);
    update();
    event->accept();
    return;
  }

  // Handle connector wiring tool (click origin and click destination, with specific connect points)
  if (tool_mode_ == ToolMode::AddConnector) {
    if (wiring_click_origin_locked_) {
      // Click 2: Destination clicked!
      core::NodeId hit_node = core::InvalidNodeId;
      core::PortId hit_port = find_hit_port(interaction_start_world_, 20.0, &hit_node);
      if (hit_node == core::InvalidNodeId && spatial_index_) {
        auto hits = spatial_index_->query_point(interaction_start_world_);
        if (!hits.empty()) {
          hit_node = pick_topmost_node(hits);
        }
      }

      if (hit_node != core::InvalidNodeId && (hit_node != wiring_source_node_ || hit_port != wiring_source_port_)) {
        auto_route_arrow(wiring_source_node_, wiring_source_port_, hit_node, hit_port);
      }

      wiring_click_origin_locked_ = false;
      wiring_source_node_ = core::InvalidNodeId;
      wiring_source_port_ = core::InvalidPortId;
      interaction_state_ = InteractionState::None;
      update();
      return;
    }

    // Click 1: Origin clicked!
    core::NodeId hit_node = core::InvalidNodeId;
    core::PortId hit_port = find_hit_port(interaction_start_world_, 20.0, &hit_node);
    if (hit_node == core::InvalidNodeId && spatial_index_) {
      auto hits = spatial_index_->query_point(interaction_start_world_);
      if (!hits.empty()) {
        hit_node = pick_topmost_node(hits);
      }
    }

    if (hit_node != core::InvalidNodeId) {
      wiring_source_node_ = hit_node;
      wiring_source_port_ = hit_port;
      wiring_source_is_specific_port_ = (hit_port != core::InvalidPortId);
      wiring_current_target_ = interaction_start_world_;
      wiring_click_origin_locked_ = true;
      interaction_state_ = InteractionState::WiringConnector;
      update();
    }
    return;
  }

  // ToolMode::Select - Hit-test Selection Manipulators (8 Resize Handles, Lollipop Rotation Stalk, Quick-Connect Arrows)
  core::NodeId handle_node = core::InvalidNodeId;
  const HandleType hit_handle = hit_test_handles(last_mouse_screen_, &handle_node);
  if (hit_handle != HandleType::None && handle_node != core::InvalidNodeId && graph_model_) {
    if (hit_handle == HandleType::QuickConnectNorth ||
        hit_handle == HandleType::QuickConnectEast ||
        hit_handle == HandleType::QuickConnectSouth ||
        hit_handle == HandleType::QuickConnectWest) {
      core::PortDirection target_dir = core::PortDirection::North;
      core::Vec2d anchor = {0.5, 0.0};
      std::string port_name = "N";
      if (hit_handle == HandleType::QuickConnectEast) {
        target_dir = core::PortDirection::East;
        anchor = {1.0, 0.5};
        port_name = "E";
      } else if (hit_handle == HandleType::QuickConnectSouth) {
        target_dir = core::PortDirection::South;
        anchor = {0.5, 1.0};
        port_name = "S";
      } else if (hit_handle == HandleType::QuickConnectWest) {
        target_dir = core::PortDirection::West;
        anchor = {0.0, 0.5};
        port_name = "W";
      }

      core::PortId chosen_port = core::InvalidPortId;
      if (const auto* n = graph_model_->find_node(handle_node)) {
        for (core::PortId pid : n->port_ids) {
          if (const auto* p = graph_model_->find_port(pid)) {
            if (p->direction == target_dir) {
              chosen_port = pid;
              break;
            }
          }
        }
      }

      if (chosen_port == core::InvalidPortId) {
        core::Port p;
        p.parent_node = handle_node;
        p.name = port_name;
        p.local_anchor = anchor;
        p.direction = target_dir;
        chosen_port = graph_model_->add_port(std::move(p));
      }

      wiring_source_node_ = handle_node;
      wiring_source_port_ = chosen_port;
      wiring_current_target_ = interaction_start_world_;
      interaction_state_ = InteractionState::WiringConnector;
      update();
      return;
    } else if (hit_handle == HandleType::RotationStalk) {
      active_handle_ = hit_handle;
      active_handle_node_ = handle_node;
      interaction_state_ = InteractionState::RotatingNode;
      if (const auto* n = graph_model_->find_node(handle_node)) {
        rotation_initial_deg_ = n->rotation_deg;
        rotation_center_world_ = n->bounds.center();
        current_rotating_angle_deg_ = n->rotation_deg;
        is_current_angle_snapped_ = false;
      }
      update();
      return;
    } else if (is_parametric_handle(hit_handle)) {
      active_handle_ = hit_handle;
      active_handle_node_ = handle_node;
      interaction_state_ = InteractionState::DraggingParametricHandle;
      if (const auto* n = graph_model_->find_node(handle_node)) {
        parametric_initial_vertices_ = n->polygon_vertices;
        parametric_initial_props_ = n->properties;
      }
      update();
      return;
    } else {
      active_handle_ = hit_handle;
      active_handle_node_ = handle_node;
      interaction_state_ = InteractionState::ResizingNode;
      if (const auto* n = graph_model_->find_node(handle_node)) {
        resize_initial_bounds_ = n->bounds;
      }
      resize_start_world_ = interaction_start_world_;
      active_smart_guides_.clear();
      return;
    }
  }

  // Select node under cursor
  if (spatial_index_) {
    auto hits = spatial_index_->query_point(interaction_start_world_);
    if (!hits.empty()) {
      const core::NodeId clicked_node = pick_topmost_node(hits, interaction_start_world_);
      if (clicked_node != core::InvalidNodeId && graph_model_) {
        const bool already_selected = std::find(selected_nodes_.begin(), selected_nodes_.end(), clicked_node) != selected_nodes_.end();
        if (!(event->modifiers() & Qt::ShiftModifier)) {
          if (!already_selected) {
            for (auto& [id, n] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
              n.selected = (id == clicked_node);
            }
            selected_nodes_ = {clicked_node};
          }
        } else {
          if (auto* n = graph_model_->find_node(clicked_node)) {
            n->selected = !n->selected;
            if (n->selected) selected_nodes_.push_back(clicked_node);
            else std::erase(selected_nodes_, clicked_node);
          }
        }
      }
      interaction_state_ = InteractionState::DraggingNodes;
      drag_initial_bounds_.clear();
      if (graph_model_) {
        for (core::NodeId id : selected_nodes_) {
          if (const auto* n = graph_model_->find_node(id)) {
            drag_initial_bounds_[id] = n->bounds;
          }
        }
      }
      drag_accumulated_delta_ = {0.0, 0.0};
      active_smart_guides_.clear();
      emit selection_changed();
      update();
      return;
    }
  }

  // Hit-test wires/edges
  const core::EdgeId hit_edge = find_hit_edge(interaction_start_world_, 8.0 / std::max(0.1, zoom_level_));
  if (hit_edge != core::InvalidEdgeId && graph_model_) {
    select_edge(hit_edge, !(event->modifiers() & Qt::ShiftModifier));
    return;
  }

  // Marquee selection start
  clear_selection();
  interaction_state_ = InteractionState::SelectingMarquee;
  selection_marquee_ = core::Rect2d(interaction_start_world_, core::Size2d(0, 0));
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
  const core::Vec2d current_screen = {static_cast<double>(event->position().x()),
                                      static_cast<double>(event->position().y())};
  const core::Vec2d delta_screen = current_screen - last_mouse_screen_;
  const core::Vec2d current_world = screen_to_world(current_screen);

  if (interaction_state_ == InteractionState::Panning) {
    pan_by(delta_screen);
    update_lod_state();
    update();
  } else if (interaction_state_ == InteractionState::WiringConnector) {
    wiring_current_target_ = current_world;
    core::NodeId h_node = core::InvalidNodeId;
    core::PortId h_port = find_hit_port(current_world, 20.0, &h_node);
    if (h_port != core::InvalidPortId && graph_model_) {
      if (const auto* hn = graph_model_->find_node(h_node)) {
        if (const auto* hp = graph_model_->find_port(h_port)) {
          wiring_current_target_ = hn->resolve_port_position(*hp);
        }
      }
    } else if (grid_manager_) {
      auto snap_res = grid_manager_->resolve_snap(current_world, *graph_model_, spatial_index_);
      if (snap_res.snapped) {
        wiring_current_target_ = snap_res.snapped_point;
      }
    }
    update();
  } else if (interaction_state_ == InteractionState::InstantiatingResize) {
    core::Vec2d cursor_pt = current_world;
    if (grid_manager_ && !(event->modifiers() & Qt::AltModifier)) {
      auto snap_res = grid_manager_->resolve_snap(cursor_pt, *graph_model_, spatial_index_);
      if (snap_res.snapped) {
        cursor_pt = snap_res.snapped_point;
      }
    }

    const double def_w = (active_template_.width > 0.0) ? active_template_.width : 120.0;
    const double def_h = (active_template_.height > 0.0) ? active_template_.height : 70.0;
    const bool keep_ratio = (event->modifiers() & Qt::ShiftModifier);

    const core::Vec2d anc_norm = active_template_.anchor_point;
    const core::Vec2d res_norm = active_template_.resize_point;

    const double delta_ax = res_norm.x - anc_norm.x;
    const double delta_ay = res_norm.y - anc_norm.y;

    const double dx = cursor_pt.x - instantiation_anchor_world_.x;
    const double dy = cursor_pt.y - instantiation_anchor_world_.y;

    double w = def_w;
    double h = def_h;

    if (std::abs(delta_ax) > 1e-3) {
      w = std::max(20.0, std::abs(dx / delta_ax));
    }
    if (std::abs(delta_ay) > 1e-3) {
      h = std::max(20.0, std::abs(dy / delta_ay));
    }

    if (keep_ratio && def_h > 1e-3) {
      const double aspect = def_w / def_h;
      if (w / h > aspect) {
        w = h * aspect;
      } else {
        h = w / aspect;
      }
      w = std::max(20.0, w);
      h = std::max(20.0, h);
    }

    const bool rev_x = (delta_ax >= 0.0) ? (dx < 0.0) : (dx > 0.0);
    const bool rev_y = (delta_ay >= 0.0) ? (dy < 0.0) : (dy > 0.0);

    const double x = rev_x ? (instantiation_anchor_world_.x - (1.0 - anc_norm.x) * w)
                           : (instantiation_anchor_world_.x - anc_norm.x * w);
    const double y = rev_y ? (instantiation_anchor_world_.y - (1.0 - anc_norm.y) * h)
                           : (instantiation_anchor_world_.y - anc_norm.y * h);

    placement_preview_bounds_ = {x, y, w, h};

    if (!(event->modifiers() & Qt::AltModifier)) {
      core::Vec2d snap_offset = compute_smart_alignment(core::InvalidNodeId, placement_preview_bounds_);
      placement_preview_bounds_.x += snap_offset.x;
      placement_preview_bounds_.y += snap_offset.y;
    } else {
      active_smart_guides_.clear();
    }

    setCursor(Qt::SizeFDiagCursor);
    const bool is_aligned = !active_smart_guides_.empty();

    emit cursor_position_changed(QString("Instantiating: %1×%2 mm  |  (%3, %4)%5%6")
        .arg(static_cast<int>(w * 0.264583))
        .arg(static_cast<int>(h * 0.264583))
        .arg(static_cast<int>(cursor_pt.x))
        .arg(static_cast<int>(cursor_pt.y))
        .arg(keep_ratio ? " [Shift: Ratio Locked]" : "")
        .arg(is_aligned ? " [Aligned]" : ""));

    last_mouse_screen_ = current_screen;
    update();
    return;
  } else if (interaction_state_ == InteractionState::DraggingParametricHandle) {
    if (graph_model_ && active_handle_node_ != core::InvalidNodeId) {
      if (auto* node = graph_model_->find_node(active_handle_node_)) {
        core::Vec2d unrot_cursor = current_world;
        if (std::abs(node->rotation_deg) > 1e-4) {
          const core::Vec2d center = node->bounds.center();
          const double rad = -node->rotation_deg * (std::numbers::pi / 180.0);
          const double cos_a = std::cos(rad);
          const double sin_a = std::sin(rad);
          const double dx = current_world.x - center.x;
          const double dy = current_world.y - center.y;
          unrot_cursor = {center.x + dx * cos_a - dy * sin_a, center.y + dx * sin_a + dy * cos_a};
        }

        const bool symmetric = (event->modifiers() & Qt::AltModifier);
        apply_parametric_drag(*node, active_handle_, unrot_cursor, symmetric);
        if (spatial_index_) {
          spatial_index_->update(active_handle_node_, node->bounds);
        }
        emit diagram_modified();
        last_mouse_screen_ = current_screen;
        update();
        return;
      }
    }
  } else if (interaction_state_ == InteractionState::ResizingNode) {
    if (graph_model_ && active_handle_node_ != core::InvalidNodeId) {
      if (auto* node = graph_model_->find_node(active_handle_node_)) {
        const bool keep_ratio = (event->modifiers() & Qt::ShiftModifier);
        const bool from_center = (event->modifiers() & Qt::ControlModifier);

        const double w0 = resize_initial_bounds_.width;
        const double h0 = resize_initial_bounds_.height;
        const core::Vec2d center0 = resize_initial_bounds_.center();
        const double aspect_ratio = (h0 > 1e-4) ? (w0 / h0) : 1.0;

        const core::Vec2d delta = current_world - resize_start_world_;
        core::Rect2d nb = resize_initial_bounds_;

        if (from_center) {
          // Symmetric resizing from center
          double dw = 0.0;
          double dh = 0.0;
          switch (active_handle_) {
            case HandleType::TopLeft:
              dw = -delta.x * 2.0; dh = -delta.y * 2.0; break;
            case HandleType::TopCenter:
              dh = -delta.y * 2.0; break;
            case HandleType::TopRight:
              dw = delta.x * 2.0;  dh = -delta.y * 2.0; break;
            case HandleType::MiddleRight:
              dw = delta.x * 2.0;  break;
            case HandleType::BottomRight:
              dw = delta.x * 2.0;  dh = delta.y * 2.0; break;
            case HandleType::BottomCenter:
              dh = delta.y * 2.0;  break;
            case HandleType::BottomLeft:
              dw = -delta.x * 2.0; dh = delta.y * 2.0; break;
            case HandleType::MiddleLeft:
              dw = -delta.x * 2.0; break;
            default: break;
          }

          double new_w = std::max(20.0, w0 + dw);
          double new_h = std::max(20.0, h0 + dh);

          if (keep_ratio) {
            if (active_handle_ == HandleType::MiddleRight || active_handle_ == HandleType::MiddleLeft) {
              new_h = std::max(20.0, new_w / aspect_ratio);
            } else if (active_handle_ == HandleType::TopCenter || active_handle_ == HandleType::BottomCenter) {
              new_w = std::max(20.0, new_h * aspect_ratio);
            } else {
              const double scale_w = new_w / w0;
              const double scale_h = new_h / h0;
              const double scale = (std::abs(scale_w - 1.0) > std::abs(scale_h - 1.0)) ? scale_w : scale_h;
              new_w = std::max(20.0, w0 * scale);
              new_h = std::max(20.0, new_w / aspect_ratio);
            }
          }

          nb.x = center0.x - new_w * 0.5;
          nb.y = center0.y - new_h * 0.5;
          nb.width = new_w;
          nb.height = new_h;
        } else {
          // Standard corner/edge anchored resizing
          switch (active_handle_) {
            case HandleType::TopLeft:
              nb.x += delta.x; nb.y += delta.y;
              nb.width -= delta.x; nb.height -= delta.y;
              break;
            case HandleType::TopCenter:
              nb.y += delta.y; nb.height -= delta.y;
              break;
            case HandleType::TopRight:
              nb.y += delta.y;
              nb.width += delta.x; nb.height -= delta.y;
              break;
            case HandleType::MiddleRight:
              nb.width += delta.x;
              break;
            case HandleType::BottomRight:
              nb.width += delta.x; nb.height += delta.y;
              break;
            case HandleType::BottomCenter:
              nb.height += delta.y;
              break;
            case HandleType::BottomLeft:
              nb.x += delta.x;
              nb.width -= delta.x; nb.height += delta.y;
              break;
            case HandleType::MiddleLeft:
              nb.x += delta.x; nb.width -= delta.x;
              break;
            default:
              break;
          }

          if (keep_ratio) {
            if (active_handle_ == HandleType::BottomRight) {
              const double scale_w = nb.width / w0;
              const double scale_h = nb.height / h0;
              const double scale = (std::abs(scale_w - 1.0) > std::abs(scale_h - 1.0)) ? scale_w : scale_h;
              nb.width = std::max(20.0, w0 * scale);
              nb.height = std::max(20.0, nb.width / aspect_ratio);
            } else if (active_handle_ == HandleType::BottomLeft) {
              const double scale_w = nb.width / w0;
              const double scale_h = nb.height / h0;
              const double scale = (std::abs(scale_w - 1.0) > std::abs(scale_h - 1.0)) ? scale_w : scale_h;
              nb.width = std::max(20.0, w0 * scale);
              nb.height = std::max(20.0, nb.width / aspect_ratio);
              nb.x = resize_initial_bounds_.right() - nb.width;
            } else if (active_handle_ == HandleType::TopRight) {
              const double scale_w = nb.width / w0;
              const double scale_h = nb.height / h0;
              const double scale = (std::abs(scale_w - 1.0) > std::abs(scale_h - 1.0)) ? scale_w : scale_h;
              nb.width = std::max(20.0, w0 * scale);
              nb.height = std::max(20.0, nb.width / aspect_ratio);
              nb.y = resize_initial_bounds_.bottom() - nb.height;
            } else if (active_handle_ == HandleType::TopLeft) {
              const double scale_w = nb.width / w0;
              const double scale_h = nb.height / h0;
              const double scale = (std::abs(scale_w - 1.0) > std::abs(scale_h - 1.0)) ? scale_w : scale_h;
              nb.width = std::max(20.0, w0 * scale);
              nb.height = std::max(20.0, nb.width / aspect_ratio);
              nb.x = resize_initial_bounds_.right() - nb.width;
              nb.y = resize_initial_bounds_.bottom() - nb.height;
            } else if (active_handle_ == HandleType::MiddleRight || active_handle_ == HandleType::MiddleLeft) {
              nb.width = std::max(20.0, nb.width);
              nb.height = std::max(20.0, nb.width / aspect_ratio);
              nb.y = center0.y - nb.height * 0.5;
              if (active_handle_ == HandleType::MiddleLeft) {
                nb.x = resize_initial_bounds_.right() - nb.width;
              }
            } else if (active_handle_ == HandleType::TopCenter || active_handle_ == HandleType::BottomCenter) {
              nb.height = std::max(20.0, nb.height);
              nb.width = std::max(20.0, nb.height * aspect_ratio);
              nb.x = center0.x - nb.width * 0.5;
              if (active_handle_ == HandleType::TopCenter) {
                nb.y = resize_initial_bounds_.bottom() - nb.height;
              }
            }
          }

          if (nb.width < 20.0) {
            if (active_handle_ == HandleType::TopLeft || active_handle_ == HandleType::BottomLeft || active_handle_ == HandleType::MiddleLeft) {
              nb.x = resize_initial_bounds_.right() - 20.0;
            }
            nb.width = 20.0;
          }
          if (nb.height < 20.0) {
            if (active_handle_ == HandleType::TopLeft || active_handle_ == HandleType::TopCenter || active_handle_ == HandleType::TopRight) {
              nb.y = resize_initial_bounds_.bottom() - 20.0;
            }
            nb.height = 20.0;
          }
        }

        if (!(event->modifiers() & Qt::AltModifier)) {
          compute_resize_alignment(active_handle_node_, active_handle_, nb, from_center, keep_ratio);
        } else {
          active_smart_guides_.clear();
        }

        node->bounds = nb;
        graph_model_->update_all_edge_routes();
        refresh_spatial_index();
        update();
      }
    }
  } else if (interaction_state_ == InteractionState::RotatingNode) {
    if (graph_model_ && active_handle_node_ != core::InvalidNodeId) {
      if (auto* node = graph_model_->find_node(active_handle_node_)) {
        const core::Vec2d delta = current_world - rotation_center_world_;
        double raw_deg = std::atan2(delta.y, delta.x) * (180.0 / std::numbers::pi) + 90.0;
        raw_deg = std::fmod(raw_deg, 360.0);
        if (raw_deg < 0.0) raw_deg += 360.0;

        const bool shift_enforce = (event->modifiers() & Qt::ShiftModifier);
        const bool alt_free = (event->modifiers() & Qt::AltModifier);
        const bool should_snap = (angle_snapping_enabled_ || shift_enforce) && !alt_free;

        double final_deg = raw_deg;
        is_current_angle_snapped_ = false;
        if (should_snap) {
          if (cardinal_snap_priority_) {
            const double cardinals[] = {0.0, 90.0, 180.0, 270.0, 360.0};
            for (double c : cardinals) {
              if (std::abs(raw_deg - c) <= cardinal_snap_tolerance_deg_ ||
                  std::abs(raw_deg - (c - 360.0)) <= cardinal_snap_tolerance_deg_) {
                final_deg = (c >= 360.0) ? 0.0 : c;
                is_current_angle_snapped_ = true;
                break;
              }
            }
          }
          if (!is_current_angle_snapped_ && angle_snap_step_deg_ > 1e-4) {
            final_deg = std::round(raw_deg / angle_snap_step_deg_) * angle_snap_step_deg_;
            if (final_deg >= 360.0) final_deg = 0.0;
            is_current_angle_snapped_ = true;
          }
        }

        node->rotation_deg = final_deg;
        current_rotating_angle_deg_ = final_deg;
        graph_model_->update_all_edge_routes();
        refresh_spatial_index();
        emit node_rotation_changed(active_handle_node_, final_deg);
        emit cursor_position_changed(QString("Rotation: %1°%2  |  Center: (%3, %4)")
            .arg(final_deg, 0, 'f', 1)
            .arg(is_current_angle_snapped_ ? " [Snapped]" : " [Free]")
            .arg(static_cast<int>(rotation_center_world_.x))
            .arg(static_cast<int>(rotation_center_world_.y)));
        update();
      }
    }
  } else if (interaction_state_ == InteractionState::DraggingNodes) {
    if (graph_model_ && !selected_nodes_.empty()) {
      core::Vec2d total_delta = current_world - interaction_start_world_;
      if (event->modifiers() & Qt::ShiftModifier) {
        if (std::abs(total_delta.x) >= std::abs(total_delta.y)) {
          total_delta.y = 0.0;
        } else {
          total_delta.x = 0.0;
        }
      }

      core::Vec2d snap_offset{0.0, 0.0};
      if (!(event->modifiers() & Qt::AltModifier)) {
        const core::NodeId primary_id = selected_nodes_.front();
        auto it = drag_initial_bounds_.find(primary_id);
        if (it != drag_initial_bounds_.end()) {
          core::Rect2d proposed = it->second;
          proposed.x += total_delta.x;
          proposed.y += total_delta.y;
          snap_offset = compute_smart_alignment(primary_id, proposed);
        }
      } else {
        active_smart_guides_.clear();
      }

      const core::Vec2d effective_delta = total_delta + snap_offset;
      for (core::NodeId id : selected_nodes_) {
        if (auto* n = graph_model_->find_node(id)) {
          auto it = drag_initial_bounds_.find(id);
          if (it != drag_initial_bounds_.end()) {
            n->bounds.x = it->second.x + effective_delta.x;
            n->bounds.y = it->second.y + effective_delta.y;
          }
        }
      }
      drag_accumulated_delta_ = effective_delta;
      graph_model_->update_all_edge_routes();
      refresh_spatial_index();
    }
    update_lod_state();
    update();
  } else if (interaction_state_ == InteractionState::SelectingMarquee) {
    selection_marquee_ = core::Rect2d::from_corners(interaction_start_world_, current_world);
    update();
  } else if (interaction_state_ == InteractionState::None) {
    // Point-and-Click Polygon Creation Tool Real-time Tracking & Snapping
    if (tool_mode_ == ToolMode::AddPolygon) {
      core::Vec2d snapped = current_world;
      if (!(event->modifiers() & Qt::AltModifier)) {
        if (grid_manager_ && grid_manager_->is_snap_enabled() && grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid)) {
          snapped = grid_manager_->snap_to_grid(current_world);
        }
        // Orthogonal snapping to horizontal/vertical with previous vertex
        if (!in_progress_polygon_points_.empty()) {
          const auto& prev = in_progress_polygon_points_.back();
          const double snap_tol = (grid_manager_ ? grid_manager_->snap_tolerance() : 8.0) / std::max(0.2, zoom_level_);
          if (std::abs(current_world.x - prev.x) <= snap_tol) {
            snapped.x = prev.x;
          }
          if (std::abs(current_world.y - prev.y) <= snap_tol) {
            snapped.y = prev.y;
          }
        }
        // Snap to start point to close loop if 3+ points
        if (in_progress_polygon_points_.size() >= 3) {
          const auto& start_pt = in_progress_polygon_points_.front();
          const auto s_start = world_to_screen(start_pt);
          if (current_screen.distance_to(s_start) <= 15.0) {
            snapped = start_pt;
          }
        }
      }
      polygon_cursor_world_ = snapped;
      setCursor(Qt::CrossCursor);

      const size_t pt_count = in_progress_polygon_points_.size();
      if (pt_count == 0) {
        emit cursor_position_changed(QString("Polygon: Click to set first vertex | (%1, %2)")
            .arg(static_cast<int>(snapped.x)).arg(static_cast<int>(snapped.y)));
      } else {
        const bool near_start = (pt_count >= 3 && snapped.distance_sq_to(in_progress_polygon_points_.front()) < 1e-4);
        emit cursor_position_changed(QString("Polygon: %1 vertices placed | Next (%2, %3)%4")
            .arg(pt_count)
            .arg(static_cast<int>(snapped.x)).arg(static_cast<int>(snapped.y))
            .arg(near_start ? " [Click to Close]" : " [Right-click to Finish]"));
      }

      last_mouse_screen_ = current_screen;
      update();
      return;
    }

    if (is_placement_tool_active()) {
      core::Rect2d cand_bounds = get_placement_bounds(tool_mode_, current_world);

      core::Vec2d snap_offset{0.0, 0.0};
      if (!(event->modifiers() & Qt::AltModifier)) {
        snap_offset = compute_smart_alignment(core::InvalidNodeId, cand_bounds);
      } else {
        active_smart_guides_.clear();
      }

      cand_bounds.x += snap_offset.x;
      cand_bounds.y += snap_offset.y;
      placement_preview_bounds_ = cand_bounds;

      setCursor(Qt::CrossCursor);

      const bool is_aligned = !active_smart_guides_.empty();
      const bool is_snapped = (std::abs(snap_offset.x) > 1e-4 || std::abs(snap_offset.y) > 1e-4);

      emit cursor_position_changed(QString("X: %1 mm   Y: %2 mm  |  (%3, %4)%5")
          .arg(static_cast<int>(cand_bounds.center().x * 0.264583))
          .arg(static_cast<int>(cand_bounds.center().y * 0.264583))
          .arg(static_cast<int>(cand_bounds.center().x))
          .arg(static_cast<int>(cand_bounds.center().y))
          .arg(is_aligned ? " [Aligned]" : (is_snapped ? " [Grid Snapped]" : "")));

      last_mouse_screen_ = current_screen;
      update();
      return;
    }

    core::NodeId h_node = core::InvalidNodeId;
    const HandleType ht = hit_test_handles(current_screen, &h_node);

    if (ht != hovered_handle_) {
      hovered_handle_ = ht;
      update();
    }

    switch (ht) {
      case HandleType::TopLeft:
      case HandleType::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
      case HandleType::TopRight:
      case HandleType::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
      case HandleType::TopCenter:
      case HandleType::BottomCenter:
        setCursor(Qt::SizeVerCursor);
        break;
      case HandleType::MiddleLeft:
      case HandleType::MiddleRight:
        setCursor(Qt::SizeHorCursor);
        break;
      case HandleType::RotationStalk:
        setCursor(Qt::PointingHandCursor);
        break;
      case HandleType::QuickConnectNorth:
      case HandleType::QuickConnectEast:
      case HandleType::QuickConnectSouth:
      case HandleType::QuickConnectWest:
        setCursor(Qt::CrossCursor);
        break;
      case HandleType::ParametricPoint0:
      case HandleType::ParametricPoint1:
      case HandleType::ParametricPoint2:
      case HandleType::ParametricPoint3:
      case HandleType::ParametricPoint4:
      case HandleType::ParametricPoint5:
      case HandleType::ParametricPoint6:
      case HandleType::ParametricPoint7:
        setCursor(Qt::PointingHandCursor);
        emit status_message_requested("Parametric Control Point: Drag to reshape (Hold Alt for symmetric modification)");
        break;
      case HandleType::ParametricLineShaftTop:
      case HandleType::ParametricLineShaftBottom:
        setCursor(Qt::SizeVerCursor);
        emit status_message_requested("Shaft Line: Drag to adjust shaft thickness (Hold Alt for symmetric modification)");
        break;
      case HandleType::ParametricLineShoulder:
        setCursor(Qt::SizeHorCursor);
        emit status_message_requested("Arrow Shoulder Line: Drag to move arrowhead base & stretch attached lines");
        break;
      default:
        if (spatial_index_) {
          auto hits = spatial_index_->query_point(current_world);
          if (!hits.empty()) {
            const core::NodeId top_hit = pick_topmost_node(hits);
            if (std::find(selected_nodes_.begin(), selected_nodes_.end(), top_hit) != selected_nodes_.end()) {
              setCursor(Qt::SizeAllCursor);
            } else {
              setCursor(Qt::PointingHandCursor);
            }
          } else {
            setCursor(tool_mode_ == ToolMode::Select ? Qt::ArrowCursor : Qt::CrossCursor);
          }
        } else {
          setCursor(Qt::ArrowCursor);
        }
        break;
    }

    emit cursor_position_changed(QString("X: %1 mm   Y: %2 mm  |  (%3, %4)")
        .arg(static_cast<int>(current_world.x * 0.264583))
        .arg(static_cast<int>(current_world.y * 0.264583))
        .arg(static_cast<int>(current_world.x))
        .arg(static_cast<int>(current_world.y)));
  }

  last_mouse_screen_ = current_screen;
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
  (void)event;
  if (is_space_panning_) {
    if (interaction_state_ == InteractionState::Panning) {
      interaction_state_ = InteractionState::None;
      setCursor(Qt::OpenHandCursor);
      return;
    }
  }

  if (interaction_state_ == InteractionState::InstantiatingResize) {
    const core::Vec2d cur_scr = last_mouse_screen_;
    const double drag_dist = (cur_scr - instantiation_press_screen_).length();
    if (drag_dist >= 8.0) {
      finalize_instantiation();
      return;
    } else {
      instantiation_drag_active_ = false;
      instantiation_two_click_active_ = true;
      return;
    }
  }

  if (interaction_state_ == InteractionState::DraggingParametricHandle) {
    if (command_history_ && graph_model_ && active_handle_node_ != core::InvalidNodeId) {
      if (const auto* n = graph_model_->find_node(active_handle_node_)) {
        if (n->polygon_vertices != parametric_initial_vertices_ || n->properties != parametric_initial_props_) {
          command_history_->execute(std::make_unique<core::ModifyShapeGeometryCommand>(
              *graph_model_, active_handle_node_,
              parametric_initial_vertices_, n->polygon_vertices,
              parametric_initial_props_, n->properties));
          emit diagram_modified();
        }
      }
    }
    interaction_state_ = InteractionState::None;
    active_handle_ = HandleType::None;
    active_handle_node_ = core::InvalidNodeId;
    update();
    return;
  }

  if (interaction_state_ == InteractionState::WiringConnector) {
    if (wiring_click_origin_locked_) {
      const core::Vec2d cur_scr = last_mouse_screen_;
      const core::Vec2d start_scr = world_to_screen(interaction_start_world_);
      const double drag_dist = (cur_scr - start_scr).length();
      if (drag_dist < 10.0) {
        // Just clicked: keep origin locked, awaiting click 2
        return;
      }
    }

    core::NodeId target_node = core::InvalidNodeId;
    core::PortId target_port = find_hit_port(wiring_current_target_, 20.0, &target_node);
    if (target_node == core::InvalidNodeId && spatial_index_) {
      auto hits = spatial_index_->query_point(wiring_current_target_);
      if (!hits.empty()) {
        target_node = pick_topmost_node(hits);
      }
    }

    if (target_node != core::InvalidNodeId &&
        (target_node != wiring_source_node_ || target_port != wiring_source_port_)) {
      auto_route_arrow(wiring_source_node_, wiring_source_port_, target_node, target_port);
    }

    wiring_click_origin_locked_ = false;
    wiring_source_node_ = core::InvalidNodeId;
    wiring_source_port_ = core::InvalidPortId;
    interaction_state_ = InteractionState::None;
    update();
  } else if (interaction_state_ == InteractionState::SelectingMarquee) {
    if (spatial_index_ && graph_model_) {
      auto hits = spatial_index_->query_box(selection_marquee_);
      selected_nodes_ = hits;
      for (auto& [id, n] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
        n.selected = std::find(hits.begin(), hits.end(), id) != hits.end();
      }
      emit selection_changed();
    }
    selection_marquee_ = {};
  } else if (interaction_state_ == InteractionState::ResizingNode) {
    active_smart_guides_.clear();
    if (command_history_ && graph_model_ && active_handle_node_ != core::InvalidNodeId) {
      if (const auto* n = graph_model_->find_node(active_handle_node_)) {
        command_history_->execute(std::make_unique<core::ResizeNodeCommand>(
            *graph_model_, active_handle_node_, resize_initial_bounds_, n->bounds));
      }
    }
    active_handle_ = HandleType::None;
    active_handle_node_ = core::InvalidNodeId;
  } else if (interaction_state_ == InteractionState::RotatingNode) {
    if (command_history_ && graph_model_ && active_handle_node_ != core::InvalidNodeId) {
      if (const auto* n = graph_model_->find_node(active_handle_node_)) {
        if (std::abs(n->rotation_deg - rotation_initial_deg_) > 1e-4) {
          command_history_->execute(std::make_unique<core::RotateNodeCommand>(
              *graph_model_, active_handle_node_, rotation_initial_deg_, n->rotation_deg));
          emit diagram_modified();
        }
      }
    }
    active_handle_ = HandleType::None;
    active_handle_node_ = core::InvalidNodeId;
  } else if (interaction_state_ == InteractionState::DraggingNodes) {
    active_smart_guides_.clear();
    const bool was_dragged = (std::abs(drag_accumulated_delta_.x) > 1e-4 || std::abs(drag_accumulated_delta_.y) > 1e-4);
    if (!was_dragged && !(event->modifiers() & Qt::ShiftModifier) && selected_nodes_.size() > 1) {
      if (spatial_index_) {
        auto hits = spatial_index_->query_point(interaction_start_world_);
        if (!hits.empty()) {
          const core::NodeId clicked_node = pick_topmost_node(hits, interaction_start_world_);
          if (clicked_node != core::InvalidNodeId) {
            for (auto& [id, n] : const_cast<std::unordered_map<core::NodeId, core::Node>&>(graph_model_->nodes())) {
              n.selected = (id == clicked_node);
            }
            selected_nodes_ = {clicked_node};
            emit selection_changed();
          }
        }
      }
    } else if (command_history_ && graph_model_ && !selected_nodes_.empty() && was_dragged) {
      for (core::NodeId id : selected_nodes_) {
        if (auto* n = graph_model_->find_node(id)) {
          auto it = drag_initial_bounds_.find(id);
          if (it != drag_initial_bounds_.end()) {
            n->bounds = it->second;
          }
        }
      }
      command_history_->execute(std::make_unique<core::MoveNodesCommand>(
          *graph_model_, selected_nodes_, drag_accumulated_delta_));
      graph_model_->update_all_edge_routes();
      refresh_spatial_index();
    }
    drag_initial_bounds_.clear();
    drag_accumulated_delta_ = {0.0, 0.0};
  }

  interaction_state_ = InteractionState::None;
  update_lod_state();
  update();
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
  const double num_degrees = event->angleDelta().y() / 8.0;
  const double zoom_factor = std::pow(1.0015, num_degrees * 15.0);
  const core::Vec2d cursor_screen = {static_cast<double>(event->position().x()),
                                     static_cast<double>(event->position().y())};
  zoom_at(cursor_screen, zoom_factor);
  update_lod_state();
  update();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
  if (tool_mode_ == ToolMode::AddPolygon) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      if (in_progress_polygon_points_.size() >= 3) {
        finish_polygon_creation();
        event->accept();
        return;
      }
    } else if (event->key() == Qt::Key_Escape) {
      cancel_polygon_creation();
      set_tool_mode(ToolMode::Select);
      event->accept();
      return;
    }
  }

  if (event->key() == Qt::Key_Colon || event->key() == Qt::Key_Slash || event->key() == Qt::Key_F2) {
    emit focus_console_requested();
    event->accept();
    return;
  }
  if (event->matches(QKeySequence::Undo) && command_history_) {
    command_history_->undo();
    refresh_spatial_index();
    update();
  } else if (event->matches(QKeySequence::Redo) && command_history_) {
    command_history_->redo();
    refresh_spatial_index();
    update();
  } else if (event->matches(QKeySequence::Copy)) {
    copy_selected();
    event->accept();
    return;
  } else if (event->matches(QKeySequence::Cut)) {
    cut_selected();
    event->accept();
    return;
  } else if (event->matches(QKeySequence::Paste)) {
    paste_clipboard();
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_D) {
    duplicate_selected();
    event->accept();
    return;
  } else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
             event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
    const double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
    core::Vec2d delta{0.0, 0.0};
    if (event->key() == Qt::Key_Left) delta.x = -step;
    else if (event->key() == Qt::Key_Right) delta.x = step;
    else if (event->key() == Qt::Key_Up) delta.y = -step;
    else if (event->key() == Qt::Key_Down) delta.y = step;
    nudge_selected(delta);
    event->accept();
    return;
  } else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
    delete_selected();
  } else if (event->key() == Qt::Key_Escape) {
    if (wiring_click_origin_locked_ || interaction_state_ == InteractionState::WiringConnector) {
      wiring_click_origin_locked_ = false;
      wiring_source_node_ = core::InvalidNodeId;
      wiring_source_port_ = core::InvalidPortId;
      interaction_state_ = InteractionState::None;
    }
    if (is_placement_tool_active()) {
      cancel_placement();
    }
    clear_selection();
    if (tool_mode_ != ToolMode::Select) {
      set_tool_mode(ToolMode::Select);
    }
    update();
    event->accept();
    return;
  } else if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    is_space_panning_ = true;
    setCursor(Qt::OpenHandCursor);
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_1) {
    set_tool_mode(ToolMode::Select);
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_3) {
    set_tool_mode(ToolMode::AddConnector);
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_8) {
    set_tool_mode(ToolMode::AddRectangle);
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_9) {
    set_tool_mode(ToolMode::AddEllipse);
    event->accept();
    return;
  } else if (event->modifiers() == Qt::NoModifier && (event->key() == Qt::Key_S || event->key() == Qt::Key_V)) {
    set_tool_mode(ToolMode::Select);
  } else if (event->modifiers() == Qt::NoModifier && (event->key() == Qt::Key_C || event->key() == Qt::Key_W)) {
    set_tool_mode(ToolMode::AddConnector);
  } else if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_R) {
    set_tool_mode(ToolMode::AddRectangle);
  } else if (event->modifiers() == Qt::NoModifier && (event->key() == Qt::Key_E || event->key() == Qt::Key_O)) {
    set_tool_mode(ToolMode::AddEllipse);
  } else if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_U) {
    set_tool_mode(ToolMode::AddRoundedRect);
  } else if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_H) {
    set_tool_mode(ToolMode::AddHierarchicalBlock);
  } else if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_P) {
    set_tool_mode(ToolMode::AddPolygon);
  } else if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_I) {
    set_tool_mode(ToolMode::AddInputPort);
  } else if (event->key() == Qt::Key_F3) {
    fit_content();
    update();
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_F9) {
    if (grid_manager_) {
      auto cfg = grid_manager_->config();
      cfg.visible = !cfg.visible;
      grid_manager_->set_config(cfg);
      update();
    }
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::AltModifier) && event->key() == Qt::Key_A) {
    set_angle_snapping_enabled(!is_angle_snapping_enabled());
    update();
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_R) {
    rotate_selected(90.0);
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_R) {
    rotate_selected(-90.0);
    event->accept();
    return;
  } else if ((event->modifiers() & Qt::ControlModifier) && !(event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_L) {
    rotate_selected(-90.0);
    event->accept();
    return;
  } else if (event->matches(QKeySequence::SelectAll)) {
    select_all();
  } else if (event->key() == Qt::Key_F9) {
    if (event->modifiers() & Qt::AltModifier) {
      emit snap_settings_requested();
    } else if (grid_manager_) {
      grid_manager_->set_snap_enabled(!grid_manager_->is_snap_enabled());
      emit snap_mode_toggled(grid_manager_->is_snap_enabled());
      update();
    }
    event->accept();
    return;
  } else if (event->key() == Qt::Key_B && (event->modifiers() & Qt::ControlModifier)) {
    if (grid_manager_) {
      grid_manager_->set_snap_enabled(!grid_manager_->is_snap_enabled());
      emit snap_mode_toggled(grid_manager_->is_snap_enabled());
      update();
    }
    event->accept();
    return;
  }
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    is_space_panning_ = false;
    if (interaction_state_ == InteractionState::Panning) {
      interaction_state_ = InteractionState::None;
    }
    setCursor(tool_mode_ == ToolMode::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void CanvasWidget::contextMenuEvent(QContextMenuEvent* event) {
  if (show_rulers_ && (event->pos().x() <= 24 || event->pos().y() <= 24)) {
    emit ruler_context_menu_requested(event->globalPos());
    event->accept();
    return;
  }

  // Hit test under right-click
  const core::Vec2d click_world = screen_to_world({
    static_cast<double>(event->pos().x()),
    static_cast<double>(event->pos().y())
  });

  if (spatial_index_) {
    auto hits = spatial_index_->query_point(click_world);
    if (!hits.empty()) {
      const core::NodeId clicked_node = pick_topmost_node(hits);
      if (std::find(selected_nodes_.begin(), selected_nodes_.end(), clicked_node) == selected_nodes_.end()) {
        select_node(clicked_node, true);
      }
    }
  }

  if (!selected_nodes_.empty()) {
    QMenu menu(this);
    menu.setStyleSheet(
      "QMenu { background-color: #2D3748; color: #F7FAFC; border: 1px solid #4A5568; padding: 4px; border-radius: 4px; }"
      "QMenu::item { padding: 6px 24px; border-radius: 2px; }"
      "QMenu::item:selected { background-color: #3182CE; }"
      "QMenu::separator { height: 1px; background-color: #4A5568; margin: 4px 8px; }"
    );

    auto* order_menu = menu.addMenu("Order");
    order_menu->setStyleSheet(menu.styleSheet());

    auto* act_front = order_menu->addAction("Bring to Front");
    act_front->setShortcut(QKeySequence("Ctrl+Shift+]"));
    connect(act_front, &QAction::triggered, this, &CanvasWidget::bring_to_front_selected);

    auto* act_fwd = order_menu->addAction("Bring Forward");
    act_fwd->setShortcut(QKeySequence("Ctrl+]"));
    connect(act_fwd, &QAction::triggered, this, &CanvasWidget::bring_forward_selected);

    auto* act_back = order_menu->addAction("Send Backward");
    act_back->setShortcut(QKeySequence("Ctrl+["));
    connect(act_back, &QAction::triggered, this, &CanvasWidget::send_backward_selected);

    auto* act_bottom = order_menu->addAction("Send to Back");
    act_bottom->setShortcut(QKeySequence("Ctrl+Shift+["));
    connect(act_bottom, &QAction::triggered, this, &CanvasWidget::send_to_back_selected);

    auto* rot_menu = menu.addMenu("Rotate");
    rot_menu->setStyleSheet(menu.styleSheet());
    auto* act_rot_90_cw = rot_menu->addAction("Rotate 90° Clockwise");
    act_rot_90_cw->setShortcut(QKeySequence("Ctrl+R"));
    connect(act_rot_90_cw, &QAction::triggered, this, [this]() { rotate_selected(90.0); });

    auto* act_rot_90_ccw = rot_menu->addAction("Rotate 90° Counter-Clockwise");
    act_rot_90_ccw->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(act_rot_90_ccw, &QAction::triggered, this, [this]() { rotate_selected(-90.0); });

    auto* act_rot_180 = rot_menu->addAction("Rotate 180°");
    connect(act_rot_180, &QAction::triggered, this, [this]() { rotate_selected(180.0); });

    auto* act_rot_reset = rot_menu->addAction("Reset Rotation (0°)");
    connect(act_rot_reset, &QAction::triggered, this, [this]() { set_selected_nodes_rotation(0.0); });

    menu.addSeparator();

    if (can_merge_selected()) {
      auto* bool_menu = menu.addMenu("Boolean Operations");
      bool_menu->setStyleSheet(menu.styleSheet());
      auto* act_u = bool_menu->addAction("Union / Merge");
      connect(act_u, &QAction::triggered, this, &CanvasWidget::boolean_union_selected);
      auto* act_i = bool_menu->addAction("Intersection");
      connect(act_i, &QAction::triggered, this, &CanvasWidget::boolean_intersect_selected);
      auto* act_d = bool_menu->addAction("Subtract / Difference");
      connect(act_d, &QAction::triggered, this, &CanvasWidget::boolean_subtract_selected);
      auto* act_x = bool_menu->addAction("XOR / Symmetric Difference");
      connect(act_x, &QAction::triggered, this, &CanvasWidget::boolean_xor_selected);
    }

    if (can_split_selected()) {
      auto* act_split = menu.addAction("Split Figure");
      connect(act_split, &QAction::triggered, this, &CanvasWidget::split_selected_figure);
    }

    menu.addSeparator();
    auto* act_del = menu.addAction("Delete");
    act_del->setShortcut(QKeySequence::Delete);
    connect(act_del, &QAction::triggered, this, &CanvasWidget::delete_selected);

    menu.exec(event->globalPos());
    event->accept();
    return;
  }

  // Hit test under right-click for wires / edges
  if (selected_nodes_.empty()) {
    const core::EdgeId hit_edge = find_hit_edge(click_world, 8.0 / std::max(0.1, zoom_level_));
    const core::EdgeId target_edge = (hit_edge != core::InvalidEdgeId) ? hit_edge : selected_edge_;

    if (target_edge != core::InvalidEdgeId && graph_model_) {
      select_edge(target_edge, true);
      if (auto* edge = graph_model_->find_edge(target_edge)) {
        QMenu menu(this);
        menu.setStyleSheet(
          "QMenu { background-color: #2D3748; color: #F7FAFC; border: 1px solid #4A5568; padding: 4px; border-radius: 4px; }"
          "QMenu::item { padding: 6px 24px; border-radius: 2px; }"
          "QMenu::item:selected { background-color: #3182CE; }"
          "QMenu::separator { height: 1px; background-color: #4A5568; margin: 4px 8px; }"
        );

        auto* act_title = menu.addAction(QString::fromStdString(edge->name));
        act_title->setEnabled(false);
        menu.addSeparator();

        if (edge->is_multipoint()) {
          auto* act_align = menu.addAction("Align Multipoint Lines");
          act_align->setCheckable(true);
          act_align->setChecked(edge->is_branches_aligned());
          connect(act_align, &QAction::toggled, this, [this, eid = target_edge](bool checked) {
            set_edge_branches_aligned(eid, checked);
          });
        }

        auto* act_del = menu.addAction("Delete Wire");
        act_del->setShortcut(QKeySequence::Delete);
        connect(act_del, &QAction::triggered, this, &CanvasWidget::delete_selected);

        menu.exec(event->globalPos());
        event->accept();
        return;
      }
    }
  }

  QOpenGLWidget::contextMenuEvent(event);
}

void CanvasWidget::leaveEvent(QEvent* event) {
  QOpenGLWidget::leaveEvent(event);
  if (is_placement_tool_active()) {
    placement_preview_bounds_ = {0.0, 0.0, 0.0, 0.0};
    active_smart_guides_.clear();
    update();
  }
}

void CanvasWidget::enterEvent(QEnterEvent* event) {
  QOpenGLWidget::enterEvent(event);
  if (is_placement_tool_active()) {
    last_mouse_screen_ = {static_cast<double>(event->position().x()),
                          static_cast<double>(event->position().y())};
    const core::Vec2d current_world = screen_to_world(last_mouse_screen_);
    core::Rect2d cand_bounds = get_placement_bounds(tool_mode_, current_world);
    const core::Vec2d snap_offset = compute_smart_alignment(core::InvalidNodeId, cand_bounds);
    cand_bounds.x += snap_offset.x;
    cand_bounds.y += snap_offset.y;
    placement_preview_bounds_ = cand_bounds;
    update();
  }
}

void CanvasWidget::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasFormat("application/x-vector-stencil") || event->mimeData()->hasUrls()) {
    if (event->mimeData()->hasFormat("application/x-vector-stencil")) {
      const QByteArray bytes = event->mimeData()->data("application/x-vector-stencil");
      deserialize_placement_template(bytes, drag_template_);
      is_drag_active_ = true;

      const QPointF pos = event->position();
      last_mouse_screen_ = {pos.x(), pos.y()};
      const core::Vec2d current_world = screen_to_world(last_mouse_screen_);
      core::Rect2d cand_bounds{current_world.x - drag_template_.width * 0.5,
                               current_world.y - drag_template_.height * 0.5,
                               drag_template_.width,
                               drag_template_.height};
      const core::Vec2d snap_offset = compute_smart_alignment(core::InvalidNodeId, cand_bounds);
      cand_bounds.x += snap_offset.x;
      cand_bounds.y += snap_offset.y;
      placement_preview_bounds_ = cand_bounds;
      update();
    }
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}

void CanvasWidget::dragMoveEvent(QDragMoveEvent* event) {
  if (event->mimeData()->hasFormat("application/x-vector-stencil")) {
    const QPointF pos = event->position();
    last_mouse_screen_ = {pos.x(), pos.y()};
    const core::Vec2d current_world = screen_to_world(last_mouse_screen_);
    core::Rect2d cand_bounds{current_world.x - drag_template_.width * 0.5,
                             current_world.y - drag_template_.height * 0.5,
                             drag_template_.width,
                             drag_template_.height};
    if (!(event->modifiers() & Qt::AltModifier)) {
      const core::Vec2d snap_offset = compute_smart_alignment(core::InvalidNodeId, cand_bounds);
      cand_bounds.x += snap_offset.x;
      cand_bounds.y += snap_offset.y;
    } else {
      active_smart_guides_.clear();
    }
    placement_preview_bounds_ = cand_bounds;
    update();
    event->acceptProposedAction();
  } else if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}

void CanvasWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  is_drag_active_ = false;
  placement_preview_bounds_ = {0.0, 0.0, 0.0, 0.0};
  active_smart_guides_.clear();
  update();
  event->accept();
}

void CanvasWidget::dropEvent(QDropEvent* event) {
  if (event->mimeData()->hasFormat("application/x-vector-stencil")) {
    const QByteArray bytes = event->mimeData()->data("application/x-vector-stencil");
    ShapePlacementTemplate tmpl = drag_template_;
    deserialize_placement_template(bytes, tmpl);

    core::Vec2d drop_pos;
    if (placement_preview_bounds_.is_valid() && placement_preview_bounds_.width > 0.0) {
      drop_pos = placement_preview_bounds_.center();
    } else {
      const QPointF pos = event->position();
      drop_pos = screen_to_world({pos.x(), pos.y()});
    }

    instantiate_template_at(tmpl, drop_pos);
    is_drag_active_ = false;
    placement_preview_bounds_ = {0.0, 0.0, 0.0, 0.0};
    active_smart_guides_.clear();
    update();
    event->acceptProposedAction();
  } else if (event->mimeData()->hasUrls()) {
    for (const auto& url : event->mimeData()->urls()) {
      const QString local = url.toLocalFile();
      if (!local.isEmpty()) {
        const QFileInfo fi(local);
        const QPointF pos = event->position();
        const core::Vec2d drop_pos = screen_to_world({pos.x(), pos.y()});
        add_subsystem(drop_pos, fi.baseName().toStdString(), true, local.toStdString());
      }
    }
    is_drag_active_ = false;
    placement_preview_bounds_ = {0.0, 0.0, 0.0, 0.0};
    active_smart_guides_.clear();
    update();
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}

void CanvasWidget::apply_parametric_drag(core::Node& node, HandleType handle, core::Vec2d unrot_cursor, bool symmetric) {
  const double bw = std::max(1.0, node.bounds.width);
  const double bh = std::max(1.0, node.bounds.height);
  const double nx = std::clamp((unrot_cursor.x - node.bounds.x) / bw, 0.0, 1.0);
  const double ny = std::clamp((unrot_cursor.y - node.bounds.y) / bh, 0.0, 1.0);

  const auto kind = core::get_parametric_kind(node);
  if (kind == core::ParametricShapeKind::Arrow && node.polygon_vertices.size() == 7) {
    double sx = node.polygon_vertices[1].x;
    double sy0 = node.polygon_vertices[0].y;
    double sy1 = node.polygon_vertices[6].y;
    double hy0 = node.polygon_vertices[2].y;
    double hy1 = node.polygon_vertices[4].y;
    double tx = node.polygon_vertices[3].x;
    double ty = node.polygon_vertices[3].y;

    if (handle == HandleType::ParametricPoint0) {
      sx = std::clamp(nx, 0.1, tx - 0.05);
      sy0 = std::clamp(ny, hy0 + 0.02, 0.49);
      if (symmetric) sy1 = 1.0 - sy0;
    } else if (handle == HandleType::ParametricPoint1) {
      sx = std::clamp(nx, 0.1, tx - 0.05);
      sy1 = std::clamp(ny, 0.51, hy1 - 0.02);
      if (symmetric) sy0 = 1.0 - sy1;
    } else if (handle == HandleType::ParametricPoint2) {
      hy0 = std::clamp(ny, 0.0, sy0 - 0.02);
      if (symmetric) hy1 = 1.0 - hy0;
    } else if (handle == HandleType::ParametricPoint3) {
      hy1 = std::clamp(ny, sy1 + 0.02, 1.0);
      if (symmetric) hy0 = 1.0 - hy1;
    } else if (handle == HandleType::ParametricPoint4) {
      tx = std::clamp(nx, sx + 0.05, 1.0);
      if (!symmetric) ty = std::clamp(ny, 0.1, 0.9);
      else ty = 0.5;
    } else if (handle == HandleType::ParametricLineShoulder) {
      sx = std::clamp(nx, 0.1, tx - 0.05);
    } else if (handle == HandleType::ParametricLineShaftTop) {
      sy0 = std::clamp(ny, hy0 + 0.02, 0.49);
      if (symmetric) sy1 = 1.0 - sy0;
    } else if (handle == HandleType::ParametricLineShaftBottom) {
      sy1 = std::clamp(ny, 0.51, hy1 - 0.02);
      if (symmetric) sy0 = 1.0 - sy1;
    }

    node.polygon_vertices[0] = {0.0, sy0};
    node.polygon_vertices[1] = {sx, sy0};
    node.polygon_vertices[2] = {sx, hy0};
    node.polygon_vertices[3] = {tx, ty};
    node.polygon_vertices[4] = {sx, hy1};
    node.polygon_vertices[5] = {sx, sy1};
    node.polygon_vertices[6] = {0.0, sy1};

    node.properties["parametric_type"] = "arrow";
    node.properties["arrow_shoulder_x"] = std::to_string(sx);
    node.properties["arrow_shaft_y0"] = std::to_string(sy0);
    node.properties["arrow_shaft_y1"] = std::to_string(sy1);
    node.properties["arrow_head_y0"] = std::to_string(hy0);
    node.properties["arrow_head_y1"] = std::to_string(hy1);
  } else if (kind == core::ParametricShapeKind::Trapezoid && node.polygon_vertices.size() == 4) {
    const double inset = std::clamp(nx, 0.0, 0.45);
    node.polygon_vertices[0] = {inset, 0.0};
    if (symmetric) {
      node.polygon_vertices[1] = {1.0 - inset, 0.0};
    }
    node.properties["parametric_type"] = "trapezoid";
  } else if (kind == core::ParametricShapeKind::Star && node.polygon_vertices.size() == 10) {
    const double dx = nx - 0.5;
    const double dy = ny - 0.5;
    const double inner_r = std::clamp(std::sqrt(dx * dx + dy * dy), 0.1, 0.45);
    for (int i = 0; i < 10; ++i) {
      const double r = (i % 2 == 0) ? 0.5 : inner_r;
      const double rad = -std::numbers::pi / 2.0 + (std::numbers::pi * i) / 5.0;
      node.polygon_vertices[i] = {0.5 + r * std::cos(rad), 0.5 + r * std::sin(rad)};
    }
    node.properties["parametric_type"] = "star";
  } else if (kind == core::ParametricShapeKind::Cross && node.polygon_vertices.size() == 12) {
    const double arm_t = std::clamp(nx, 0.1, 0.45);
    node.polygon_vertices[0] = {arm_t, 0.0};
    node.polygon_vertices[1] = {1.0 - arm_t, 0.0};
    node.polygon_vertices[2] = {1.0 - arm_t, arm_t};
    node.polygon_vertices[3] = {1.0, arm_t};
    node.polygon_vertices[4] = {1.0, 1.0 - arm_t};
    node.polygon_vertices[5] = {1.0 - arm_t, 1.0 - arm_t};
    node.polygon_vertices[6] = {1.0 - arm_t, 1.0};
    node.polygon_vertices[7] = {arm_t, 1.0};
    node.polygon_vertices[8] = {arm_t, 1.0 - arm_t};
    node.polygon_vertices[9] = {0.0, 1.0 - arm_t};
    node.polygon_vertices[10] = {0.0, arm_t};
    node.polygon_vertices[11] = {arm_t, arm_t};
    node.properties["parametric_type"] = "cross";
  } else if (kind == core::ParametricShapeKind::GenericPolygon && !node.polygon_vertices.empty()) {
    int idx = static_cast<int>(static_cast<uint8_t>(handle) - static_cast<uint8_t>(HandleType::ParametricPoint0));
    if (idx >= 0 && idx < static_cast<int>(node.polygon_vertices.size())) {
      node.polygon_vertices[idx] = {nx, ny};
    }
  }
}
#else
CanvasWidget::CanvasWidget()
    : renderer_(std::make_unique<CanvasRenderer>()) {}
CanvasWidget::~CanvasWidget() = default;
void CanvasWidget::set_tool_mode(ToolMode mode) { tool_mode_ = mode; }
void CanvasWidget::start_shape_placement(const ShapePlacementTemplate&) {}
core::NodeId CanvasWidget::instantiate_template_at(const ShapePlacementTemplate&, core::Vec2d) { return 0; }
core::NodeId CanvasWidget::add_shape(core::ShapeType, core::Vec2d, const std::string&, bool) { return 0; }
core::NodeId CanvasWidget::add_polygon(core::Vec2d, int, const std::string&, bool) { return 0; }
core::NodeId CanvasWidget::add_custom_polygon_shape(core::Vec2d, const std::vector<core::Vec2d>&, const std::string&, double, double, bool) { return 0; }
core::NodeId CanvasWidget::add_hierarchical_port(core::Vec2d, bool, const std::string&, bool) { return 0; }
void CanvasWidget::delete_selected() {}
void CanvasWidget::select_all() {}
void CanvasWidget::clear_selection() {}
void CanvasWidget::copy_selected() {}
void CanvasWidget::cut_selected() {}
void CanvasWidget::paste_clipboard(core::Vec2d, bool) {}
void CanvasWidget::duplicate_selected() {}
void CanvasWidget::nudge_selected(core::Vec2d) {}
void CanvasWidget::select_edge(core::EdgeId, bool) {}
core::EdgeId CanvasWidget::find_hit_edge(core::Vec2d, double) const { return core::InvalidEdgeId; }
bool CanvasWidget::drill_down_selected() { return false; }
bool CanvasWidget::pop_hierarchy_scope() { return false; }
void CanvasWidget::reset_hierarchy(core::GraphModel*) {}
void CanvasWidget::sync_all_hierarchy_symbols() {}
core::PortId CanvasWidget::find_hit_port(core::Vec2d, double, core::NodeId*) const { return 0; }
void CanvasWidget::start_inline_text_edit(core::NodeId) {}
void CanvasWidget::commit_inline_text_edit() {}
HandleType CanvasWidget::hit_test_handles(core::Vec2d, core::NodeId*) const { return HandleType::None; }
void CanvasWidget::update_smart_alignment_guides(core::NodeId) {}
core::Vec2d CanvasWidget::compute_smart_alignment(core::NodeId, const core::Rect2d&) { return {0.0, 0.0}; }
void CanvasWidget::compute_resize_alignment(core::NodeId, HandleType, core::Rect2d&, bool, bool) {}
bool CanvasWidget::bring_to_front_selected() { return false; }
bool CanvasWidget::send_to_back_selected() { return false; }
bool CanvasWidget::bring_forward_selected() { return false; }
bool CanvasWidget::send_backward_selected() { return false; }
void CanvasWidget::normalize_z_orders() {}
core::NodeId CanvasWidget::pick_topmost_node(const std::vector<core::NodeId>&, std::optional<core::Vec2d>) const { return core::InvalidNodeId; }
void CanvasWidget::set_selected_nodes_fill_color(const std::string&) {}
void CanvasWidget::set_selected_nodes_stroke_color(const std::string&) {}
void CanvasWidget::set_selected_nodes_text_color(const std::string&) {}
void CanvasWidget::set_selected_nodes_stroke_width(double) {}
void CanvasWidget::set_selected_nodes_font_family(const std::string&) {}
void CanvasWidget::set_selected_nodes_font_size(int) {}
void CanvasWidget::set_selected_nodes_font_bold(bool) {}
void CanvasWidget::set_selected_nodes_font_italic(bool) {}
void CanvasWidget::set_selected_nodes_font_underline(bool) {}
void CanvasWidget::reset_selected_nodes_style() {}
void CanvasWidget::rotate_selected(double) {}
void CanvasWidget::set_selected_nodes_rotation(double) {}
void CanvasWidget::set_node_rotation(core::NodeId, double) {}
double CanvasWidget::get_node_rotation(core::NodeId) const { return 0.0; }
void CanvasWidget::finalize_instantiation() {}
void CanvasWidget::apply_parametric_drag(core::Node&, HandleType, core::Vec2d, bool) {}
#endif

bool CanvasWidget::is_placement_tool_active() const noexcept {
  return tool_mode_ == ToolMode::AddRectangle ||
         tool_mode_ == ToolMode::AddRoundedRect ||
         tool_mode_ == ToolMode::AddEllipse ||
         tool_mode_ == ToolMode::AddHierarchicalBlock ||
         tool_mode_ == ToolMode::AddInputPort ||
         tool_mode_ == ToolMode::AddOutputPort ||
         tool_mode_ == ToolMode::AddCustomShape;
}

bool CanvasWidget::is_polygon_drawing_active() const noexcept {
  return tool_mode_ == ToolMode::AddPolygon;
}

core::Rect2d CanvasWidget::get_placement_bounds(ToolMode mode, core::Vec2d center_world) const {
  double w = 120.0;
  double h = 70.0;
  const auto anchor = active_template_.anchor_point;
  if (mode == ToolMode::AddCustomShape && active_template_.width > 0.0 && active_template_.height > 0.0) {
    w = active_template_.width;
    h = active_template_.height;
  } else {
    switch (mode) {
      case ToolMode::AddHierarchicalBlock:
        w = 180.0;
        h = 100.0;
        break;
      case ToolMode::AddPolygon:
        w = 120.0;
        h = 100.0;
        break;
      case ToolMode::AddInputPort:
      case ToolMode::AddOutputPort:
        w = 84.0;
        h = 28.0;
        break;
      case ToolMode::AddRectangle:
      case ToolMode::AddRoundedRect:
      case ToolMode::AddEllipse:
      default:
        w = 120.0;
        h = 70.0;
        break;
    }
  }
  return {center_world.x - anchor.x * w, center_world.y - anchor.y * h, w, h};
}

void CanvasWidget::cancel_placement() {
  placement_preview_bounds_ = {0.0, 0.0, 0.0, 0.0};
  active_smart_guides_.clear();
  in_progress_polygon_points_.clear();
  interaction_state_ = InteractionState::None;
  instantiation_drag_active_ = false;
  instantiation_two_click_active_ = false;
  set_tool_mode(ToolMode::Select);
#ifdef VECTOR_ENABLE_QT
  update();
#endif
}

bool CanvasWidget::finish_polygon_creation() {
  if (in_progress_polygon_points_.size() < 3 || !graph_model_) {
    cancel_polygon_creation();
    set_tool_mode(ToolMode::Select);
    return false;
  }

  const auto pts = in_progress_polygon_points_;
  double min_x = pts[0].x, max_x = pts[0].x;
  double min_y = pts[0].y, max_y = pts[0].y;
  for (const auto& p : pts) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }

  const double w = std::max(20.0, max_x - min_x);
  const double h = std::max(20.0, max_y - min_y);

  core::Node node;
  node.shape = core::ShapeType::CustomPolygon;
  node.name = "Polygon " + std::to_string(graph_model_->node_count() + 1);
  node.bounds = {min_x, min_y, w, h};
  node.z_order = graph_model_->get_max_z_order() + 1;
  node.polygon_vertices.clear();
  node.polygon_vertices.reserve(pts.size());
  for (const auto& p : pts) {
    node.polygon_vertices.push_back({(p.x - min_x) / w, (p.y - min_y) / h});
  }

  const std::string node_name_copy = node.name;
  core::NodeId nid = core::InvalidNodeId;
  if (command_history_) {
    auto cmd = std::make_unique<core::AddNodeCommand>(*graph_model_, std::move(node));
    command_history_->execute(std::move(cmd));
    nid = graph_model_->node_count();
  } else {
    nid = graph_model_->add_node(std::move(node));
  }

  // Attach 4 cardinal connection ports (North, East, South, West)
  const std::array<std::tuple<std::string, core::Vec2d, core::PortDirection>, 4> port_defs = {
    std::tuple{"N", core::Vec2d{0.5, 0.0}, core::PortDirection::North},
    std::tuple{"E", core::Vec2d{1.0, 0.5}, core::PortDirection::East},
    std::tuple{"S", core::Vec2d{0.5, 1.0}, core::PortDirection::South},
    std::tuple{"W", core::Vec2d{0.0, 0.5}, core::PortDirection::West}
  };

  for (const auto& [pname, anchor, dir] : port_defs) {
    core::Port p;
    p.parent_node = nid;
    p.name = pname;
    p.local_anchor = anchor;
    p.direction = dir;
    graph_model_->add_port(std::move(p));
  }

  in_progress_polygon_points_.clear();
  set_tool_mode(ToolMode::Select);
  refresh_spatial_index();
#ifdef VECTOR_ENABLE_QT
  update();
  emit diagram_modified();
  emit status_message_requested(QString("Created %1 with %2 vertices.")
      .arg(QString::fromStdString(node_name_copy))
      .arg(pts.size()));
#endif
  return true;
}

void CanvasWidget::cancel_polygon_creation() {
  in_progress_polygon_points_.clear();
#ifdef VECTOR_ENABLE_QT
  update();
#endif
}

void CanvasWidget::pan_by(core::Vec2d delta) {
  camera_offset_ += delta;
}

void CanvasWidget::zoom_at(core::Vec2d screen_pivot, double zoom_factor) {
  const double old_zoom = zoom_level_;
  const double new_zoom = std::clamp(old_zoom * zoom_factor, 1e-5, 1e5);

  const core::Vec2d world_pivot = screen_to_world(screen_pivot);
  zoom_level_ = new_zoom;

  camera_offset_.x = screen_pivot.x - world_pivot.x * zoom_level_;
  camera_offset_.y = screen_pivot.y - world_pivot.y * zoom_level_;
}

void CanvasWidget::reset_view() {
  zoom_level_ = 1.0;
  camera_offset_ = {0.0, 0.0};
}

void CanvasWidget::fit_content() {
  core::Rect2d bbox = graph_model_ ? graph_model_->get_bounding_box() : core::Rect2d{};
  if (show_page_sheet_) {
    if (bbox.is_empty()) {
      bbox = page_bounds_;
    } else {
      bbox = bbox.united(page_bounds_);
    }
  }
  if (bbox.is_empty()) return;

  const double padding = 40.0;
  const double scale_x = (viewport_width_ - 2.0 * padding) / bbox.width;
  const double scale_y = (viewport_height_ - 2.0 * padding) / bbox.height;
  zoom_level_ = std::clamp(std::min(scale_x, scale_y), 0.05, 5.0);

  const core::Vec2d center = bbox.center();
  camera_offset_.x = viewport_width_ * 0.5 - center.x * zoom_level_;
  camera_offset_.y = viewport_height_ * 0.5 - center.y * zoom_level_;
  update_lod_state();
  emit zoom_level_changed(zoom_level_);
}

void CanvasWidget::set_zoom_level(double z) {
  const double old_zoom = zoom_level_;
  const double new_zoom = std::clamp(z, 0.01, 10.0);
  if (std::abs(old_zoom - new_zoom) < 1e-6) return;

  const core::Vec2d center_screen = {viewport_width_ * 0.5, viewport_height_ * 0.5};
  const core::Vec2d center_world = screen_to_world(center_screen);

  zoom_level_ = new_zoom;
  camera_offset_.x = center_screen.x - center_world.x * zoom_level_;
  camera_offset_.y = center_screen.y - center_world.y * zoom_level_;

  update_lod_state();
  update();
#ifdef VECTOR_ENABLE_QT
  emit zoom_level_changed(zoom_level_);
#endif
}

#ifdef VECTOR_ENABLE_QT
void CanvasWidget::start_inline_text_edit(core::NodeId id) {
  if (!graph_model_ || !inline_editor_) return;
  auto* node = graph_model_->find_node(id);
  if (!node) return;

  editing_node_ = id;
  const core::Rect2d srect = {
    world_to_screen({node->bounds.x, node->bounds.y}),
    {node->bounds.width * zoom_level_, node->bounds.height * zoom_level_}
  };

  const int ew = std::max(60, static_cast<int>(srect.width - 16));
  const int eh = std::clamp(static_cast<int>(srect.height * 0.4), 22, 34);
  const int ex = static_cast<int>(srect.x + (srect.width - ew) * 0.5);
  const int ey = static_cast<int>(srect.y + (srect.height - eh) * 0.5);

  inline_editor_->setText(QString::fromStdString(node->name));
  inline_editor_->setGeometry(ex, ey, ew, eh);
  inline_editor_->setVisible(true);
  inline_editor_->setFocus();
  inline_editor_->selectAll();
}

void CanvasWidget::commit_inline_text_edit() {
  if (editing_node_ == core::InvalidNodeId || !graph_model_ || !inline_editor_) {
    if (inline_editor_) inline_editor_->setVisible(false);
    return;
  }
  if (auto* node = graph_model_->find_node(editing_node_)) {
    const std::string new_name = inline_editor_->text().trimmed().toStdString();
    if (!new_name.empty() && node->name != new_name) {
      node->name = new_name;
      emit diagram_modified();
    }
  }
  editing_node_ = core::InvalidNodeId;
  inline_editor_->setVisible(false);
  update();
}

HandleType CanvasWidget::hit_test_handles(core::Vec2d screen_pt, core::NodeId* out_node) const {
  if (out_node) *out_node = core::InvalidNodeId;
  if (!graph_model_ || selected_nodes_.size() != 1) {
    return HandleType::None;
  }

  const core::NodeId sel_id = selected_nodes_.front();
  const auto* node = graph_model_->find_node(sel_id);
  if (!node) return HandleType::None;

  if (out_node) *out_node = sel_id;

  const core::Vec2d tl = world_to_screen({node->bounds.x, node->bounds.y});
  const double sw = node->bounds.width * zoom_level_;
  const double sh = node->bounds.height * zoom_level_;
  const double x0 = tl.x;
  const double y0 = tl.y;
  const double x1 = tl.x + sw;
  const double y1 = tl.y + sh;
  const double xc = (x0 + x1) * 0.5;
  const double yc = (y0 + y1) * 0.5;

  core::Vec2d test_pt = screen_pt;
  if (std::abs(node->rotation_deg) > 1e-4) {
    const double rad = -node->rotation_deg * (std::numbers::pi / 180.0);
    const double cos_a = std::cos(rad);
    const double sin_a = std::sin(rad);
    const double dx = screen_pt.x - xc;
    const double dy = screen_pt.y - yc;
    test_pt = {xc + dx * cos_a - dy * sin_a, yc + dx * sin_a + dy * cos_a};
  }

  auto hit_square = [test_pt](double cx, double cy, double half_sz) {
    return (test_pt.x >= cx - half_sz && test_pt.x <= cx + half_sz &&
            test_pt.y >= cy - half_sz && test_pt.y <= cy + half_sz);
  };

  auto hit_circle = [test_pt](double cx, double cy, double r) {
    const double dx = test_pt.x - cx;
    const double dy = test_pt.y - cy;
    return (dx * dx + dy * dy) <= (r * r);
  };

  const double handle_hs = 5.5;

  // 0. Parametric Handles (Points & Attached Lines)
  const auto p_kind = core::get_parametric_kind(*node);
  if (p_kind != core::ParametricShapeKind::None) {
    const auto p_handles = core::get_parametric_handles(*node);
    for (const auto& ph : p_handles) {
      const double px = x0 + ph.normalized_pos.x * sw;
      const double py = y0 + ph.normalized_pos.y * sh;
      if (hit_circle(px, py, 7.5)) {
        return static_cast<HandleType>(static_cast<uint8_t>(HandleType::ParametricPoint0) + ph.id);
      }
    }

    if (p_kind == core::ParametricShapeKind::Arrow && node->polygon_vertices.size() == 7) {
      auto dist_to_seg = [](core::Vec2d p, core::Vec2d a, core::Vec2d b) {
        const double l2 = a.distance_sq_to(b);
        if (l2 < 1e-6) return p.distance_to(a);
        const double t = std::clamp(((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / l2, 0.0, 1.0);
        const core::Vec2d proj = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
        return p.distance_to(proj);
      };

      const auto& pv = node->polygon_vertices;
      const core::Vec2d p0_s = {x0 + pv[0].x * sw, y0 + pv[0].y * sh};
      const core::Vec2d p1_s = {x0 + pv[1].x * sw, y0 + pv[1].y * sh};
      const core::Vec2d p2_s = {x0 + pv[2].x * sw, y0 + pv[2].y * sh};
      const core::Vec2d p4_s = {x0 + pv[4].x * sw, y0 + pv[4].y * sh};
      const core::Vec2d p5_s = {x0 + pv[5].x * sw, y0 + pv[5].y * sh};
      const core::Vec2d p6_s = {x0 + pv[6].x * sw, y0 + pv[6].y * sh};

      if (dist_to_seg(test_pt, p2_s, p4_s) <= 5.5) {
        return HandleType::ParametricLineShoulder;
      }
      if (dist_to_seg(test_pt, p0_s, p1_s) <= 5.5) {
        return HandleType::ParametricLineShaftTop;
      }
      if (dist_to_seg(test_pt, p5_s, p6_s) <= 5.5) {
        return HandleType::ParametricLineShaftBottom;
      }
    }
  }

  // 1. Rotation Stalk Lollipop Handle (22px above TopCenter)
  if (hit_circle(xc, y0 - 22.0, 7.5)) {
    return HandleType::RotationStalk;
  }

  // 2. 8 Resize Handles
  if (hit_square(x0, y0, handle_hs)) return HandleType::TopLeft;
  if (hit_square(xc, y0, handle_hs)) return HandleType::TopCenter;
  if (hit_square(x1, y0, handle_hs)) return HandleType::TopRight;
  if (hit_square(x1, yc, handle_hs)) return HandleType::MiddleRight;
  if (hit_square(x1, y1, handle_hs)) return HandleType::BottomRight;
  if (hit_square(xc, y1, handle_hs)) return HandleType::BottomCenter;
  if (hit_square(x0, y1, handle_hs)) return HandleType::BottomLeft;
  if (hit_square(x0, yc, handle_hs)) return HandleType::MiddleLeft;

  // 3. 4 Quick-Connect Arrows (18px outside edges)
  const double qc_r = 10.0;
  if (hit_circle(xc, y0 - 18.0, qc_r)) return HandleType::QuickConnectNorth;
  if (hit_circle(x1 + 18.0, yc, qc_r)) return HandleType::QuickConnectEast;
  if (hit_circle(xc, y1 + 18.0, qc_r)) return HandleType::QuickConnectSouth;
  if (hit_circle(x0 - 18.0, yc, qc_r)) return HandleType::QuickConnectWest;

  return HandleType::None;
}

core::Vec2d CanvasWidget::compute_smart_alignment(core::NodeId dragged_node, const core::Rect2d& proposed_bounds) {
  active_smart_guides_.clear();
  if (!graph_model_) return {0.0, 0.0};
  if (grid_manager_ && !grid_manager_->is_snap_enabled()) return {0.0, 0.0};

  const double tol = grid_manager_ ? grid_manager_->snap_tolerance() : 6.0;
  if (tol <= 1e-4) return {0.0, 0.0};
  const double snap_dist = tol / std::max(0.2, zoom_level_);

  const bool snap_bounds = !grid_manager_ || grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::ShapeBounds);
  const bool snap_center = !grid_manager_ || grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::ShapeCenter);
  const bool snap_grid = grid_manager_ && grid_manager_->is_snap_enabled() &&
                         grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid);

  const double d_left = proposed_bounds.x;
  const double d_cx = proposed_bounds.center().x;
  const double d_right = proposed_bounds.right();

  const double d_top = proposed_bounds.y;
  const double d_cy = proposed_bounds.center().y;
  const double d_bot = proposed_bounds.bottom();

  double best_x_dist = snap_dist + 1.0;
  double best_x_diff = 0.0;
  double best_x_guide = 0.0;
  core::NodeId best_x_other = core::InvalidNodeId;

  double best_y_dist = snap_dist + 1.0;
  double best_y_diff = 0.0;
  double best_y_guide = 0.0;
  core::NodeId best_y_other = core::InvalidNodeId;

  if (show_smart_guides_) {
    struct AlignCand {
      double d_val;
      double t_val;
      bool enabled;
    };

    for (const auto& [nid, other] : graph_model_->nodes()) {
      if (nid == dragged_node || !other.visible) continue;
      if (std::find(selected_nodes_.begin(), selected_nodes_.end(), nid) != selected_nodes_.end()) continue;

      const double o_left = other.bounds.x;
      const double o_cx = other.bounds.center().x;
      const double o_right = other.bounds.right();

      const double o_top = other.bounds.y;
      const double o_cy = other.bounds.center().y;
      const double o_bot = other.bounds.bottom();

      // Vertical guide lines (X alignment)
      const AlignCand x_cands[] = {
        {d_left, o_left, snap_bounds},
        {d_right, o_right, snap_bounds},
        {d_left, o_right, snap_bounds},
        {d_right, o_left, snap_bounds},
        {d_cx, o_cx, snap_center},
        {d_left, o_cx, snap_bounds && snap_center},
        {d_right, o_cx, snap_bounds && snap_center},
        {d_cx, o_left, snap_bounds && snap_center},
        {d_cx, o_right, snap_bounds && snap_center}
      };

      for (const auto& c : x_cands) {
        if (!c.enabled) continue;
        const double diff = c.t_val - c.d_val;
        const double dist = std::abs(diff);
        if (dist <= snap_dist && dist < best_x_dist) {
          best_x_dist = dist;
          best_x_diff = diff;
          best_x_guide = c.t_val;
          best_x_other = nid;
        }
      }

      // Horizontal guide lines (Y alignment)
      const AlignCand y_cands[] = {
        {d_top, o_top, snap_bounds},
        {d_bot, o_bot, snap_bounds},
        {d_top, o_bot, snap_bounds},
        {d_bot, o_top, snap_bounds},
        {d_cy, o_cy, snap_center},
        {d_top, o_cy, snap_bounds && snap_center},
        {d_bot, o_cy, snap_bounds && snap_center},
        {d_cy, o_top, snap_bounds && snap_center},
        {d_cy, o_bot, snap_bounds && snap_center}
      };

      for (const auto& c : y_cands) {
        if (!c.enabled) continue;
        const double diff = c.t_val - c.d_val;
        const double dist = std::abs(diff);
        if (dist <= snap_dist && dist < best_y_dist) {
          best_y_dist = dist;
          best_y_diff = diff;
          best_y_guide = c.t_val;
          best_y_other = nid;
        }
      }
    }
  }

  core::Vec2d offset{0.0, 0.0};

  if (show_smart_guides_ && best_x_dist <= snap_dist && best_x_other != core::InvalidNodeId) {
    offset.x = best_x_diff;
    if (const auto* other = graph_model_->find_node(best_x_other)) {
      grid::SnapGuideLine guide;
      guide.is_vertical = true;
      const double min_y = std::min(proposed_bounds.y + best_y_diff, other->bounds.y) - 30.0;
      const double max_y = std::max(proposed_bounds.bottom() + best_y_diff, other->bounds.bottom()) + 30.0;
      guide.p1 = {best_x_guide, min_y};
      guide.p2 = {best_x_guide, max_y};
      active_smart_guides_.push_back(guide);
    }
  } else if (snap_grid) {
    const core::Vec2d pt = grid_manager_->snap_to_grid(proposed_bounds.top_left());
    const double diff_x = pt.x - proposed_bounds.x;
    if (is_placement_tool_active() || std::abs(diff_x) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_x() * 0.5) {
      offset.x = diff_x;
    }
  }

  if (show_smart_guides_ && best_y_dist <= snap_dist && best_y_other != core::InvalidNodeId) {
    offset.y = best_y_diff;
    if (const auto* other = graph_model_->find_node(best_y_other)) {
      grid::SnapGuideLine guide;
      guide.is_vertical = false;
      const double min_x = std::min(proposed_bounds.x + best_x_diff, other->bounds.x) - 30.0;
      const double max_x = std::max(proposed_bounds.right() + best_x_diff, other->bounds.right()) + 30.0;
      guide.p1 = {min_x, best_y_guide};
      guide.p2 = {max_x, best_y_guide};
      active_smart_guides_.push_back(guide);
    }
  } else if (snap_grid) {
    const core::Vec2d pt = grid_manager_->snap_to_grid(proposed_bounds.top_left());
    const double diff_y = pt.y - proposed_bounds.y;
    if (is_placement_tool_active() || std::abs(diff_y) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_y() * 0.5) {
      offset.y = diff_y;
    }
  }

  return offset;
}

void CanvasWidget::compute_resize_alignment(core::NodeId resizing_node, HandleType handle, core::Rect2d& in_out_bounds, bool from_center, bool keep_ratio) {
  active_smart_guides_.clear();
  if (!graph_model_) return;
  if (grid_manager_ && !grid_manager_->is_snap_enabled()) return;

  const double tol = grid_manager_ ? grid_manager_->snap_tolerance() : 6.0;
  if (tol <= 1e-4) return;
  const double snap_dist = tol / std::max(0.2, zoom_level_);

  const bool snap_bounds = !grid_manager_ || grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::ShapeBounds);
  const bool snap_center = !grid_manager_ || grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::ShapeCenter);
  const bool snap_grid = grid_manager_ && grid_manager_->is_snap_enabled() &&
                         grid::has_flag(grid_manager_->snap_flags(), grid::SnapFlags::Grid);

  const bool moves_left = (handle == HandleType::TopLeft || handle == HandleType::MiddleLeft || handle == HandleType::BottomLeft);
  const bool moves_right = (handle == HandleType::TopRight || handle == HandleType::MiddleRight || handle == HandleType::BottomRight);
  const bool moves_top = (handle == HandleType::TopLeft || handle == HandleType::TopCenter || handle == HandleType::TopRight);
  const bool moves_bottom = (handle == HandleType::BottomLeft || handle == HandleType::BottomCenter || handle == HandleType::BottomRight);

  if (!moves_left && !moves_right && !moves_top && !moves_bottom) return;

  const double fixed_r = (resize_initial_bounds_.width > 1e-4) ? resize_initial_bounds_.right() : in_out_bounds.right();
  const double fixed_l = (resize_initial_bounds_.width > 1e-4) ? resize_initial_bounds_.x : in_out_bounds.x;
  const double fixed_b = (resize_initial_bounds_.height > 1e-4) ? resize_initial_bounds_.bottom() : in_out_bounds.bottom();
  const double fixed_t = (resize_initial_bounds_.height > 1e-4) ? resize_initial_bounds_.y : in_out_bounds.y;

  const double d_left = in_out_bounds.x;
  const double d_right = in_out_bounds.right();
  const double d_top = in_out_bounds.y;
  const double d_bot = in_out_bounds.bottom();

  double best_x_dist = snap_dist + 1.0;
  double best_x_guide = 0.0;
  core::NodeId best_x_other = core::InvalidNodeId;

  double best_y_dist = snap_dist + 1.0;
  double best_y_guide = 0.0;
  core::NodeId best_y_other = core::InvalidNodeId;

  if (show_smart_guides_) {
    for (const auto& [nid, other] : graph_model_->nodes()) {
      if (nid == resizing_node || !other.visible) continue;

      const double o_left = other.bounds.x;
      const double o_cx = other.bounds.center().x;
      const double o_right = other.bounds.right();

      const double o_top = other.bounds.y;
      const double o_cy = other.bounds.center().y;
      const double o_bot = other.bounds.bottom();

      // Check X candidates
      if (moves_left || moves_right) {
        const double moving_x = moves_left ? d_left : d_right;
        const double target_match_w = moves_right ? (fixed_l + other.bounds.width) : (fixed_r - other.bounds.width);
        struct TargetCand { double target; bool enabled; };
        const TargetCand cands_x[] = {
          {o_left, snap_bounds},
          {o_right, snap_bounds},
          {o_cx, snap_center},
          {target_match_w, snap_bounds && (other.bounds.width >= 20.0)}
        };
        for (const auto& c : cands_x) {
          if (!c.enabled) continue;
          const double dist = std::abs(c.target - moving_x);
          if (dist <= snap_dist && dist < best_x_dist) {
            best_x_dist = dist;
            best_x_guide = c.target;
            best_x_other = nid;
          }
        }
      }

      // Check Y candidates
      if (moves_top || moves_bottom) {
        const double moving_y = moves_top ? d_top : d_bot;
        const double target_match_h = moves_bottom ? (fixed_t + other.bounds.height) : (fixed_b - other.bounds.height);
        struct TargetCand { double target; bool enabled; };
        const TargetCand cands_y[] = {
          {o_top, snap_bounds},
          {o_bot, snap_bounds},
          {o_cy, snap_center},
          {target_match_h, snap_bounds && (other.bounds.height >= 20.0)}
        };
        for (const auto& c : cands_y) {
          if (!c.enabled) continue;
          const double dist = std::abs(c.target - moving_y);
          if (dist <= snap_dist && dist < best_y_dist) {
            best_y_dist = dist;
            best_y_guide = c.target;
            best_y_other = nid;
          }
        }
      }
    }
  }

  const double aspect_ratio = (resize_initial_bounds_.height > 1e-4) ?
      (resize_initial_bounds_.width / resize_initial_bounds_.height) :
      ((in_out_bounds.height > 1e-4) ? (in_out_bounds.width / in_out_bounds.height) : 1.0);

  bool x_snapped = false;
  if (show_smart_guides_ && best_x_dist <= snap_dist && best_x_other != core::InvalidNodeId) {
    if (from_center) {
      const double cx = (resize_initial_bounds_.width > 1e-4) ? resize_initial_bounds_.center().x : in_out_bounds.center().x;
      const double half_w = std::abs(best_x_guide - cx);
      const double new_w = 2.0 * half_w;
      if (new_w >= 20.0) {
        in_out_bounds.x = cx - half_w;
        in_out_bounds.width = new_w;
        x_snapped = true;
      }
    } else if (moves_left) {
      const double new_w = fixed_r - best_x_guide;
      if (new_w >= 20.0) {
        in_out_bounds.x = best_x_guide;
        in_out_bounds.width = new_w;
        x_snapped = true;
      }
    } else if (moves_right) {
      const double new_w = best_x_guide - fixed_l;
      if (new_w >= 20.0) {
        in_out_bounds.width = new_w;
        x_snapped = true;
      }
    }

    if (x_snapped) {
      if (const auto* other = graph_model_->find_node(best_x_other)) {
        const double min_y = std::min(in_out_bounds.y, other->bounds.y) - 30.0;
        const double max_y = std::max(in_out_bounds.bottom(), other->bounds.bottom()) + 30.0;

        grid::SnapGuideLine guide;
        guide.is_vertical = true;
        guide.p1 = {best_x_guide, min_y};
        guide.p2 = {best_x_guide, max_y};
        active_smart_guides_.push_back(guide);

        // If the opposite side also matches other shape, show alignment line on both sides!
        if (moves_right && std::abs(in_out_bounds.x - other->bounds.x) <= snap_dist) {
          in_out_bounds.x = other->bounds.x;
          in_out_bounds.width = best_x_guide - other->bounds.x;
          grid::SnapGuideLine opp_guide;
          opp_guide.is_vertical = true;
          opp_guide.p1 = {other->bounds.x, min_y};
          opp_guide.p2 = {other->bounds.x, max_y};
          active_smart_guides_.push_back(opp_guide);
        } else if (moves_left && std::abs(in_out_bounds.right() - other->bounds.right()) <= snap_dist) {
          in_out_bounds.width = other->bounds.right() - best_x_guide;
          grid::SnapGuideLine opp_guide;
          opp_guide.is_vertical = true;
          opp_guide.p1 = {other->bounds.right(), min_y};
          opp_guide.p2 = {other->bounds.right(), max_y};
          active_smart_guides_.push_back(opp_guide);
        }
      }
    }
  } else if (snap_grid && (moves_left || moves_right)) {
    const double moving_x = moves_left ? in_out_bounds.x : in_out_bounds.right();
    const core::Vec2d pt = grid_manager_->snap_to_grid({moving_x, in_out_bounds.y});
    const double diff_x = pt.x - moving_x;
    if (std::abs(diff_x) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_x() * 0.5) {
      if (moves_left) {
        const double new_w = fixed_r - pt.x;
        if (new_w >= 20.0) {
          in_out_bounds.x = pt.x;
          in_out_bounds.width = new_w;
          x_snapped = true;
        }
      } else if (moves_right) {
        const double new_w = pt.x - fixed_l;
        if (new_w >= 20.0) {
          in_out_bounds.width = new_w;
          x_snapped = true;
        }
      }
    }
  }

  bool y_snapped = false;
  if (show_smart_guides_ && best_y_dist <= snap_dist && best_y_other != core::InvalidNodeId) {
    if (from_center) {
      const double cy = (resize_initial_bounds_.height > 1e-4) ? resize_initial_bounds_.center().y : in_out_bounds.center().y;
      const double half_h = std::abs(best_y_guide - cy);
      const double new_h = 2.0 * half_h;
      if (new_h >= 20.0) {
        in_out_bounds.y = cy - half_h;
        in_out_bounds.height = new_h;
        y_snapped = true;
      }
    } else if (moves_top) {
      const double new_h = fixed_b - best_y_guide;
      if (new_h >= 20.0) {
        in_out_bounds.y = best_y_guide;
        in_out_bounds.height = new_h;
        y_snapped = true;
      }
    } else if (moves_bottom) {
      const double new_h = best_y_guide - fixed_t;
      if (new_h >= 20.0) {
        in_out_bounds.height = new_h;
        y_snapped = true;
      }
    }

    if (y_snapped) {
      if (const auto* other = graph_model_->find_node(best_y_other)) {
        const double min_x = std::min(in_out_bounds.x, other->bounds.x) - 30.0;
        const double max_x = std::max(in_out_bounds.right(), other->bounds.right()) + 30.0;

        grid::SnapGuideLine guide;
        guide.is_vertical = false;
        guide.p1 = {min_x, best_y_guide};
        guide.p2 = {max_x, best_y_guide};
        active_smart_guides_.push_back(guide);

        // If the opposite side also matches other shape, show alignment line on both sides!
        if (moves_bottom && std::abs(in_out_bounds.y - other->bounds.y) <= snap_dist) {
          in_out_bounds.y = other->bounds.y;
          in_out_bounds.height = best_y_guide - other->bounds.y;
          grid::SnapGuideLine opp_guide;
          opp_guide.is_vertical = false;
          opp_guide.p1 = {min_x, other->bounds.y};
          opp_guide.p2 = {max_x, other->bounds.y};
          active_smart_guides_.push_back(opp_guide);
        } else if (moves_top && std::abs(in_out_bounds.bottom() - other->bounds.bottom()) <= snap_dist) {
          in_out_bounds.height = other->bounds.bottom() - best_y_guide;
          grid::SnapGuideLine opp_guide;
          opp_guide.is_vertical = false;
          opp_guide.p1 = {min_x, other->bounds.bottom()};
          opp_guide.p2 = {max_x, other->bounds.bottom()};
          active_smart_guides_.push_back(opp_guide);
        }
      }
    }
  } else if (snap_grid && (moves_top || moves_bottom)) {
    const double moving_y = moves_top ? in_out_bounds.y : in_out_bounds.bottom();
    const core::Vec2d pt = grid_manager_->snap_to_grid({in_out_bounds.x, moving_y});
    const double diff_y = pt.y - moving_y;
    if (std::abs(diff_y) <= snap_dist || snap_dist >= grid_manager_->snap_spacing_y() * 0.5) {
      if (moves_top) {
        const double new_h = fixed_b - pt.y;
        if (new_h >= 20.0) {
          in_out_bounds.y = pt.y;
          in_out_bounds.height = new_h;
          y_snapped = true;
        }
      } else if (moves_bottom) {
        const double new_h = pt.y - fixed_t;
        if (new_h >= 20.0) {
          in_out_bounds.height = new_h;
          y_snapped = true;
        }
      }
    }
  }

  if (keep_ratio) {
    if (x_snapped && !y_snapped) {
      const double target_h = std::max(20.0, in_out_bounds.width / aspect_ratio);
      if (from_center) {
        const double cy = (resize_initial_bounds_.height > 1e-4) ? resize_initial_bounds_.center().y : in_out_bounds.center().y;
        in_out_bounds.height = target_h;
        in_out_bounds.y = cy - target_h * 0.5;
      } else if (moves_top) {
        const double fixed_b = (resize_initial_bounds_.height > 1e-4) ? resize_initial_bounds_.bottom() : in_out_bounds.bottom();
        in_out_bounds.y = fixed_b - target_h;
        in_out_bounds.height = target_h;
      } else if (moves_bottom) {
        in_out_bounds.height = target_h;
      }
    } else if (y_snapped && !x_snapped) {
      const double target_w = std::max(20.0, in_out_bounds.height * aspect_ratio);
      if (from_center) {
        const double cx = (resize_initial_bounds_.width > 1e-4) ? resize_initial_bounds_.center().x : in_out_bounds.center().x;
        in_out_bounds.width = target_w;
        in_out_bounds.x = cx - target_w * 0.5;
      } else if (moves_left) {
        const double fixed_r = (resize_initial_bounds_.width > 1e-4) ? resize_initial_bounds_.right() : in_out_bounds.right();
        in_out_bounds.x = fixed_r - target_w;
        in_out_bounds.width = target_w;
      } else if (moves_right) {
        in_out_bounds.width = target_w;
      }
    }
  }
}

void CanvasWidget::update_smart_alignment_guides(core::NodeId dragged_node) {
  if (auto* dnode = graph_model_->find_node(dragged_node)) {
    compute_smart_alignment(dragged_node, dnode->bounds);
  }
}
#endif

core::Vec2d CanvasWidget::screen_to_world(core::Vec2d screen_pt) const noexcept {
  return {
    (screen_pt.x - camera_offset_.x) / zoom_level_,
    (screen_pt.y - camera_offset_.y) / zoom_level_
  };
}

core::Vec2d CanvasWidget::world_to_screen(core::Vec2d world_pt) const noexcept {
  return {
    world_pt.x * zoom_level_ + camera_offset_.x,
    world_pt.y * zoom_level_ + camera_offset_.y
  };
}

core::Rect2d CanvasWidget::visible_world_frustum() const noexcept {
  const core::Vec2d tl = screen_to_world({0.0, 0.0});
  const core::Vec2d br = screen_to_world({static_cast<double>(viewport_width_),
                                          static_cast<double>(viewport_height_)});
  return core::Rect2d::from_corners(tl, br);
}

void CanvasWidget::refresh_spatial_index() {
  if (!spatial_index_ || !graph_model_) return;
  spatial_index_->clear();
  for (const auto& [id, node] : graph_model_->nodes()) {
    if (node.visible) {
      spatial_index_->insert(id, node.bounding_box());
    }
  }
}

void CanvasWidget::update_lod_state() {
  if (graph_model_ && graph_model_->node_count() > 10000) {
    current_lod_ = LODLevel::Proxy;
    is_rapid_interacting_ = true;
#ifdef VECTOR_ENABLE_QT
    lod_restore_timer_.start(150);
#endif
  } else {
    current_lod_ = LODLevel::High;
  }
}

void CanvasWidget::render_canvas() {
  RenderContext ctx;
  ctx.zoom = zoom_level_;
  ctx.pan_offset = camera_offset_;
  ctx.viewport_width = viewport_width_;
  ctx.viewport_height = viewport_height_;
  ctx.device_pixel_ratio = dpr_;
  ctx.visible_frustum = visible_world_frustum();
  ctx.lod = current_lod_;

  ctx.world_to_screen = core::Transform2d{
    zoom_level_, 0.0, camera_offset_.x,
    0.0, zoom_level_, camera_offset_.y
  };
  if (auto inv = ctx.world_to_screen.inverted()) {
    ctx.screen_to_world = *inv;
  }

  renderer_->begin_frame(ctx);

  if (grid_manager_) {
    renderer_->draw_grid(*grid_manager_, ctx, diagram_style_.render_style);
  }

#ifndef VECTOR_ENABLE_QT
  if (graph_model_) {
    graph_model_->update_all_edge_routes();
    RenderStyle default_style = diagram_style_.render_style;

    std::vector<uint64_t> visible_node_ids;
    if (spatial_index_) {
      visible_node_ids = spatial_index_->query_frustum(ctx.visible_frustum);
    } else {
      for (const auto& [id, node] : graph_model_->nodes()) {
        visible_node_ids.push_back(id);
      }
    }

    std::vector<const core::Node*> sorted_nodes;
    sorted_nodes.reserve(visible_node_ids.size());
    for (uint64_t id : visible_node_ids) {
      if (const auto* node = graph_model_->find_node(id)) {
        sorted_nodes.push_back(node);
      }
    }
    std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const core::Node* a, const core::Node* b) {
      if (a->z_order != b->z_order) return a->z_order < b->z_order;
      return a->id < b->id;
    });

    for (const auto* node : sorted_nodes) {
      renderer_->draw_node(*node, ctx, default_style);

      if (ctx.lod == LODLevel::High) {
        for (core::PortId pid : node->port_ids) {
          if (const auto* p = graph_model_->find_port(pid)) {
            renderer_->draw_port(*p, node->resolve_port_position(*p), ctx);
          }
        }
      }
    }

    for (const auto& [eid, edge] : graph_model_->edges()) {
      if (edge.visible) {
        renderer_->draw_edge(edge, ctx, default_style);
      }
    }
  }

  // Draw active rubberband wire during interactive connector creation
  if (interaction_state_ == InteractionState::WiringConnector &&
      wiring_source_node_ != core::InvalidNodeId &&
      wiring_source_port_ != core::InvalidPortId &&
      graph_model_) {
    if (const auto* src_node = graph_model_->find_node(wiring_source_node_)) {
      if (const auto* src_port = graph_model_->find_port(wiring_source_port_)) {
        const core::Vec2d p_src = src_node->resolve_port_position(*src_port);
        renderer_->draw_pending_wire(p_src, wiring_current_target_, ctx);
      }
    }
  }

  if (!selection_marquee_.is_empty()) {
    renderer_->draw_selection_marquee(selection_marquee_, ctx);
  }
#else
  if (graph_model_) {
    graph_model_->update_all_edge_routes();
  }
#endif

  renderer_->end_frame();

#ifdef VECTOR_ENABLE_QT
  if (renderer_->backend_type() == RendererBackend::SoftwareRaster) {
    QPainter painter(this);
    if (!painter.isActive()) return;
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 1. Desk Canvas Background (Desk workspace tone)
    painter.fillRect(rect(), argb_to_qcolor(diagram_style_.canvas_bg));

    // 2. Bounded Page Sheet with Drop Shadow & Print Margins
    if (show_page_sheet_) {
      const core::Vec2d page_tl = world_to_screen({page_bounds_.x, page_bounds_.y});
      const double page_sw = page_bounds_.width * zoom_level_;
      const double page_sh = page_bounds_.height * zoom_level_;
      const QRectF page_rect(page_tl.x, page_tl.y, page_sw, page_sh);

      // Drop shadow (offset 5px)
      painter.fillRect(page_rect.translated(5.0, 5.0), QColor(0, 0, 0, 35));
      // Page fill
      painter.fillRect(page_rect, argb_to_qcolor(diagram_style_.page_bg));
      // Sheet border
      painter.setPen(QPen(argb_to_qcolor(diagram_style_.page_border), 1.0));
      painter.drawRect(page_rect);

      // Print margins
      const double margin = page_margin_ * zoom_level_;
      if (page_sw > margin * 2.0 && page_sh > margin * 2.0) {
        painter.setPen(QPen(QColor(0x94, 0xA3, 0xB8, 120), 1.0, Qt::DashLine));
        painter.drawRect(page_rect.adjusted(margin, margin, -margin, -margin));
      }
    }

    // 3. Composite Software Raster Buffer (Dot Lattice Grid)
    const QImage img(
        renderer_->software_buffer(),
        viewport_width_, viewport_height_,
        static_cast<qsizetype>(renderer_->software_buffer_pitch()),
        QImage::Format_ARGB32_Premultiplied);
    painter.drawImage(0, 0, img);

    // 4. Draw All Wires / Connection Nets with Antialiased Vector Geometry
    if (graph_model_) {
      auto draw_arrowhead_func = [&](const core::Vec2d& p_prev, const core::Vec2d& p_tip, const QColor& color) {
        const auto p_tip_s = ctx.world_to_screen.map_point(p_tip);
        const auto p_prev_s = ctx.world_to_screen.map_point(p_prev);
        double dx = p_tip_s.x - p_prev_s.x;
        double dy = p_tip_s.y - p_prev_s.y;
        double len = std::hypot(dx, dy);
        if (len < 1e-4) return;
        dx /= len;
        dy /= len;
        const double arrow_size = 9.0;
        const double arrow_width = 4.5;
        QPointF tip(p_tip_s.x, p_tip_s.y);
        QPointF base = tip - QPointF(dx * arrow_size, dy * arrow_size);
        QPointF left = base + QPointF(-dy * arrow_width, dx * arrow_width);
        QPointF right = base + QPointF(dy * arrow_width, -dx * arrow_width);
        QPainterPath head;
        head.moveTo(tip);
        head.lineTo(left);
        head.lineTo(right);
        head.closeSubpath();
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPath(head);
      };

      for (const auto& [eid, edge] : graph_model_->edges()) {
        if (!edge.visible || edge.waypoints.size() < 2) continue;

        QColor wire_color = argb_to_qcolor(diagram_style_.render_style.wire_color);
        auto it_w = edge.properties.find("stroke_color");
        if (it_w != edge.properties.end()) {
          QColor parsed(QString::fromStdString(it_w->second));
          if (parsed.isValid()) wire_color = parsed;
        }
        if (edge.selected) {
          wire_color = argb_to_qcolor(diagram_style_.render_style.selection_border);
        }

        double stroke_w = 1.5;
        auto it_sw = edge.properties.find("stroke_width");
        if (it_sw != edge.properties.end()) {
          bool ok = false;
          double parsed_sw = QString::fromStdString(it_sw->second).toDouble(&ok);
          if (ok && parsed_sw > 0.0) stroke_w = parsed_sw;
        }
        if (edge.selected) stroke_w = std::max(stroke_w, 2.0);

        QPainterPath wire_path;
        const auto p0_s = ctx.world_to_screen.map_point(edge.waypoints.front());
        wire_path.moveTo(QPointF(p0_s.x, p0_s.y));
        for (size_t i = 1; i < edge.waypoints.size(); ++i) {
          const auto pi_s = ctx.world_to_screen.map_point(edge.waypoints[i]);
          wire_path.lineTo(QPointF(pi_s.x, pi_s.y));
        }

        QPen wire_pen(wire_color, stroke_w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(wire_pen);
        painter.drawPath(wire_path);

        // Source anchor connection dot
        painter.setBrush(wire_color);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(p0_s.x, p0_s.y), 2.5, 2.5);

        // Terminal arrowhead for primary trunk
        if (!edge.arrowhead.empty() || edge.properties.count("arrowhead") > 0) {
          draw_arrowhead_func(edge.waypoints[edge.waypoints.size() - 2], edge.waypoints.back(), wire_color);
        }

        // Draw extra destination branches (multipoint arrows)
        for (const auto& branch : edge.extra_destinations) {
          if (branch.waypoints.size() < 2) continue;

          QPainterPath branch_path;
          const auto bp0_s = ctx.world_to_screen.map_point(branch.waypoints.front());
          branch_path.moveTo(QPointF(bp0_s.x, bp0_s.y));
          for (size_t i = 1; i < branch.waypoints.size(); ++i) {
            const auto bpi_s = ctx.world_to_screen.map_point(branch.waypoints[i]);
            branch_path.lineTo(QPointF(bpi_s.x, bpi_s.y));
          }
          painter.setBrush(Qt::NoBrush);
          painter.setPen(wire_pen);
          painter.drawPath(branch_path);

          // Junction ball at branch tap point
          painter.setBrush(wire_color);
          painter.setPen(Qt::NoPen);
          painter.drawEllipse(QPointF(bp0_s.x, bp0_s.y), 3.5, 3.5);

          // Terminal arrowhead for branch
          if (!branch.arrowhead.empty()) {
            draw_arrowhead_func(branch.waypoints[branch.waypoints.size() - 2], branch.waypoints.back(), wire_color);
          }
        }
      }
    }

    // 5. Wire Crossings & Junctions (Style 1: Modern Dot Priority vs Style 2: Legacy Bridge/Loop Priority)
    if (graph_model_) {
      struct HorizSeg { double y, x_min, x_max; core::EdgeId eid; };
      struct VertSeg { double x, y_min, y_max; core::EdgeId eid; };
      std::vector<HorizSeg> h_segs;
      std::vector<VertSeg> v_segs;

      auto collect_segs = [&](const std::vector<core::Vec2d>& wpts, core::EdgeId eid) {
        for (size_t i = 0; i + 1 < wpts.size(); ++i) {
          const auto& p1 = wpts[i];
          const auto& p2 = wpts[i + 1];
          if (std::abs(p1.y - p2.y) < 1e-4 && std::abs(p1.x - p2.x) > 2.0) {
            h_segs.push_back({p1.y, std::min(p1.x, p2.x), std::max(p1.x, p2.x), eid});
          } else if (std::abs(p1.x - p2.x) < 1e-4 && std::abs(p1.y - p2.y) > 2.0) {
            v_segs.push_back({p1.x, std::min(p1.y, p2.y), std::max(p1.y, p2.y), eid});
          }
        }
      };

      for (const auto& [eid, edge] : graph_model_->edges()) {
        if (!edge.visible || edge.waypoints.size() < 2) continue;
        collect_segs(edge.waypoints, eid);
        for (const auto& br : edge.extra_destinations) {
          if (br.waypoints.size() >= 2) {
            collect_segs(br.waypoints, eid);
          }
        }
      }

      for (const auto& hs : h_segs) {
        for (const auto& vs : v_segs) {
          if (vs.x > hs.x_min + 5.0 && vs.x < hs.x_max - 5.0 &&
              hs.y > vs.y_min + 5.0 && hs.y < vs.y_max - 5.0) {
            const core::Vec2d spt = world_to_screen({vs.x, hs.y});
            const bool same_edge = (hs.eid == vs.eid);

            if (wire_crossing_style_ == WireCrossingStyle::DotPriority) {
              // Style 1: Modern Dot Priority - crossing wires do not connect unless there is a junction ball.
              // Connected crossings (same net / junction) receive a connection ball; non-connected pass cleanly.
              if (same_edge) {
                painter.setBrush(argb_to_qcolor(diagram_style_.render_style.wire_color));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(QPointF(spt.x, spt.y), 3.5, 3.5);
              }
            } else {
              // Style 2: Legacy Bridge/Loop Priority - crossing wires connect by default, unless there is a little loop.
              // Non-connected crossings get the hop loop; connected crossings are straight 4-way junctions.
              if (!same_edge) {
                painter.fillRect(QRectF(spt.x - 7.0, spt.y - 2.0, 14.0, 4.0), argb_to_qcolor(diagram_style_.page_bg));
                QPainterPath hop;
                hop.arcMoveTo(QRectF(spt.x - 6.0, spt.y - 6.0, 12.0, 12.0), 180.0);
                hop.arcTo(QRectF(spt.x - 6.0, spt.y - 6.0, 12.0, 12.0), 180.0, -180.0);
                painter.setPen(QPen(argb_to_qcolor(diagram_style_.jump_bridge_color), 2.0));
                painter.drawPath(hop);
              }
            }
          }
        }
      }
    }

    // 6. Draw All Nodes & Figures with Subpixel Antialiased Vector Geometry
    if (graph_model_) {
      const QColor default_text_color = argb_to_qcolor(diagram_style_.render_style.text_color);
      QFont font = painter.font();
      font.setPointSize(std::clamp(static_cast<int>(10.0 * zoom_level_), 8, 14));
      painter.setFont(font);

      std::vector<const core::Node*> sorted_nodes;
      sorted_nodes.reserve(graph_model_->node_count());
      for (const auto& [nid, node] : graph_model_->nodes()) {
        sorted_nodes.push_back(&node);
      }
      std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const core::Node* a, const core::Node* b) {
        if (a->z_order != b->z_order) return a->z_order < b->z_order;
        return a->id < b->id;
      });

      for (const auto* p_node : sorted_nodes) {
        const auto& node = *p_node;
        const core::Rect2d srect = ctx.world_to_screen.map_rect(node.bounding_box());
        if (!srect.intersects(core::Rect2d(0, 0, viewport_width_, viewport_height_))) continue;

        const core::Vec2d node_tl_scr = ctx.world_to_screen.map_point({node.bounds.x, node.bounds.y});
        const double sw = node.bounds.width * zoom_level_;
        const double sh = node.bounds.height * zoom_level_;
        const QRectF qr_f(node_tl_scr.x, node_tl_scr.y, sw, sh);

        QColor fill_c = argb_to_qcolor(diagram_style_.render_style.fill_color);
        auto it_f = node.properties.find("fill_color");
        if (it_f != node.properties.end()) {
          QColor parsed(QString::fromStdString(it_f->second));
          if (parsed.isValid()) fill_c = parsed;
        }

        QColor stroke_c = argb_to_qcolor(diagram_style_.render_style.stroke_color);
        auto it_s = node.properties.find("stroke_color");
        if (it_s != node.properties.end()) {
          QColor parsed(QString::fromStdString(it_s->second));
          if (parsed.isValid()) stroke_c = parsed;
        }
        if (node.selected) {
          stroke_c = argb_to_qcolor(diagram_style_.render_style.selection_border);
        }

        double stroke_w = 1.5;
        auto it_sw = node.properties.find("stroke_width");
        if (it_sw != node.properties.end()) {
          bool ok = false;
          double parsed_sw = QString::fromStdString(it_sw->second).toDouble(&ok);
          if (ok && parsed_sw > 0.0) stroke_w = parsed_sw;
        }
        if (node.selected) stroke_w = std::max(stroke_w, 2.0);

        QPen strokePen(stroke_c, stroke_w, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin);
        strokePen.setMiterLimit(2.0);

        QColor node_text_color = default_text_color;
        auto it_tc = node.properties.find("text_color");
        if (it_tc != node.properties.end()) {
          QColor parsed(QString::fromStdString(it_tc->second));
          if (parsed.isValid()) node_text_color = parsed;
        }

        QFont node_font = font;
        auto it_ff = node.properties.find("font_family");
        if (it_ff != node.properties.end() && !it_ff->second.empty()) {
          node_font.setFamily(QString::fromStdString(it_ff->second));
        }
        int pt_size = 10;
        auto it_fs = node.properties.find("font_size");
        if (it_fs != node.properties.end()) {
          bool ok = false;
          int parsed_fs = QString::fromStdString(it_fs->second).toInt(&ok);
          if (ok && parsed_fs > 0) pt_size = parsed_fs;
        }
        node_font.setPointSize(std::max(1, static_cast<int>(pt_size * zoom_level_)));

        auto it_fb = node.properties.find("font_bold");
        node_font.setBold(it_fb != node.properties.end() && (it_fb->second == "true" || it_fb->second == "1"));

        auto it_fi = node.properties.find("font_italic");
        node_font.setItalic(it_fi != node.properties.end() && (it_fi->second == "true" || it_fi->second == "1"));

        auto it_fu = node.properties.find("font_underline");
        node_font.setUnderline(it_fu != node.properties.end() && (it_fu->second == "true" || it_fu->second == "1"));

        painter.setFont(node_font);

        const bool is_node_rotated = (std::abs(node.rotation_deg) > 1e-4);
        if (is_node_rotated) {
          painter.save();
          painter.translate(qr_f.center());
          painter.rotate(node.rotation_deg);
          painter.translate(-qr_f.center());
        }

        if (node.shape == core::ShapeType::Rectangle) {
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawRect(qr_f);
          if (current_lod_ == LODLevel::High) {
            painter.setPen(node_text_color);
            painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
          }
        } else if (node.shape == core::ShapeType::RoundedRect) {
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawRoundedRect(qr_f, 8.0, 8.0);
          if (current_lod_ == LODLevel::High) {
            painter.setPen(node_text_color);
            painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
          }
        } else if (node.shape == core::ShapeType::Ellipse) {
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawEllipse(qr_f);
          if (current_lod_ == LODLevel::High) {
            painter.setPen(node_text_color);
            painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
          }
        } else if (node.shape == core::ShapeType::CustomPolygon) {
          QPainterPath qpath;
          qpath.setFillRule(Qt::OddEvenFill);
          const auto world_contours = node.get_world_subpath_polygons();
          for (const auto& w_contour : world_contours) {
            if (w_contour.empty()) continue;
            QPolygonF qsub;
            qsub.reserve(static_cast<qsizetype>(w_contour.size()));
            for (const auto& wp : w_contour) {
              const auto sp = ctx.world_to_screen.map_point(wp);
              qsub << QPointF(sp.x, sp.y);
            }
            qpath.addPolygon(qsub);
            qpath.closeSubpath();
          }
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawPath(qpath);
          if (current_lod_ == LODLevel::High) {
            painter.setPen(node_text_color);
            painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
          }
        } else if (node.shape == core::ShapeType::HierarchicalBlock) {
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawRoundedRect(qr_f, 4.0, 4.0);

          const double header_h = std::min(24.0, qr_f.height() * 0.3);
          const QRectF banner_rect(qr_f.x(), qr_f.y(), qr_f.width(), header_h);
          QColor banner_c = node.selected
              ? argb_to_qcolor(diagram_style_.render_style.banner_selected)
              : argb_to_qcolor(diagram_style_.render_style.banner_color);
          auto it_bc = node.properties.find("banner_color");
          if (it_bc != node.properties.end()) {
            QColor parsed(QString::fromStdString(it_bc->second));
            if (parsed.isValid()) banner_c = parsed;
          }
          painter.setBrush(banner_c);
          painter.setPen(Qt::NoPen);
          painter.drawRoundedRect(banner_rect, 4.0, 4.0);
          painter.drawRect(QRectF(qr_f.x(), qr_f.y() + header_h - 4.0, qr_f.width(), 4.0));

          if (qr_f.width() > 12.0 && qr_f.height() > 12.0) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(stroke_c, 1.0, Qt::DashLine));
            painter.drawRect(QRectF(qr_f.x() + 3.0, qr_f.y() + header_h + 2.0, qr_f.width() - 6.0, qr_f.height() - header_h - 5.0));
          }

          if (current_lod_ == LODLevel::High) {
            painter.setPen(Qt::white);
            painter.drawText(banner_rect, Qt::AlignCenter, QString::fromStdString(node.name));

            painter.setPen(node.is_file_subsystem() ? argb_to_qcolor(diagram_style_.render_style.banner_color) : QColor(0x71, 0x80, 0x96));
            QFont sub_font = font;
            sub_font.setPointSize(std::max(7, font.pointSize() - 2));
            painter.setFont(sub_font);
            const QString sub_text = node.is_file_subsystem()
                ? QString("📁 %1").arg(QString::fromStdString(node.subsystem_file()))
                : "⚙ Built-in Subsystem";
            painter.drawText(QRectF(qr_f.x() + 4.0, qr_f.y() + header_h + 2.0, qr_f.width() - 8.0, qr_f.height() - header_h - 4.0),
                             Qt::AlignCenter, sub_text);

            // Draw port pins & names along symbol perimeter
            QFont pin_font = font;
            pin_font.setPointSize(std::max(6, font.pointSize() - 3));
            painter.setFont(pin_font);
            for (core::PortId pid : node.port_ids) {
              if (const auto* p = graph_model_->find_port(pid)) {
                const auto p_world = node.resolve_port_position(*p);
                const auto p_screen = ctx.world_to_screen.map_point(p_world);
                const bool is_in = (p->direction == core::PortDirection::West || p->local_anchor.x <= 0.2);

                painter.fillRect(QRectF(p_screen.x - 3.0, p_screen.y - 3.0, 6.0, 6.0),
                                 is_in ? QColor(0x10, 0xB9, 0x81) : QColor(0xF5, 0x9E, 0x0B));
                painter.setPen(QColor(0x1E, 0x29, 0x3B));
                painter.drawRect(QRectF(p_screen.x - 3.0, p_screen.y - 3.0, 6.0, 6.0));

                painter.setPen(node_text_color);
                if (is_in) {
                  painter.drawText(QRectF(p_screen.x + 6.0, p_screen.y - 8.0, 60.0, 16.0),
                                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromStdString(p->name));
                } else {
                  painter.drawText(QRectF(p_screen.x - 66.0, p_screen.y - 8.0, 60.0, 16.0),
                                   Qt::AlignRight | Qt::AlignVCenter, QString::fromStdString(p->name));
                }
              }
            }
            painter.setFont(font);
            painter.setPen(node_text_color);
          }
        } else if (node.shape == core::ShapeType::InputPort || node.shape == core::ShapeType::OutputPort) {
          const bool is_in = (node.shape == core::ShapeType::InputPort);
          QPolygonF badge;
          if (is_in) {
            badge << QPointF(qr_f.left(), qr_f.top())
                  << QPointF(qr_f.right() - 8.0, qr_f.top())
                  << QPointF(qr_f.right(), qr_f.center().y())
                  << QPointF(qr_f.right() - 8.0, qr_f.bottom())
                  << QPointF(qr_f.left(), qr_f.bottom());
          } else {
            badge << QPointF(qr_f.left(), qr_f.top())
                  << QPointF(qr_f.right() - 8.0, qr_f.top())
                  << QPointF(qr_f.right(), qr_f.center().y())
                  << QPointF(qr_f.right() - 8.0, qr_f.bottom())
                  << QPointF(qr_f.left(), qr_f.bottom())
                  << QPointF(qr_f.left() + 8.0, qr_f.center().y());
          }
          QColor bg_c = is_in ? QColor(0x10, 0xB9, 0x81) : QColor(0xF5, 0x9E, 0x0B);
          auto it_badge_f = node.properties.find("fill_color");
          if (it_badge_f != node.properties.end()) {
            QColor parsed(QString::fromStdString(it_badge_f->second));
            if (parsed.isValid()) bg_c = parsed;
          }
          painter.setBrush(node.selected ? argb_to_qcolor(diagram_style_.render_style.selection_border) : bg_c);
          painter.setPen(strokePen);
          painter.drawPolygon(badge);

          painter.setPen(Qt::white);
          const QString tag = QString::fromStdString(node.name);
          painter.drawText(qr_f.adjusted(is_in ? 2.0 : 10.0, 0.0, is_in ? -10.0 : -2.0, 0.0), Qt::AlignCenter, tag);
        }

        if (is_node_rotated) {
          painter.restore();
        }

        // Draw port connection pins for regular nodes in High LOD
        if (current_lod_ == LODLevel::High && node.shape != core::ShapeType::HierarchicalBlock) {
          for (core::PortId pid : node.port_ids) {
            if (const auto* p = graph_model_->find_port(pid)) {
              const auto p_world = node.resolve_port_position(*p);
              const auto p_screen = ctx.world_to_screen.map_point(p_world);
              painter.setBrush(QColor(0x31, 0x82, 0xCE));
              painter.setPen(QPen(QColor(0x1E, 0x29, 0x3B), 1.0));
              painter.drawEllipse(QPointF(p_screen.x, p_screen.y), 3.0, 3.0);
            }
          }
        }
      }
    }

    // Active Rubberband / Auto-Route Preview During Interactive Connector Creation
    if (interaction_state_ == InteractionState::WiringConnector &&
        wiring_source_node_ != core::InvalidNodeId &&
        graph_model_) {
      core::Vec2d p_src_w = interaction_start_world_;
      if (const auto* src_node = graph_model_->find_node(wiring_source_node_)) {
        if (wiring_source_port_ != core::InvalidPortId) {
          if (const auto* src_port = graph_model_->find_port(wiring_source_port_)) {
            p_src_w = src_node->resolve_port_position(*src_port);
          }
        } else {
          p_src_w = src_node->bounds.center();
        }
      }

      std::vector<core::Vec2d> preview_path = graph_model_->compute_orthogonal_path(
          p_src_w, wiring_current_target_, wiring_source_node_, core::InvalidNodeId);

      if (preview_path.size() >= 2) {
        QPainterPath path;
        const auto p0_s = world_to_screen(preview_path.front());
        path.moveTo(QPointF(p0_s.x, p0_s.y));
        for (size_t i = 1; i < preview_path.size(); ++i) {
          const auto pi_s = world_to_screen(preview_path[i]);
          path.lineTo(QPointF(pi_s.x, pi_s.y));
        }
        painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        const auto p_tip = preview_path.back();
        const auto p_prev = preview_path[preview_path.size() - 2];
        const auto p_tip_s = world_to_screen(p_tip);
        const auto p_prev_s = world_to_screen(p_prev);
        double dx = p_tip_s.x - p_prev_s.x;
        double dy = p_tip_s.y - p_prev_s.y;
        double len = std::hypot(dx, dy);
        if (len > 1e-4) {
          dx /= len;
          dy /= len;
          const double arrow_size = 9.0;
          const double arrow_width = 4.5;
          QPointF tip(p_tip_s.x, p_tip_s.y);
          QPointF base = tip - QPointF(dx * arrow_size, dy * arrow_size);
          QPointF left = base + QPointF(-dy * arrow_width, dx * arrow_width);
          QPointF right = base + QPointF(dy * arrow_width, -dx * arrow_width);
          QPainterPath head;
          head.moveTo(tip);
          head.lineTo(left);
          head.lineTo(right);
          head.closeSubpath();
          painter.setBrush(QColor(0x00, 0x78, 0xD4));
          painter.setPen(Qt::NoPen);
          painter.drawPath(head);
        }
      }
    }

    // Active Selection Marquee
    if (!selection_marquee_.is_empty()) {
      const core::Vec2d m_tl = world_to_screen({selection_marquee_.x, selection_marquee_.y});
      const double m_w = selection_marquee_.width * zoom_level_;
      const double m_h = selection_marquee_.height * zoom_level_;
      const QRectF mq_rect(m_tl.x, m_tl.y, m_w, m_h);
      painter.fillRect(mq_rect, QColor(0x00, 0x78, 0xD4, 30));
      painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 1.2, Qt::DashLine));
      painter.drawRect(mq_rect);
    }

    // 6. Smart Alignment Guides (Cyan Dashed Lines with Diamond Markers)
    if (show_smart_guides_ && !active_smart_guides_.empty()) {
      const QColor guide_c = argb_to_qcolor(diagram_style_.guide_color);
      painter.setPen(QPen(guide_c, 1.5, Qt::DashLine));
      painter.setBrush(guide_c);

      for (const auto& g : active_smart_guides_) {
        const auto sp1 = world_to_screen(g.p1);
        const auto sp2 = world_to_screen(g.p2);
        painter.drawLine(QPointF(sp1.x, sp1.y), QPointF(sp2.x, sp2.y));

        const double cx = (sp1.x + sp2.x) * 0.5;
        const double cy = (sp1.y + sp2.y) * 0.5;
        QPolygonF diamond;
        diamond << QPointF(cx, cy - 4.5) << QPointF(cx + 4.5, cy)
                << QPointF(cx, cy + 4.5) << QPointF(cx - 4.5, cy);
        painter.drawPolygon(diamond);
      }
    }

    // 7. Selection Manipulators: 8 Resize Handles + Rotation Lollipop + 4 Quick-Connect Arrows
    if (graph_model_ && selected_nodes_.size() == 1) {
      if (const auto* sel_node = graph_model_->find_node(selected_nodes_.front())) {
        const core::Vec2d tl = world_to_screen({sel_node->bounds.x, sel_node->bounds.y});
        const double sw = sel_node->bounds.width * zoom_level_;
        const double sh = sel_node->bounds.height * zoom_level_;
        const double x0 = tl.x;
        const double y0 = tl.y;
        const double x1 = tl.x + sw;
        const double y1 = tl.y + sh;
        const double xc = (x0 + x1) * 0.5;
        const double yc = (y0 + y1) * 0.5;

        const bool is_sel_rotated = (std::abs(sel_node->rotation_deg) > 1e-4);
        if (is_sel_rotated) {
          painter.save();
          painter.translate(QPointF(xc, yc));
          painter.rotate(sel_node->rotation_deg);
          painter.translate(-QPointF(xc, yc));
        }

        // Selection boundary
        if (sel_node->shape == core::ShapeType::CustomPolygon) {
          painter.setPen(QPen(QColor(0x00, 0x78, 0xD4, 140), 1.0, Qt::DashLine));
          painter.setBrush(Qt::NoBrush);
          painter.drawRect(QRectF(x0, y0, sw, sh));
        } else {
          painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 1.2, Qt::SolidLine));
          painter.setBrush(Qt::NoBrush);
          painter.drawRect(QRectF(x0, y0, sw, sh));
        }

        // Rotation stalk lollipop stem & handle
        painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 1.2));
        painter.drawLine(QPointF(xc, y0), QPointF(xc, y0 - 22.0));
        const bool rot_hover = (hovered_handle_ == HandleType::RotationStalk);
        painter.setBrush(rot_hover ? QColor(0x00, 0x78, 0xD4) : Qt::white);
        painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 1.5));
        painter.drawEllipse(QPointF(xc, y0 - 22.0), 5.5, 5.5);

        // 8 Resize Handles
        const std::array<std::pair<HandleType, QPointF>, 8> handle_pts = {
          std::pair{HandleType::TopLeft, QPointF(x0, y0)},
          std::pair{HandleType::TopCenter, QPointF(xc, y0)},
          std::pair{HandleType::TopRight, QPointF(x1, y0)},
          std::pair{HandleType::MiddleRight, QPointF(x1, yc)},
          std::pair{HandleType::BottomRight, QPointF(x1, y1)},
          std::pair{HandleType::BottomCenter, QPointF(xc, y1)},
          std::pair{HandleType::BottomLeft, QPointF(x0, y1)},
          std::pair{HandleType::MiddleLeft, QPointF(x0, yc)}
        };

        for (const auto& [htype, pt] : handle_pts) {
          const bool is_hov = (hovered_handle_ == htype);
          painter.setBrush(is_hov ? QColor(0x00, 0x78, 0xD4) : Qt::white);
          painter.setPen(QPen(is_hov ? QColor(0x00, 0x5A, 0x9E) : QColor(0x00, 0x78, 0xD4), 1.5));
          painter.drawRect(QRectF(pt.x() - 3.5, pt.y() - 3.5, 7.0, 7.0));
        }

        // 4 Quick-Connect Arrows (North, East, South, West)
        auto draw_qc_arrow = [&](HandleType htype, QPointF pos, qreal rot_deg) {
          const bool is_hov = (hovered_handle_ == htype);
          painter.save();
          painter.translate(pos);
          painter.rotate(rot_deg);
          QPolygonF arrow;
          arrow << QPointF(0, -6) << QPointF(5, 4) << QPointF(-5, 4);
          painter.setBrush(is_hov ? QColor(0x00, 0x5A, 0x9E) : QColor(0x00, 0x78, 0xD4, 210));
          painter.setPen(QPen(is_hov ? Qt::white : QColor(0x00, 0x78, 0xD4), 1.0));
          painter.drawPolygon(arrow);
          painter.restore();
        };

        draw_qc_arrow(HandleType::QuickConnectNorth, QPointF(xc, y0 - 16.0), 0.0);
        draw_qc_arrow(HandleType::QuickConnectEast, QPointF(x1 + 16.0, yc), 90.0);
        draw_qc_arrow(HandleType::QuickConnectSouth, QPointF(xc, y1 + 16.0), 180.0);
        draw_qc_arrow(HandleType::QuickConnectWest, QPointF(x0 - 16.0, yc), 270.0);

        // 5. Parametric shape handles (amber diamonds & line highlights)
        const auto p_kind = core::get_parametric_kind(*sel_node);
        if (p_kind != core::ParametricShapeKind::None) {
          if (p_kind == core::ParametricShapeKind::Arrow && sel_node->polygon_vertices.size() == 7) {
            const auto& pv = sel_node->polygon_vertices;
            const QPointF p0_s(x0 + pv[0].x * sw, y0 + pv[0].y * sh);
            const QPointF p1_s(x0 + pv[1].x * sw, y0 + pv[1].y * sh);
            const QPointF p2_s(x0 + pv[2].x * sw, y0 + pv[2].y * sh);
            const QPointF p4_s(x0 + pv[4].x * sw, y0 + pv[4].y * sh);
            const QPointF p5_s(x0 + pv[5].x * sw, y0 + pv[5].y * sh);
            const QPointF p6_s(x0 + pv[6].x * sw, y0 + pv[6].y * sh);

            const bool sh_hov = (hovered_handle_ == HandleType::ParametricLineShoulder ||
                                 (interaction_state_ == InteractionState::DraggingParametricHandle && active_handle_ == HandleType::ParametricLineShoulder));
            const bool st_hov = (hovered_handle_ == HandleType::ParametricLineShaftTop ||
                                 (interaction_state_ == InteractionState::DraggingParametricHandle && active_handle_ == HandleType::ParametricLineShaftTop));
            const bool sb_hov = (hovered_handle_ == HandleType::ParametricLineShaftBottom ||
                                 (interaction_state_ == InteractionState::DraggingParametricHandle && active_handle_ == HandleType::ParametricLineShaftBottom));

            if (sh_hov) {
              painter.setPen(QPen(QColor(0xFB, 0xBF, 0x24), 3.0, Qt::DashLine));
              painter.drawLine(p2_s, p4_s);
            }
            if (st_hov) {
              painter.setPen(QPen(QColor(0xFB, 0xBF, 0x24), 3.0, Qt::DashLine));
              painter.drawLine(p0_s, p1_s);
            }
            if (sb_hov) {
              painter.setPen(QPen(QColor(0xFB, 0xBF, 0x24), 3.0, Qt::DashLine));
              painter.drawLine(p5_s, p6_s);
            }
          }

          const auto p_handles = core::get_parametric_handles(*sel_node);
          for (const auto& ph : p_handles) {
            const double hx = x0 + ph.normalized_pos.x * sw;
            const double hy = y0 + ph.normalized_pos.y * sh;

            const HandleType target_h = static_cast<HandleType>(static_cast<uint8_t>(HandleType::ParametricPoint0) + ph.id);
            const bool is_hov = (hovered_handle_ == target_h);
            const bool is_act = (interaction_state_ == InteractionState::DraggingParametricHandle && active_handle_ == target_h);

            const double hsz = (is_hov || is_act) ? 6.5 : 5.0;
            QPolygonF diamond;
            diamond << QPointF(hx, hy - hsz)
                    << QPointF(hx + hsz, hy)
                    << QPointF(hx, hy + hsz)
                    << QPointF(hx - hsz, hy);

            painter.setBrush((is_hov || is_act) ? QColor(0xFB, 0xBF, 0x24) : QColor(0xF5, 0x9E, 0x0B));
            painter.setPen(QPen(QColor(0x78, 0x35, 0x0F), (is_hov || is_act) ? 2.0 : 1.5));
            painter.drawPolygon(diamond);

            painter.setPen(QPen(QColor(0xFF, 0xFF, 0xFF, 220), 1.0));
            painter.drawPoint(QPointF(hx, hy));
          }
        }

        if (is_sel_rotated) {
          painter.restore();
        }
      }
    }

    // Live Rotation Guide & Angle Badge during interactive rotation
    if (interaction_state_ == InteractionState::RotatingNode) {
      const auto sc_center = world_to_screen(rotation_center_world_);
      const auto sc_mouse = last_mouse_screen_;

      // Guide line from center to cursor
      painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 1.5, Qt::DashLine));
      painter.drawLine(QPointF(sc_center.x, sc_center.y), QPointF(sc_mouse.x, sc_mouse.y));

      // Center pivot point
      painter.setBrush(QColor(0x00, 0x78, 0xD4));
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPointF(sc_center.x, sc_center.y), 3.5, 3.5);

      // Angle Badge near cursor
      const QString angle_text = QString("%1°%2")
          .arg(current_rotating_angle_deg_, 0, 'f', 1)
          .arg(is_current_angle_snapped_ ? " [Snapped]" : "");

      QFont badge_font = painter.font();
      badge_font.setPointSize(9);
      badge_font.setBold(true);
      painter.setFont(badge_font);
      QFontMetrics fm(badge_font);
      const int bw = fm.horizontalAdvance(angle_text) + 16;
      const int bh = fm.height() + 8;
      const QRectF badge_rect(sc_mouse.x + 12.0, sc_mouse.y - bh - 6.0, bw, bh);

      painter.setBrush(QColor(0x1E, 0x29, 0x3B, 230));
      painter.setPen(QPen(is_current_angle_snapped_ ? QColor(0x00, 0xBC, 0xD4) : QColor(0x64, 0x74, 0x8B), 1.2));
      painter.drawRoundedRect(badge_rect, 4.0, 4.0);

      painter.setPen(is_current_angle_snapped_ ? QColor(0x38, 0xBD, 0xF8) : Qt::white);
      painter.drawText(badge_rect, Qt::AlignCenter, angle_text);
    }

    // 7.5 Candidate Placement Preview Silhouette (Moving Ghost Shape with Snapping & Pin Visuals)
    const bool is_preview_showing = (is_placement_tool_active() || is_drag_active_) &&
                                    placement_preview_bounds_.is_valid() &&
                                    placement_preview_bounds_.width > 0.0;
    if (is_preview_showing) {
      const core::Vec2d g_tl = world_to_screen({placement_preview_bounds_.x, placement_preview_bounds_.y});
      const double g_sw = placement_preview_bounds_.width * zoom_level_;
      const double g_sh = placement_preview_bounds_.height * zoom_level_;
      const QRectF qr_ghost(g_tl.x, g_tl.y, g_sw, g_sh);

      // Translucent fill & dashed silhouette border
      const QColor ghost_fill(0x00, 0x78, 0xD4, 40);
      const QPen ghost_pen(QColor(0x00, 0x78, 0xD4), 1.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
      painter.setPen(ghost_pen);
      painter.setBrush(ghost_fill);

      const auto& cur_tmpl = is_drag_active_ ? drag_template_ : active_template_;

      if (cur_tmpl.category == PlacementCategory::RegularPolygon) {
        const int sides = std::max(3, cur_tmpl.polygon_sides);
        const double r = std::min(qr_ghost.width(), qr_ghost.height()) * 0.5;
        const double cx = qr_ghost.center().x();
        const double cy = qr_ghost.center().y();
        QPolygonF poly;
        for (int i = 0; i < sides; ++i) {
          const double rad = -std::numbers::pi / 2.0 + (2.0 * std::numbers::pi * i) / sides;
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        painter.drawPolygon(poly);
      } else if (cur_tmpl.category == PlacementCategory::CustomPolygon) {
        if (!cur_tmpl.custom_vertices.empty()) {
          QPolygonF poly;
          for (const auto& pt : cur_tmpl.custom_vertices) {
            poly << QPointF(qr_ghost.left() + pt.x * qr_ghost.width(),
                            qr_ghost.top() + pt.y * qr_ghost.height());
          }
          painter.drawPolygon(poly);
        } else {
          painter.drawRect(qr_ghost);
        }
      } else if (cur_tmpl.category == PlacementCategory::HierarchicalPort) {
        const bool is_in = cur_tmpl.is_input_port;
        QPolygonF badge;
        if (is_in) {
          badge << QPointF(qr_ghost.left(), qr_ghost.top())
                << QPointF(qr_ghost.right() - 8.0, qr_ghost.top())
                << QPointF(qr_ghost.right(), qr_ghost.center().y())
                << QPointF(qr_ghost.right() - 8.0, qr_ghost.bottom())
                << QPointF(qr_ghost.left(), qr_ghost.bottom());
        } else {
          badge << QPointF(qr_ghost.left(), qr_ghost.top())
                << QPointF(qr_ghost.right() - 8.0, qr_ghost.top())
                << QPointF(qr_ghost.right(), qr_ghost.center().y())
                << QPointF(qr_ghost.right() - 8.0, qr_ghost.bottom())
                << QPointF(qr_ghost.left(), qr_ghost.bottom())
                << QPointF(qr_ghost.left() + 8.0, qr_ghost.center().y());
        }
        painter.setBrush(is_in ? QColor(0x10, 0xB9, 0x81, 70) : QColor(0xF5, 0x9E, 0x0B, 70));
        painter.drawPolygon(badge);
        painter.setPen(Qt::white);
        painter.drawText(qr_ghost, Qt::AlignCenter, QString::fromStdString(cur_tmpl.name.empty() ? (is_in ? "Input Port" : "Output Port") : cur_tmpl.name));
        painter.setPen(ghost_pen);
      } else if (cur_tmpl.category == PlacementCategory::SubsystemBuiltin ||
                 cur_tmpl.category == PlacementCategory::SubsystemFile ||
                 (cur_tmpl.category == PlacementCategory::ShapeType && cur_tmpl.shape_type == core::ShapeType::HierarchicalBlock)) {
        painter.drawRoundedRect(qr_ghost, 4.0, 4.0);
        const double header_h = std::min(24.0, qr_ghost.height() * 0.3);
        const QRectF b_rect(qr_ghost.x(), qr_ghost.y(), qr_ghost.width(), header_h);
        painter.fillRect(b_rect, QColor(0x1E, 0x29, 0x3B, 90));
        painter.setPen(QColor(0xFF, 0xFF, 0xFF, 220));
        painter.drawText(b_rect, Qt::AlignCenter, QString::fromStdString(cur_tmpl.name.empty() ? "Candidate Subsystem" : cur_tmpl.name));
        painter.setPen(ghost_pen);
      } else if (cur_tmpl.category == PlacementCategory::ShapeType && cur_tmpl.shape_type == core::ShapeType::RoundedRect) {
        painter.drawRoundedRect(qr_ghost, 8.0, 8.0);
      } else if (cur_tmpl.category == PlacementCategory::ShapeType && cur_tmpl.shape_type == core::ShapeType::Ellipse) {
        painter.drawEllipse(qr_ghost);
      } else {
        painter.drawRect(qr_ghost);
      }

      // Candidate cardinal connection ports (N, E, S, W)
      const QPointF pin_pts[] = {
        QPointF(qr_ghost.center().x(), qr_ghost.top()),
        QPointF(qr_ghost.right(), qr_ghost.center().y()),
        QPointF(qr_ghost.center().x(), qr_ghost.bottom()),
        QPointF(qr_ghost.left(), qr_ghost.center().y())
      };
      painter.setPen(QPen(QColor(0x1E, 0x29, 0x3B), 1.0));
      painter.setBrush(QColor(0x31, 0x82, 0xCE));
      for (const auto& pt : pin_pts) {
        painter.drawEllipse(pt, 3.0, 3.0);
      }

      // Candidate Center Target Crosshair
      const double ccx = qr_ghost.center().x();
      const double ccy = qr_ghost.center().y();
      painter.setPen(QPen(QColor(0x00, 0x78, 0xD4, 180), 1.0, Qt::DotLine));
      painter.drawLine(QPointF(ccx - 8.0, ccy), QPointF(ccx + 8.0, ccy));
      painter.drawLine(QPointF(ccx, ccy - 8.0), QPointF(ccx, ccy + 8.0));

      // Candidate Anchor Point Marker (Green crosshair)
      const double g_anc_x = qr_ghost.left() + cur_tmpl.anchor_point.x * qr_ghost.width();
      const double g_anc_y = qr_ghost.top() + cur_tmpl.anchor_point.y * qr_ghost.height();
      painter.setPen(QPen(QColor(0x10, 0xB9, 0x81), 2.0));
      painter.setBrush(QColor(0x10, 0xB9, 0x81, 140));
      painter.drawEllipse(QPointF(g_anc_x, g_anc_y), 4.5, 4.5);
      painter.drawLine(QPointF(g_anc_x - 6.0, g_anc_y), QPointF(g_anc_x + 6.0, g_anc_y));
      painter.drawLine(QPointF(g_anc_x, g_anc_y - 6.0), QPointF(g_anc_x, g_anc_y + 6.0));

      // Candidate Resize Control Point Handle (Cyan diamond)
      const double g_res_x = qr_ghost.left() + cur_tmpl.resize_point.x * qr_ghost.width();
      const double g_res_y = qr_ghost.top() + cur_tmpl.resize_point.y * qr_ghost.height();
      QPolygonF res_diamond;
      res_diamond << QPointF(g_res_x, g_res_y - 6.0)
                  << QPointF(g_res_x + 6.0, g_res_y)
                  << QPointF(g_res_x, g_res_y + 6.0)
                  << QPointF(g_res_x - 6.0, g_res_y);
      painter.setPen(QPen(QColor(0x00, 0xBC, 0xD4), 2.0));
      painter.setBrush(QColor(0x00, 0xBC, 0xD4, 180));
      painter.drawPolygon(res_diamond);

      if (interaction_state_ == InteractionState::InstantiatingResize) {
        painter.setPen(QPen(QColor(0x00, 0xBC, 0xD4, 180), 1.5, Qt::DashLine));
        painter.drawLine(QPointF(g_anc_x, g_anc_y), QPointF(g_res_x, g_res_y));
      }

      // Floating Coordinate & Dimension Badge
      const bool is_aligned = !active_smart_guides_.empty();
      const bool is_resizing = (interaction_state_ == InteractionState::InstantiatingResize);
      const QString shape_label = QString::fromStdString(cur_tmpl.name.empty() ? "Shape" : cur_tmpl.name);
      const QString badge_str = QString("%1%2: (%3, %4)  %5×%6%7")
          .arg(is_resizing ? "[Resizing] " : "")
          .arg(shape_label)
          .arg(static_cast<int>(placement_preview_bounds_.center().x))
          .arg(static_cast<int>(placement_preview_bounds_.center().y))
          .arg(static_cast<int>(placement_preview_bounds_.width))
          .arg(static_cast<int>(placement_preview_bounds_.height))
          .arg(is_aligned ? "  [Aligned]" : "");

      const QFont badge_font("sans-serif", 8, QFont::Bold);
      painter.setFont(badge_font);
      const QFontMetrics fm(badge_font);
      const int bw = fm.horizontalAdvance(badge_str) + 12;
      const int bh = fm.height() + 6;
      const QRectF b_rect(qr_ghost.right() - bw, qr_ghost.top() - bh - 6.0, bw, bh);

      painter.setPen(Qt::NoPen);
      painter.setBrush(is_aligned ? QColor(0x00, 0xBC, 0xD4, 210) : QColor(0x1E, 0x29, 0x3B, 200));
      painter.drawRoundedRect(b_rect, 4.0, 4.0);
      painter.setPen(Qt::white);
      painter.drawText(b_rect, Qt::AlignCenter, badge_str);
    }

    // 7.6 In-Progress Point-and-Click Polygon Creation Overlay
    if (tool_mode_ == ToolMode::AddPolygon) {
      const auto s_cur = world_to_screen(polygon_cursor_world_);
      const QPointF q_cur(s_cur.x, s_cur.y);

      if (!in_progress_polygon_points_.empty()) {
        QPolygonF q_placed;
        q_placed.reserve(static_cast<qsizetype>(in_progress_polygon_points_.size()));
        for (const auto& pt : in_progress_polygon_points_) {
          const auto sp = world_to_screen(pt);
          q_placed << QPointF(sp.x, sp.y);
        }

        // 1. Translucent preview polygon fill formed by placed vertices + current cursor
        QPolygonF q_preview = q_placed;
        q_preview << q_cur;
        painter.setBrush(QColor(0x00, 0x78, 0xD4, 35));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(q_preview);

        // 2. Solid edges between all placed vertices
        painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        for (qsizetype i = 1; i < q_placed.size(); ++i) {
          painter.drawLine(q_placed[i - 1], q_placed[i]);
        }

        // 3. Dynamic rubberband line from last placed vertex to current cursor
        painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 2.0, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(q_placed.back(), q_cur);

        // 4. Closing guide line from cursor back to the first vertex
        if (q_placed.size() >= 2) {
          painter.setPen(QPen(QColor(0x00, 0xBC, 0xD4, 180), 1.5, Qt::DotLine));
          painter.drawLine(q_cur, q_placed.front());
        }

        // 5. Vertex handles & numbers
        const bool is_near_start = (q_placed.size() >= 3 &&
            polygon_cursor_world_.distance_sq_to(in_progress_polygon_points_.front()) < 1e-4);

        for (qsizetype i = 0; i < q_placed.size(); ++i) {
          const QPointF& vp = q_placed[i];
          const bool is_start = (i == 0);

          if (is_start && is_near_start) {
            painter.setBrush(QColor(0x10, 0xB9, 0x81, 60));
            painter.setPen(QPen(QColor(0x10, 0xB9, 0x81), 2.0));
            painter.drawEllipse(vp, 8.0, 8.0);
          }

          painter.setBrush(is_start ? QColor(0x10, 0xB9, 0x81) : Qt::white);
          painter.setPen(QPen(QColor(0x00, 0x78, 0xD4), 1.5));
          painter.drawEllipse(vp, 4.0, 4.0);

          painter.setFont(QFont("sans-serif", 7, QFont::Bold));
          painter.setPen(QColor(0x1E, 0x29, 0x3B));
          painter.drawText(QRectF(vp.x() + 6.0, vp.y() - 14.0, 24.0, 14.0),
                           Qt::AlignLeft | Qt::AlignVCenter, QString::number(i + 1));
        }

        // 6. Floating Status Pill Badge near Mouse Pointer
        QString tip_str;
        if (is_near_start) {
          tip_str = "Click to close polygon (or Right-click to finish)";
        } else if (q_placed.size() < 2) {
          tip_str = "Click next point";
        } else if (q_placed.size() == 2) {
          tip_str = "Click 3rd point (min 3 points)";
        } else {
          tip_str = QString("Point %1  •  Right-click to finish").arg(q_placed.size() + 1);
        }

        const QFont tip_font("sans-serif", 8, QFont::Bold);
        painter.setFont(tip_font);
        const QFontMetrics fm(tip_font);
        const int tw = fm.horizontalAdvance(tip_str) + 14;
        const int th = fm.height() + 6;
        const QRectF tip_rect(q_cur.x() + 12.0, q_cur.y() + 12.0, tw, th);

        painter.setPen(Qt::NoPen);
        painter.setBrush(is_near_start ? QColor(0x10, 0xB9, 0x81, 220) : QColor(0x1E, 0x29, 0x3B, 200));
        painter.drawRoundedRect(tip_rect, 4.0, 4.0);
        painter.setPen(Qt::white);
        painter.drawText(tip_rect, Qt::AlignCenter, tip_str);
      } else {
        // Initial point crosshair guide
        painter.setPen(QPen(QColor(0x00, 0x78, 0xD4, 200), 1.2, Qt::DashLine));
        painter.drawLine(QPointF(q_cur.x() - 10.0, q_cur.y()), QPointF(q_cur.x() + 10.0, q_cur.y()));
        painter.drawLine(QPointF(q_cur.x(), q_cur.y() - 10.0), QPointF(q_cur.x(), q_cur.y() + 10.0));

        const QString tip_str = "Click to place first vertex";
        const QFont tip_font("sans-serif", 8, QFont::Bold);
        painter.setFont(tip_font);
        const QFontMetrics fm(tip_font);
        const int tw = fm.horizontalAdvance(tip_str) + 14;
        const int th = fm.height() + 6;
        const QRectF tip_rect(q_cur.x() + 12.0, q_cur.y() + 12.0, tw, th);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x1E, 0x29, 0x3B, 200));
        painter.drawRoundedRect(tip_rect, 4.0, 4.0);
        painter.setPen(Qt::white);
        painter.drawText(tip_rect, Qt::AlignCenter, tip_str);
      }
    }

    // 8. Interactive Rulers (Top & Left) with Cursor Hairlines & Span Highlights
    if (show_rulers_) {
      const int ruler_sz = 24;

      // Backgrounds
      painter.fillRect(QRect(0, 0, ruler_sz, height()), argb_to_qcolor(diagram_style_.ruler_bg));
      painter.fillRect(QRect(0, 0, width(), ruler_sz), argb_to_qcolor(diagram_style_.ruler_bg));
      painter.fillRect(QRect(0, 0, ruler_sz, ruler_sz), argb_to_qcolor(diagram_style_.ruler_border));

      // Border lines
      painter.setPen(QPen(argb_to_qcolor(diagram_style_.ruler_border), 1.0));
      painter.drawLine(0, ruler_sz, width(), ruler_sz);
      painter.drawLine(ruler_sz, 0, ruler_sz, height());

      // Unit label
      painter.setPen(argb_to_qcolor(diagram_style_.ruler_fg));
      QFont rfont = painter.font();
      rfont.setPointSize(8);
      painter.setFont(rfont);
      painter.drawText(QRect(0, 0, ruler_sz, ruler_sz), Qt::AlignCenter, "px");

      // Span Highlight for Single Selected Node or Candidate Placement Preview
      if (selected_nodes_.size() == 1 && graph_model_) {
        if (const auto* sn = graph_model_->find_node(selected_nodes_.front())) {
          const auto stl = world_to_screen({sn->bounds.x, sn->bounds.y});
          const double sx0 = std::max(static_cast<double>(ruler_sz), stl.x);
          const double sx1 = std::min(static_cast<double>(width()), stl.x + sn->bounds.width * zoom_level_);
          const double sy0 = std::max(static_cast<double>(ruler_sz), stl.y);
          const double sy1 = std::min(static_cast<double>(height()), stl.y + sn->bounds.height * zoom_level_);

          if (sx1 > sx0) {
            painter.fillRect(QRectF(sx0, 1, sx1 - sx0, ruler_sz - 1), QColor(0x00, 0x78, 0xD4, 45));
          }
          if (sy1 > sy0) {
            painter.fillRect(QRectF(1, sy0, ruler_sz - 1, sy1 - sy0), QColor(0x00, 0x78, 0xD4, 45));
          }
        }
      } else if ((is_placement_tool_active() || is_drag_active_) && placement_preview_bounds_.is_valid() && placement_preview_bounds_.width > 0.0) {
        const auto stl = world_to_screen({placement_preview_bounds_.x, placement_preview_bounds_.y});
        const double sx0 = std::max(static_cast<double>(ruler_sz), stl.x);
        const double sx1 = std::min(static_cast<double>(width()), stl.x + placement_preview_bounds_.width * zoom_level_);
        const double sy0 = std::max(static_cast<double>(ruler_sz), stl.y);
        const double sy1 = std::min(static_cast<double>(height()), stl.y + placement_preview_bounds_.height * zoom_level_);

        if (sx1 > sx0) {
          painter.fillRect(QRectF(sx0, 1, sx1 - sx0, ruler_sz - 1), QColor(0x00, 0x78, 0xD4, 45));
        }
        if (sy1 > sy0) {
          painter.fillRect(QRectF(1, sy0, ruler_sz - 1, sy1 - sy0), QColor(0x00, 0x78, 0xD4, 45));
        }
      }

      // Step calculation based on zoom
      double tick_step = 100.0;
      if (zoom_level_ > 2.5) tick_step = 20.0;
      else if (zoom_level_ > 1.2) tick_step = 50.0;
      else if (zoom_level_ < 0.4) tick_step = 200.0;

      // Top ruler ticks
      const double world_left = screen_to_world({static_cast<double>(ruler_sz), 0.0}).x;
      const double world_right = screen_to_world({static_cast<double>(width()), 0.0}).x;
      const double start_x = std::floor(world_left / tick_step) * tick_step;

      for (double wx = start_x; wx <= world_right; wx += tick_step) {
        const double sx = world_to_screen({wx, 0.0}).x;
        if (sx < ruler_sz || sx > width()) continue;
        painter.setPen(argb_to_qcolor(diagram_style_.ruler_fg));
        painter.drawLine(QPointF(sx, ruler_sz - 8), QPointF(sx, ruler_sz));
        painter.drawText(QRectF(sx + 2, 2, 50, ruler_sz - 8), Qt::AlignLeft | Qt::AlignVCenter, QString::number(static_cast<int>(wx)));

        const double minor_step = tick_step * 0.2;
        for (int m = 1; m < 5; ++m) {
          const double smx = world_to_screen({wx + m * minor_step, 0.0}).x;
          if (smx >= ruler_sz && smx <= width()) {
            painter.drawLine(QPointF(smx, ruler_sz - 4), QPointF(smx, ruler_sz));
          }
        }
      }

      // Left ruler ticks
      const double world_top = screen_to_world({0.0, static_cast<double>(ruler_sz)}).y;
      const double world_bot = screen_to_world({0.0, static_cast<double>(height())}).y;
      const double start_y = std::floor(world_top / tick_step) * tick_step;

      for (double wy = start_y; wy <= world_bot; wy += tick_step) {
        const double sy = world_to_screen({0.0, wy}).y;
        if (sy < ruler_sz || sy > height()) continue;
        painter.setPen(argb_to_qcolor(diagram_style_.ruler_fg));
        painter.drawLine(QPointF(ruler_sz - 8, sy), QPointF(ruler_sz, sy));

        painter.save();
        painter.translate(ruler_sz - 8, sy - 2);
        painter.rotate(-90);
        painter.drawText(QRectF(0, 0, 50, 10), Qt::AlignLeft, QString::number(static_cast<int>(wy)));
        painter.restore();

        const double minor_step = tick_step * 0.2;
        for (int m = 1; m < 5; ++m) {
          const double smy = world_to_screen({0.0, wy + m * minor_step}).y;
          if (smy >= ruler_sz && smy <= height()) {
            painter.drawLine(QPointF(ruler_sz - 4, smy), QPointF(ruler_sz, smy));
          }
        }
      }

      // Cursor Hairlines
      if (last_mouse_screen_.x >= ruler_sz && last_mouse_screen_.x <= width()) {
        painter.setPen(QPen(QColor(0xE5, 0x3E, 0x3E), 1.0));
        painter.drawLine(QPointF(last_mouse_screen_.x, 0), QPointF(last_mouse_screen_.x, ruler_sz));
      }
      if (last_mouse_screen_.y >= ruler_sz && last_mouse_screen_.y <= height()) {
        painter.setPen(QPen(QColor(0xE5, 0x3E, 0x3E), 1.0));
        painter.drawLine(QPointF(0, last_mouse_screen_.y), QPointF(ruler_sz, last_mouse_screen_.y));
      }
    }

    // 9. Main Diagram Viewport Perimeter Border (Sharp framing separating diagram from surrounding windows)
    painter.setPen(QPen(argb_to_qcolor(diagram_style_.ruler_border), 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(1.0, 1.0, width() - 2.0, height() - 2.0));
  }
#endif
}

void CanvasWidget::set_diagram_style(const DiagramStyle& style) {
  diagram_style_ = style;
  update();
}

void CanvasWidget::apply_theme_to_entire_diagram(const DiagramStyle& style) {
  diagram_style_ = style;
  if (graph_model_) {
    const QString f_hex = QString("#%1").arg(style.render_style.fill_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();
    const QString s_hex = QString("#%1").arg(style.render_style.stroke_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();
    const QString t_hex = QString("#%1").arg(style.render_style.text_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();
    const QString w_hex = QString("#%1").arg(style.render_style.wire_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();

    std::vector<core::NodeId> nids;
    nids.reserve(graph_model_->node_count());
    for (const auto& [nid, _] : graph_model_->nodes()) {
      nids.push_back(nid);
    }
    for (core::NodeId nid : nids) {
      if (auto* node = graph_model_->find_node(nid)) {
        node->properties["fill_color"] = f_hex.toStdString();
        node->properties["stroke_color"] = s_hex.toStdString();
        node->properties["text_color"] = t_hex.toStdString();
      }
    }

    std::vector<core::EdgeId> eids;
    eids.reserve(graph_model_->edge_count());
    for (const auto& [eid, _] : graph_model_->edges()) {
      eids.push_back(eid);
    }
    for (core::EdgeId eid : eids) {
      if (auto* edge = graph_model_->find_edge(eid)) {
        edge->properties["stroke_color"] = w_hex.toStdString();
      }
    }
  }
  update();
}

void CanvasWidget::apply_theme_to_selection(const DiagramStyle& style) {
  if (!graph_model_) return;

  const QString f_hex = QString("#%1").arg(style.render_style.fill_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();
  const QString s_hex = QString("#%1").arg(style.render_style.stroke_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();
  const QString t_hex = QString("#%1").arg(style.render_style.text_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();
  const QString w_hex = QString("#%1").arg(style.render_style.wire_color & 0x00FFFFFF, 6, 16, QChar('0')).toUpper();

  for (core::NodeId nid : selected_nodes_) {
    if (auto* node = graph_model_->find_node(nid)) {
      node->properties["fill_color"] = f_hex.toStdString();
      node->properties["stroke_color"] = s_hex.toStdString();
      node->properties["text_color"] = t_hex.toStdString();
    }
  }
  for (const auto& [eid, edge] : graph_model_->edges()) {
    if (edge.selected) {
      if (auto* e = graph_model_->find_edge(eid)) {
        e->properties["stroke_color"] = w_hex.toStdString();
      }
    }
  }
  update();
}

void CanvasWidget::reset_selection_to_defaults() {
  if (!graph_model_) return;
  for (core::NodeId nid : selected_nodes_) {
    if (auto* node = graph_model_->find_node(nid)) {
      node->properties.erase("fill_color");
      node->properties.erase("stroke_color");
      node->properties.erase("text_color");
      node->properties.erase("stroke_width");
    }
  }
  for (const auto& [eid, edge] : graph_model_->edges()) {
    if (edge.selected) {
      if (auto* e = graph_model_->find_edge(eid)) {
        e->properties.erase("stroke_color");
        e->properties.erase("stroke_width");
      }
    }
  }
  update();
}

void CanvasWidget::reset_all_to_defaults() {
  if (!graph_model_) return;
  std::vector<core::NodeId> nids;
  nids.reserve(graph_model_->node_count());
  for (const auto& [nid, _] : graph_model_->nodes()) {
    nids.push_back(nid);
  }
  for (core::NodeId nid : nids) {
    if (auto* node = graph_model_->find_node(nid)) {
      node->properties.erase("fill_color");
      node->properties.erase("stroke_color");
      node->properties.erase("text_color");
      node->properties.erase("stroke_width");
    }
  }
  std::vector<core::EdgeId> eids;
  eids.reserve(graph_model_->edge_count());
  for (const auto& [eid, _] : graph_model_->edges()) {
    eids.push_back(eid);
  }
  for (core::EdgeId eid : eids) {
    if (auto* edge = graph_model_->find_edge(eid)) {
      edge->properties.erase("stroke_color");
      edge->properties.erase("stroke_width");
    }
  }
  update();
}

void CanvasWidget::set_page_size(double width, double height) {
  if (width <= 0.0 || height <= 0.0) return;
  page_bounds_.width = width;
  page_bounds_.height = height;
  page_orientation_ = (width >= height) ? PageOrientation::Landscape : PageOrientation::Portrait;
  page_preset_ = PagePreset::Custom;
  update();
}

void CanvasWidget::set_page_preset(PagePreset preset, PageOrientation orientation) {
  page_preset_ = preset;
  page_orientation_ = orientation;
  if (preset != PagePreset::Custom) {
    const auto dims = get_page_preset_dimensions(preset, orientation);
    page_bounds_.width = dims.x;
    page_bounds_.height = dims.y;
  }
  update();
}

void CanvasWidget::set_page_orientation(PageOrientation orientation) {
  if (page_orientation_ == orientation) return;
  page_orientation_ = orientation;
  if (page_preset_ != PagePreset::Custom) {
    const auto dims = get_page_preset_dimensions(page_preset_, orientation);
    page_bounds_.width = dims.x;
    page_bounds_.height = dims.y;
  } else {
    // Custom: swap width and height to match requested orientation
    const double w = page_bounds_.width;
    const double h = page_bounds_.height;
    if ((orientation == PageOrientation::Landscape && w < h) ||
        (orientation == PageOrientation::Portrait && w > h)) {
      page_bounds_.width = h;
      page_bounds_.height = w;
    }
  }
  update();
}

} // namespace vector::renderer
