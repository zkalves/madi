// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/gui/ThemeManager.hpp"
#include "vector/gui/IconFactory.hpp"
#include "vector/gui/SnapGridSettingsDialog.hpp"
#include "vector/gui/CommandConsoleDock.hpp"
#include "vector/gui/MainWindow.hpp"
#include "vector/renderer/CanvasWidget.hpp"
#include "vector/gui/HierarchyTreeDock.hpp"
#include "vector/core/GraphModel.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <cassert>
#include <iostream>

int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  std::cout << "===> Starting Theme System & Element Table Smoke Tests..." << std::endl;

  auto& tm = vector::gui::ThemeManager::instance();
  tm.scan_themes();

  // 1. Verify UI Themes Loaded from files
  std::cout << "Loaded UI Themes count: " << tm.ui_themes().size() << std::endl;
  assert(tm.ui_themes().size() >= 8);

  const auto* sol_dark_ui = tm.find_ui_theme("solarized-dark");
  assert(sol_dark_ui != nullptr);
  assert(sol_dark_ui->name == "Solarized Dark");
  assert(sol_dark_ui->get_color("window_bg") == "#002b36");
  (void)sol_dark_ui;

  const auto* sol_light_ui = tm.find_ui_theme("solarized-light");
  assert(sol_light_ui != nullptr);
  assert(sol_light_ui->name == "Solarized Light");
  assert(sol_light_ui->get_color("window_bg") == "#fdf6e3");
  (void)sol_light_ui;

  assert(tm.find_ui_theme("dark") != nullptr);
  assert(tm.find_ui_theme("light") != nullptr);
  assert(tm.find_ui_theme("nord") != nullptr);
  assert(tm.find_ui_theme("monokai") != nullptr);
  assert(tm.find_ui_theme("dracula") != nullptr);
  assert(tm.find_ui_theme("high-contrast-dark") != nullptr);

  // 2. Verify Diagram Themes Loaded from files
  std::cout << "Loaded Diagram Themes count: " << tm.diagram_themes().size() << std::endl;
  assert(tm.diagram_themes().size() >= 9);

  const auto* sol_dark_diag = tm.find_diagram_theme("solarized-dark");
  assert(sol_dark_diag != nullptr);
  assert(sol_dark_diag->name == "Solarized Dark");
  assert(sol_dark_diag->canvas_bg == 0xFF00212B);
  assert(sol_dark_diag->page_bg == 0xFF002B36);

  const auto* sol_light_diag = tm.find_diagram_theme("solarized-light");
  assert(sol_light_diag != nullptr);
  assert(sol_light_diag->name == "Solarized Light");
  assert(sol_light_diag->canvas_bg == 0xFFE0D8C3);
  assert(sol_light_diag->page_bg == 0xFFFDF6E3);
  (void)sol_light_diag;

  assert(tm.find_diagram_theme("blueprint") != nullptr);
  assert(tm.find_diagram_theme("nord") != nullptr);
  assert(tm.find_diagram_theme("monokai") != nullptr);
  assert(tm.find_diagram_theme("dracula") != nullptr);
  assert(tm.find_diagram_theme("high-contrast-dark") != nullptr);

  // 3. Test Activating UI Theme
  bool ui_ok = tm.set_active_ui_theme("solarized-dark");
  assert(ui_ok);
  (void)ui_ok;
  assert(app.styleSheet().contains("background-color: #002b36;"));
  assert(app.styleSheet().contains("QTreeWidget, QTreeWidget#elementTable"));

  // 4. Test Diagram Theme on CanvasWidget (All at once vs Selected items only)
  vector::core::GraphModel graph;
  vector::renderer::CanvasWidget canvas;
  canvas.set_graph_model(&graph);

  vector::core::Node n1;
  n1.name = "Node A";
  n1.bounds = {50.0, 50.0, 100.0, 60.0};
  const auto n1_id = graph.add_node(std::move(n1));

  vector::core::Node n2;
  n2.name = "Node B";
  n2.bounds = {250.0, 50.0, 100.0, 60.0};
  const auto n2_id = graph.add_node(std::move(n2));
  (void)n2_id;

  // Initially nodes have no overrides
  assert(graph.find_node(n1_id)->properties.count("fill_color") == 0);

  // Apply theme to entire diagram
  vector::renderer::CanvasWidget::DiagramStyle style;
  style.canvas_bg = sol_dark_diag->canvas_bg;
  style.page_bg = sol_dark_diag->page_bg;
  style.render_style = sol_dark_diag->to_render_style();

  canvas.apply_theme_to_entire_diagram(style);
  assert(graph.find_node(n1_id)->properties["fill_color"] == "#073642");
  assert(graph.find_node(n2_id)->properties["fill_color"] == "#073642");

  // Select only Node A and apply a different theme to selected items only (e.g. Blueprint / custom)
  canvas.select_node(n1_id, true);
  const auto* bp = tm.find_diagram_theme("blueprint");
  assert(bp != nullptr);

  vector::renderer::CanvasWidget::DiagramStyle bp_style;
  bp_style.render_style = bp->to_render_style();
  canvas.apply_theme_to_selection(bp_style);

  // Verify only Node A changed to blueprint fill (#173A60), Node B remained Solarized Dark (#073642)
  assert(graph.find_node(n1_id)->properties["fill_color"] == "#173A60");
  assert(graph.find_node(n2_id)->properties["fill_color"] == "#073642");

  // Reset selection to theme defaults
  canvas.reset_selection_to_defaults();
  assert(graph.find_node(n1_id)->properties.count("fill_color") == 0);
  assert(graph.find_node(n2_id)->properties.count("fill_color") == 1);

  // Reset all to defaults
  canvas.reset_all_to_defaults();
  assert(graph.find_node(n1_id)->properties.count("fill_color") == 0);
  assert(graph.find_node(n2_id)->properties.count("fill_color") == 0);

  // 5. Test Element Table (HierarchyTreeDock)
  vector::gui::HierarchyTreeDock hierarchy_dock;
  hierarchy_dock.set_graph_model(&graph);
  // Verify tree widget exists and has elementTable object name
  auto* tree = hierarchy_dock.findChild<QTreeWidget*>("elementTable");
  assert(tree != nullptr);
  (void)tree;
  assert(tree->headerItem()->text(0) == "Element");
  assert(tree->headerItem()->text(1) == "Kind");
  assert(tree->topLevelItemCount() == 1); // root item

  // 6. Test Snapping Engine Configurability
  std::cout << "Testing Snapping Engine configurability..." << std::endl;
  vector::grid::GridManager grid_mgr;
  assert(grid_mgr.is_snap_enabled());
  assert(grid_mgr.snap_strength() == vector::grid::SnapStrength::Normal);
  assert(grid_mgr.snap_tolerance() == 6.0);

  grid_mgr.set_snap_strength(vector::grid::SnapStrength::Weak);
  assert(grid_mgr.snap_tolerance() == 3.0);

  grid_mgr.set_snap_strength(vector::grid::SnapStrength::Strong);
  assert(grid_mgr.snap_tolerance() == 12.0);

  grid_mgr.set_snap_strength(vector::grid::SnapStrength::Off);
  assert(!grid_mgr.is_snap_enabled());
  assert(grid_mgr.snap_tolerance() == 0.0);

  grid_mgr.set_snap_tolerance(4.5);
  assert(grid_mgr.is_snap_enabled());
  assert(grid_mgr.snap_strength() == vector::grid::SnapStrength::Custom);
  assert(grid_mgr.snap_tolerance() == 4.5);

  auto snap_close = grid_mgr.snap_to_grid_if_close({21.0, 20.5});
  assert(snap_close.has_value());
  assert(snap_close->x == 20.0 && snap_close->y == 20.0);
  (void)snap_close;

  grid_mgr.set_snap_enabled(false);
  assert(!grid_mgr.snap_to_grid_if_close({21.0, 20.5}).has_value());

  // 7. Test Hierarchical Subsystem (Created Empty, Default Rectangle Symbol & Auto Port Sync)
  std::cout << "Testing Hierarchical Subsystem empty creation & auto port sync..." << std::endl;
  vector::core::HierarchyContext hierarchy_ctx(&graph);
  canvas.set_hierarchy_context(&hierarchy_ctx);

  const auto sub_id = canvas.add_subsystem({500.0, 500.0}, "ALU");
  const auto* sub_node = graph.find_node(sub_id);
  assert(sub_node != nullptr);
  assert(sub_node->shape == vector::core::ShapeType::HierarchicalBlock);
  assert(sub_node->bounds.width >= 140.0);
  assert(sub_node->bounds.height >= 100.0);

  // Drill down into new subsystem
  bool drilled = canvas.drill_down_node(sub_id);
  assert(drilled);
  (void)drilled;
  auto* sub_graph = canvas.graph_model();
  assert(sub_graph != &graph);
  (void)sub_graph;

  // Requirement: "When adding a new subsystem, it must be created empty"
  assert(sub_graph->node_count() == 0);
  assert(sub_graph->port_count() == 0);

  // Add Input Port in the hierarchical diagram
  const auto in_id = canvas.add_hierarchical_port({50.0, 100.0}, true, "data_a");
  assert(sub_graph->node_count() == 1);
  assert(sub_graph->find_node(in_id)->is_input_port());
  (void)in_id;

  // Verify automatically added to the parent symbol rectangle
  assert(sub_node->port_ids.size() == 1);
  const auto* sym_p1 = graph.find_port(sub_node->port_ids[0]);
  assert(sym_p1 != nullptr);
  assert(sym_p1->name == "data_a");
  assert(sym_p1->direction == vector::core::PortDirection::West);
  assert(sym_p1->local_anchor.x == 0.0);
  (void)sym_p1;

  // Add Output Port in the hierarchical diagram
  const auto out_id = canvas.add_hierarchical_port({350.0, 100.0}, false, "result");
  assert(sub_graph->node_count() == 2);
  assert(sub_graph->find_node(out_id)->is_output_port());
  (void)out_id;

  // Verify automatically added to the parent symbol rectangle
  assert(sub_node->port_ids.size() == 2);
  const auto* sym_p2 = graph.find_port(sub_node->port_ids[1]);
  assert(sym_p2 != nullptr);
  assert(sym_p2->name == "result");
  assert(sym_p2->direction == vector::core::PortDirection::East);
  assert(sym_p2->local_anchor.x == 1.0);
  (void)sym_p2;

  // Pop back up to parent scope
  bool popped = canvas.pop_hierarchy_scope();
  assert(popped);
  (void)popped;
  assert(canvas.graph_model() == &graph);
  assert(sub_node->port_ids.size() == 2);

  // 9. Test Tool Icon Generation & Mouse Hover Tooltip Requirements
  std::cout << "===> Testing Tool Icons and Tooltip Specs..." << std::endl;
  const QColor test_fg(205, 214, 244);
  const std::vector<vector::gui::IconFactory::IconType> all_icon_types = {
    vector::gui::IconFactory::IconType::Select,
    vector::gui::IconFactory::IconType::Rectangle,
    vector::gui::IconFactory::IconType::RoundedRect,
    vector::gui::IconFactory::IconType::Ellipse,
    vector::gui::IconFactory::IconType::PolygonHex,
    vector::gui::IconFactory::IconType::Triangle,
    vector::gui::IconFactory::IconType::Diamond,
    vector::gui::IconFactory::IconType::Octagon,
    vector::gui::IconFactory::IconType::SubsystemBlock,
    vector::gui::IconFactory::IconType::SubsystemFile,
    vector::gui::IconFactory::IconType::InputPort,
    vector::gui::IconFactory::IconType::OutputPort,
    vector::gui::IconFactory::IconType::Wire,
    vector::gui::IconFactory::IconType::MergeFigures,
    vector::gui::IconFactory::IconType::SplitFigure,
    vector::gui::IconFactory::IconType::Undo,
    vector::gui::IconFactory::IconType::Redo,
    vector::gui::IconFactory::IconType::Delete,
    vector::gui::IconFactory::IconType::ZoomIn,
    vector::gui::IconFactory::IconType::ZoomOut,
    vector::gui::IconFactory::IconType::FitContent,
    vector::gui::IconFactory::IconType::ShowGrid,
    vector::gui::IconFactory::IconType::Snapping,
    vector::gui::IconFactory::IconType::DrillDown,
    vector::gui::IconFactory::IconType::PopScope
  };

  for (auto itype : all_icon_types) {
    QPixmap pm = vector::gui::IconFactory::render_pixmap(itype, test_fg, 48);
    assert(!pm.isNull());
    assert(pm.width() == 48);
    assert(pm.height() == 48);

    QIcon ic = vector::gui::IconFactory::create_icon(itype, test_fg);
    assert(!ic.isNull());
    assert(!ic.pixmap(24, 24).isNull());
  }

  // 10. Test Multi-Part Alignment for Boxes of Slightly Different Sizes
  std::cout << "===> Testing Multi-Part Alignment for Slightly Different Sized Boxes..." << std::endl;
  vector::grid::GridManager gm;
  gm.set_snap_strength(vector::grid::SnapStrength::Normal); // 6 px tolerance
  canvas.set_grid_manager(&gm);

  // Box A (reference): width 100, height 100 at (100, 100)
  // Left = 100, Center = 150, Right = 200
  vector::core::Node box_a;
  box_a.name = "Box A";
  box_a.bounds = {100.0, 100.0, 100.0, 100.0};
  const auto box_a_id = graph.add_node(std::move(box_a));
  (void)box_a_id;

  // Box B (dragged): width 104, height 104 (different size!)
  vector::core::Node box_b;
  box_b.name = "Box B";
  box_b.bounds = {250.0, 100.0, 104.0, 104.0};
  const auto box_b_id = graph.add_node(std::move(box_b));

  // Case 1: Proposed near left border (x=100.4) -> snaps left border
  vector::core::Rect2d prop_left = {100.4, 100.0, 104.0, 104.0};
  vector::core::Vec2d off_left = canvas.compute_smart_alignment(box_b_id, prop_left);
  assert(std::abs(off_left.x - (-0.4)) < 1e-3);
  assert(std::abs((prop_left.x + off_left.x) - 100.0) < 1e-3);
  (void)off_left;

  // Case 2: User moves towards center alignment: Box B center approaching 150 (x = 150 - 52 = 98)
  // At x=97.9, center is 149.9 (dist 0.1 to center, dist 2.1 to left, dist 1.9 to right)
  vector::core::Rect2d prop_center = {97.9, 100.0, 104.0, 104.0};
  vector::core::Vec2d off_center = canvas.compute_smart_alignment(box_b_id, prop_center);
  assert(std::abs((prop_center.center().x + off_center.x) - 150.0) < 1e-3);
  (void)off_center;

  // Case 3: User moves towards right border alignment: Box B right approaching 200 (x = 200 - 104 = 96)
  // At x=96.1, right is 200.1 (dist 0.1 to right, dist 1.9 to center, dist 3.9 to left)
  vector::core::Rect2d prop_right = {96.1, 100.0, 104.0, 104.0};
  vector::core::Vec2d off_right = canvas.compute_smart_alignment(box_b_id, prop_right);
  assert(std::abs((prop_right.right() + off_right.x) - 200.0) < 1e-3);
  (void)off_right;

  // Case 4: User moves away (x=80.0, all distances > 6px) -> zero snap, moves freely!
  vector::core::Rect2d prop_away = {80.0, 100.0, 104.0, 104.0};
  vector::core::Vec2d off_away = canvas.compute_smart_alignment(box_b_id, prop_away);
  assert(std::abs(off_away.x) < 1e-4 && std::abs(off_away.y) < 1e-4);
  (void)off_away;

  // Case 5: Side-by-side abutting (Box B left at 200.8, Box A right at 200.0)
  vector::core::Rect2d prop_abut = {200.8, 100.0, 104.0, 104.0};
  vector::core::Vec2d off_abut = canvas.compute_smart_alignment(box_b_id, prop_abut);
  assert(std::abs((prop_abut.x + off_abut.x) - 200.0) < 1e-3);
  (void)off_abut;

  // 11. Test Resize Alignment Markers & Edge Snapping
  std::cout << "===> Testing Resize Alignment Markers & Edge Snapping..." << std::endl;
  // Box A is at {100.0, 100.0, 100.0, 100.0} (Left=100, Center=150, Right=200, Top=100, Center=150, Bottom=200)
  // Box B is at {250.0, 100.0, 104.0, 104.0}
  // Case A: Resizing Box B left edge towards Box A right edge (200.0)
  // Proposed bounds: left at 200.5, right fixed at 354.0 (width = 153.5)
  vector::core::Rect2d resize_left = {200.5, 100.0, 153.5, 104.0};
  canvas.compute_resize_alignment(box_b_id, vector::renderer::HandleType::MiddleLeft, resize_left);
  // Verify snapped to 200.0 and width adjusted to 154.0
  assert(std::abs(resize_left.x - 200.0) < 1e-3);
  assert(std::abs(resize_left.width - 154.0) < 1e-3);
  assert(canvas.active_smart_guides().size() == 1);
  assert(canvas.active_smart_guides()[0].is_vertical);
  assert(std::abs(canvas.active_smart_guides()[0].p1.x - 200.0) < 1e-3);

  // Case B: Resizing using BottomRight corner towards Box A right (200.0) and bottom (200.0)
  vector::core::Node box_c;
  box_c.name = "Box C";
  box_c.bounds = {50.0, 50.0, 50.0, 50.0};
  const auto box_c_id = graph.add_node(std::move(box_c));

  vector::core::Rect2d resize_corner = {50.0, 50.0, 149.2, 151.3}; // right=199.2 (close to 200), bot=201.3 (close to 200)
  canvas.compute_resize_alignment(box_c_id, vector::renderer::HandleType::BottomRight, resize_corner);
  assert(std::abs(resize_corner.right() - 200.0) < 1e-3);
  assert(std::abs(resize_corner.bottom() - 200.0) < 1e-3);
  assert(canvas.active_smart_guides().size() == 2);
  bool found_v = false, found_h = false;
  for (const auto& g : canvas.active_smart_guides()) {
    if (g.is_vertical && std::abs(g.p1.x - 200.0) < 1e-3) found_v = true;
    if (!g.is_vertical && std::abs(g.p1.y - 200.0) < 1e-3) found_h = true;
  }
  assert(found_v && found_h);
  (void)found_v; (void)found_h;

  // Case C: Resizing far away (> 6px snap distance) -> no guide lines, bounds unchanged
  vector::core::Rect2d resize_away = {50.0, 50.0, 115.0, 115.0}; // right=165, bot=165 (far from 100, 150, 200)
  canvas.compute_resize_alignment(box_c_id, vector::renderer::HandleType::BottomRight, resize_away);
  assert(canvas.active_smart_guides().empty());
  assert(std::abs(resize_away.width - 115.0) < 1e-3);
  assert(std::abs(resize_away.height - 115.0) < 1e-3);

  // 12. Test Advanced Snap & Grid Settings (Independent Spacing, Snap Types, Dialog & Commands)
  std::cout << "===> Testing Advanced Snap & Grid Settings (Independent Spacing, Snap Types, Dialog & Commands)..." << std::endl;
  vector::grid::GridManager adv_grid;
  // Test Independent Snap Spacing
  adv_grid.set_snap_spacing(10.0, 25.0);
  assert(adv_grid.snap_spacing_x() == 10.0);
  assert(adv_grid.snap_spacing_y() == 25.0);

  auto snap_indep = adv_grid.snap_to_grid({11.2, 27.8});
  assert(std::abs(snap_indep.x - 10.0) < 1e-4);
  assert(std::abs(snap_indep.y - 25.0) < 1e-4);
  (void)snap_indep;

  // Test Isometric Snap Type
  adv_grid.set_snap_type(vector::grid::SnapType::Isometric);
  assert(adv_grid.snap_type() == vector::grid::SnapType::Isometric);
  auto snap_iso = adv_grid.snap_to_grid({10.0, 10.0});
  (void)snap_iso;

  // Test Polar Snap Type
  adv_grid.set_snap_type(vector::grid::SnapType::Polar);
  adv_grid.set_polar_angle_deg(45.0);
  assert(adv_grid.snap_type() == vector::grid::SnapType::Polar);
  assert(adv_grid.polar_angle_deg() == 45.0);
  auto snap_polar = adv_grid.snap_to_grid({10.0, 1.0}); // angle close to 0 rad
  assert(std::abs(snap_polar.y) < 1e-4); // snapped to horizontal 0-deg axis!
  (void)snap_polar;

  // Test Grid Spacing Mode (Fixed vs Variable)
  adv_grid.set_grid_spacing_mode(vector::grid::GridSpacingMode::Fixed);
  assert(adv_grid.grid_spacing_mode() == vector::grid::GridSpacingMode::Fixed);
  adv_grid.set_grid_spacing_mode(vector::grid::GridSpacingMode::Variable);
  assert(adv_grid.grid_spacing_mode() == vector::grid::GridSpacingMode::Variable);

  // Test SnapGridSettingsDialog
  vector::gui::SnapGridSettingsDialog snap_dlg;
  snap_dlg.load_from(adv_grid, true);
  assert(snap_dlg.chk_enable_snap_->isChecked());
  bool smart_guides_applied = false;
  adv_grid.set_snap_spacing(15.0, 30.0);
  snap_dlg.apply_to(adv_grid, smart_guides_applied);
  assert(adv_grid.is_snap_enabled());

  // Test CommandConsoleDock snap, snapunit, and dsettings commands
  vector::gui::CommandConsoleDock console;
  console.set_canvas(&canvas);
  console.set_grid_manager(&adv_grid);
  int settings_tab_opened = -1;
  console.set_settings_callback([&settings_tab_opened](int tab) {
    settings_tab_opened = tab;
  });

  // Test Command: snap 20
  auto* input_line = console.findChild<QLineEdit*>();
  assert(input_line != nullptr);
  input_line->setText("snap 20");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(adv_grid.snap_spacing_x() == 20.0);
  assert(adv_grid.snap_spacing_y() == 20.0);

  // Test Command: snapunit 12 24
  input_line->setText("snapunit 12 24");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(adv_grid.snap_spacing_x() == 12.0);
  assert(adv_grid.snap_spacing_y() == 24.0);

  // Test Command: dsettings
  input_line->setText("dsettings");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(settings_tab_opened == 2);
  (void)settings_tab_opened;

  // 13. Test Grid Snapping on Moving and Resizing
  std::cout << "===> Testing Grid Snapping on Moving and Resizing..." << std::endl;
  vector::grid::GridManager grid_snap_mgr;
  grid_snap_mgr.set_snap_flags(vector::grid::SnapFlags::Grid); // Only Grid snap enabled
  grid_snap_mgr.set_snap_spacing(20.0, 20.0);
  grid_snap_mgr.set_snap_tolerance(6.0);
  canvas.set_grid_manager(&grid_snap_mgr);

  // Proposed node bounds near grid line (x=101.5, y=98.8 -> grid line is 100.0, 100.0)
  vector::core::Rect2d move_near_grid = {101.5, 98.8, 50.0, 50.0};
  vector::core::Vec2d grid_off = canvas.compute_smart_alignment(box_c_id, move_near_grid);
  assert(std::abs((move_near_grid.x + grid_off.x) - 100.0) < 1e-4);
  assert(std::abs((move_near_grid.y + grid_off.y) - 100.0) < 1e-4);
  (void)grid_off;

  // Resize edge near grid line (right edge is at 158.5 -> nearest grid line is 160.0)
  vector::core::Rect2d resize_near_grid = {100.0, 100.0, 58.5, 50.0};
  canvas.compute_resize_alignment(box_c_id, vector::renderer::HandleType::MiddleRight, resize_near_grid);
  assert(std::abs(resize_near_grid.right() - 160.0) < 1e-4);

  // 14. Test Both Sides Alignment Lines when Dimensions Match Other Object
  std::cout << "===> Testing Both Sides Alignment Lines on Match..." << std::endl;
  grid_snap_mgr.set_snap_flags(vector::grid::SnapFlags::All);
  // Box A is at {100.0, 100.0, 100.0, 100.0} (Left=100, Right=200)
  // Box with left at 100.0, user drags right handle towards 200.0
  vector::core::Rect2d resize_both_sides = {100.0, 250.0, 99.2, 50.0}; // right=199.2 (near 200.0)
  canvas.compute_resize_alignment(box_c_id, vector::renderer::HandleType::MiddleRight, resize_both_sides);
  // Verify right edge snapped to 200.0 and left edge remains aligned at 100.0
  assert(std::abs(resize_both_sides.right() - 200.0) < 1e-3);
  assert(std::abs(resize_both_sides.x - 100.0) < 1e-3);
  // Both sides (Left at 100.0 and Right at 200.0) must show alignment lines!
  assert(canvas.active_smart_guides().size() >= 2);
  bool found_left_guide = false, found_right_guide = false;
  for (const auto& g : canvas.active_smart_guides()) {
    if (g.is_vertical && std::abs(g.p1.x - 100.0) < 1e-3) found_left_guide = true;
    if (g.is_vertical && std::abs(g.p1.x - 200.0) < 1e-3) found_right_guide = true;
  }
  assert(found_left_guide && found_right_guide);
  (void)found_left_guide; (void)found_right_guide;

  // 15. Test Page Size Selection, Orientation, Margins & Sheet Display
  std::cout << "===> Testing Page Size Selection, Presets & Margins..." << std::endl;
  canvas.set_page_preset(vector::renderer::PagePreset::A4, vector::renderer::PageOrientation::Landscape);
  assert(canvas.page_preset() == vector::renderer::PagePreset::A4);
  assert(canvas.page_orientation() == vector::renderer::PageOrientation::Landscape);
  assert(std::abs(canvas.page_bounds().width - 1169.0) < 1e-3);
  assert(std::abs(canvas.page_bounds().height - 827.0) < 1e-3);

  canvas.set_page_orientation(vector::renderer::PageOrientation::Portrait);
  assert(canvas.page_orientation() == vector::renderer::PageOrientation::Portrait);
  assert(std::abs(canvas.page_bounds().width - 827.0) < 1e-3);
  assert(std::abs(canvas.page_bounds().height - 1169.0) < 1e-3);

  canvas.set_page_size(1500.0, 950.0);
  assert(canvas.page_preset() == vector::renderer::PagePreset::Custom);
  assert(std::abs(canvas.page_bounds().width - 1500.0) < 1e-3);
  assert(std::abs(canvas.page_bounds().height - 950.0) < 1e-3);
  assert(canvas.page_orientation() == vector::renderer::PageOrientation::Landscape);

  canvas.set_page_margin(35.0);
  assert(std::abs(canvas.page_margin() - 35.0) < 1e-3);

  canvas.set_show_page_sheet(false);
  assert(!canvas.show_page_sheet());
  canvas.set_show_page_sheet(true);
  assert(canvas.show_page_sheet());

  // Test SnapGridSettingsDialog with Canvas Page Setup
  snap_dlg.load_from(adv_grid, true, &canvas);
  assert(std::abs(snap_dlg.spin_page_width_->value() - 1500.0) < 1e-3);
  assert(std::abs(snap_dlg.spin_page_height_->value() - 950.0) < 1e-3);
  assert(std::abs(snap_dlg.spin_page_margin_->value() - 35.0) < 1e-3);
  assert(snap_dlg.chk_show_page_sheet_->isChecked());

  int a3_idx = snap_dlg.combo_page_preset_->findData(static_cast<int>(vector::renderer::PagePreset::A3));
  assert(a3_idx >= 0);
  snap_dlg.combo_page_preset_->setCurrentIndex(a3_idx);
  snap_dlg.rb_orient_landscape_->setChecked(true);
  snap_dlg.apply_to(adv_grid, smart_guides_applied, &canvas);
  assert(canvas.page_preset() == vector::renderer::PagePreset::A3);
  assert(canvas.page_orientation() == vector::renderer::PageOrientation::Landscape);
  assert(std::abs(canvas.page_bounds().width - 1654.0) < 1e-3);
  assert(std::abs(canvas.page_bounds().height - 1169.0) < 1e-3);

  // Test Console Commands for Page Size and Margin
  input_line->setText("pagesize letter portrait");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(canvas.page_preset() == vector::renderer::PagePreset::Letter);
  assert(canvas.page_orientation() == vector::renderer::PageOrientation::Portrait);
  assert(std::abs(canvas.page_bounds().width - 850.0) < 1e-3);
  assert(std::abs(canvas.page_bounds().height - 1100.0) < 1e-3);

  input_line->setText("pagesize 1800 1200");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(canvas.page_preset() == vector::renderer::PagePreset::Custom);
  assert(std::abs(canvas.page_bounds().width - 1800.0) < 1e-3);
  assert(std::abs(canvas.page_bounds().height - 1200.0) < 1e-3);

  input_line->setText("pagesheet off");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(!canvas.show_page_sheet());

  input_line->setText("pagesheet on");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(canvas.show_page_sheet());

  input_line->setText("pagemargin 42.5");
  QMetaObject::invokeMethod(input_line, "returnPressed");
  assert(std::abs(canvas.page_margin() - 42.5) < 1e-3);

  // 16. Test Zoom-Out Grid Rendering & Dynamic Decimation
  std::cout << "===> Testing Zoom-Out Grid Rendering & Decimation..." << std::endl;
  canvas.resize(800, 600);
  canvas.set_zoom_level(0.01); // Extreme zoom out (1%)
  assert(canvas.zoom_level() == 0.01);
  // Trigger paint event to execute CanvasRenderer::draw_grid under extreme zoom out
  canvas.render_frame_for_test();

  canvas.set_zoom_level(0.1); // 10% zoom out
  canvas.render_frame_for_test();

  canvas.set_zoom_level(2.0); // 200% zoom in
  canvas.render_frame_for_test();

  // 17. Test Block Selection Color Preservation (Selected block background must NOT turn white)
  std::cout << "===> Testing Block Selection Background Color Preservation..." << std::endl;
  vector::renderer::CanvasRenderer test_renderer;
  test_renderer.initialize_software(400, 300);
  vector::renderer::RenderContext test_ctx;
  test_ctx.viewport_width = 400;
  test_ctx.viewport_height = 300;
  test_ctx.world_to_screen = vector::core::Transform2d{1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
  test_ctx.screen_to_world = test_ctx.world_to_screen;
  test_ctx.visible_frustum = {0.0, 0.0, 400.0, 300.0};
  test_ctx.zoom = 1.0;

  // Render Solarized Dark style
  vector::renderer::RenderStyle sol_style = sol_dark_diag->to_render_style();
  assert(sol_style.fill_color == sol_dark_diag->node_fill); // Solarized Dark node fill

  // Case A: Block selected with theme default fill color (no property override)
  vector::core::Node sel_block_theme;
  sel_block_theme.name = "Theme Block";
  sel_block_theme.bounds = {100.0, 100.0, 100.0, 60.0};
  sel_block_theme.selected = true; // Selected!

  test_renderer.begin_frame(test_ctx);
  test_renderer.draw_node(sel_block_theme, test_ctx, sol_style);
  test_renderer.end_frame();

  // Center is at (150, 130). Verify fill color matches theme node_fill and is NOT white (0xFFEBF8FF)
  const auto* buf = reinterpret_cast<const uint32_t*>(test_renderer.software_buffer());
  const uint32_t center_pixel_theme = buf[130 * 400 + 150];
  assert(center_pixel_theme == sol_dark_diag->node_fill);
  assert(center_pixel_theme != 0xFFEBF8FF);
  (void)center_pixel_theme;

  // Case B: Block selected with custom configured fill color (property override)
  vector::core::Node sel_block_custom;
  sel_block_custom.name = "Custom Color Block";
  sel_block_custom.bounds = {100.0, 100.0, 100.0, 60.0};
  sel_block_custom.properties["fill_color"] = "#E06C75";
  sel_block_custom.selected = true; // Selected!

  test_renderer.begin_frame(test_ctx);
  test_renderer.draw_node(sel_block_custom, test_ctx, sol_style);
  test_renderer.end_frame();

  const uint32_t center_pixel_custom = buf[130 * 400 + 150];
  assert(center_pixel_custom == 0xFFE06C75);
  assert(center_pixel_custom != 0xFFEBF8FF);
  (void)center_pixel_custom;

  // Case C: HierarchicalBlock selected
  vector::core::Node sel_hblock;
  sel_hblock.name = "Subsystem 1";
  sel_hblock.shape = vector::core::ShapeType::HierarchicalBlock;
  sel_hblock.bounds = {100.0, 100.0, 100.0, 60.0};
  sel_hblock.selected = true;

  test_renderer.begin_frame(test_ctx);
  test_renderer.draw_node(sel_hblock, test_ctx, sol_style);
  test_renderer.end_frame();

  // Hierarchical block body at y = 140 (below the 22px header)
  const uint32_t hblock_body_pixel = buf[140 * 400 + 150];
  assert(hblock_body_pixel == sol_dark_diag->node_fill);
  assert(hblock_body_pixel != 0xFFEBF8FF);
  (void)hblock_body_pixel;

  // 18. Test Window Positions, Layout, Themes, Colours & Settings Persistence ("if user accepts")
  std::cout << "===> Testing Session Persistence (Window Positions, Layout, Colors & Themes)..." << std::endl;
  QSettings test_settings("VECTOR_Test", "SessionPersistenceTest");
  test_settings.clear();

  vector::gui::MainWindow win_session;

  // A: Initially, unprompted and not accepted
  assert(!win_session.is_remember_settings_prompted(&test_settings));
  assert(!win_session.is_remember_settings_accepted(&test_settings));

  // B: User declines
  win_session.set_remember_settings_accepted(false, &test_settings);
  assert(win_session.is_remember_settings_prompted(&test_settings));
  assert(!win_session.is_remember_settings_accepted(&test_settings));

  // C: User accepts
  win_session.set_remember_settings_accepted(true, &test_settings);
  assert(win_session.is_remember_settings_prompted(&test_settings));
  assert(win_session.is_remember_settings_accepted(&test_settings));

  // D: Configure distinct window layout, themes, page size, snapping
  win_session.resize(1450, 920);
  tm.set_active_ui_theme("solarized-light");
  tm.set_active_diagram_theme("solarized-light");
  win_session.canvas()->set_page_preset(vector::renderer::PagePreset::A3, vector::renderer::PageOrientation::Landscape);
  win_session.canvas()->set_page_margin(31.5);
  win_session.canvas()->set_show_rulers(false);
  win_session.grid_manager()->set_snap_strength(vector::grid::SnapStrength::Strong);
  win_session.grid_manager()->set_snap_type(vector::grid::SnapType::Isometric);

  // Save settings
  win_session.save_settings(&test_settings);
  assert(test_settings.contains("Window/geometry"));
  assert(test_settings.contains("Window/state"));
  assert(test_settings.value("Theme/ui_theme").toString() == "solarized-light");
  assert(test_settings.value("Theme/diagram_theme").toString() == "solarized-light");
  assert(test_settings.value("Page/preset").toInt() == static_cast<int>(vector::renderer::PagePreset::A3));
  assert(std::abs(test_settings.value("Page/margin").toDouble() - 31.5) < 1e-3);
  assert(test_settings.value("Canvas/show_rulers").toBool() == false);
  assert(test_settings.value("SnapGrid/snap_strength").toInt() == static_cast<int>(vector::grid::SnapStrength::Strong));
  assert(test_settings.value("SnapGrid/snap_type").toInt() == static_cast<int>(vector::grid::SnapType::Isometric));

  // E: Restore into a new MainWindow instance
  vector::gui::MainWindow win_restored;
  win_restored.restore_settings(&test_settings);

  assert(tm.current_ui_theme().id == "solarized-light");
  assert(tm.current_diagram_theme().id == "solarized-light");
  assert(win_restored.canvas()->page_preset() == vector::renderer::PagePreset::A3);
  assert(win_restored.canvas()->page_orientation() == vector::renderer::PageOrientation::Landscape);
  assert(std::abs(win_restored.canvas()->page_margin() - 31.5) < 1e-3);
  assert(!win_restored.canvas()->show_rulers());
  assert(win_restored.grid_manager()->snap_strength() == vector::grid::SnapStrength::Strong);
  assert(win_restored.grid_manager()->snap_type() == vector::grid::SnapType::Isometric);

  // F: Test Reset Layout
  win_restored.reset_layout();
  assert(win_restored.width() == 1360 && win_restored.height() == 860);

  // 19. Test Console Persistence & Reset Commands
  std::cout << "===> Testing Console Remember & ResetLayout Commands..." << std::endl;
  input_line->setText("remember");
  QMetaObject::invokeMethod(input_line, "returnPressed");

  input_line->setText("remember on");
  QMetaObject::invokeMethod(input_line, "returnPressed");

  input_line->setText("remember off");
  QMetaObject::invokeMethod(input_line, "returnPressed");

  input_line->setText("resetlayout");
  QMetaObject::invokeMethod(input_line, "returnPressed");

  test_settings.clear();

  // 20. Test Clear Division between Internal Windows and Main Diagram
  std::cout << "===> Testing Clear Window Division (Separators, Dock Borders & Canvas Viewport Frame)..." << std::endl;
  const QString ss = app.styleSheet();
  assert(ss.contains("QMainWindow::separator"));
  assert(ss.contains("width: 6px;"));
  assert(ss.contains("height: 6px;"));
  assert(ss.contains("QDockWidget"));
  assert(ss.contains("QDockWidget > QWidget"));
  assert(ss.contains("#mainDiagramCanvas, QWidget#centralWidget"));

  // 21. Test Exact 2D Boolean Logic Operations (OR/Union, AND/Intersection, SUBTRACT/Difference, XOR)
  std::cout << "===> Testing 2D Boolean Logic Operations (Union/OR, Intersection/AND, Subtract, XOR)..." << std::endl;
  {
    vector::core::GraphModel bool_graph;
    vector::renderer::CanvasWidget bool_canvas;
    bool_canvas.set_graph_model(&bool_graph);

    // Box 1: (0, 0, 100, 100)
    vector::core::Node b1;
    b1.name = "Box 1";
    b1.bounds = {0.0, 0.0, 100.0, 100.0};
    const auto id1 = bool_graph.add_node(std::move(b1));

    // Box 2: (50, 50, 100, 100) - Overlapping
    vector::core::Node b2;
    b2.name = "Box 2";
    b2.bounds = {50.0, 50.0, 100.0, 100.0};
    const auto id2 = bool_graph.add_node(std::move(b2));

    // Case A: Boolean OR / Union
    bool_canvas.select_node(id1, true);
    bool_canvas.select_node(id2, false);
    assert(bool_canvas.can_merge_selected());

    bool ok_union = bool_canvas.boolean_combine_selected(vector::core::BooleanOp::Union);
    assert(ok_union);
    (void)ok_union;
    assert(bool_graph.node_count() == 1);
    const auto merged_id = bool_canvas.selected_nodes().front();
    const auto* merged = bool_graph.find_node(merged_id);
    assert(merged != nullptr);
    assert(merged->bounds.x == 0.0 && merged->bounds.y == 0.0);
    assert(merged->bounds.width == 150.0 && merged->bounds.height == 150.0);
    assert(merged->shape == vector::core::ShapeType::CustomPolygon);
    assert(merged->properties.at("boolean_op") == "union");

    // The union of two overlapping rectangles has 8 vertices (L-shaped perimeter).
    // A convex hull would have at most 6 outer vertices and miss the reflex corner (100, 50) and (50, 100).
    const auto world_poly = merged->get_world_polygon_vertices();
    assert(world_poly.size() == 8);

    // Check presence of concave/inner notch vertices
    bool has_inner_notch_1 = false;
    bool has_inner_notch_2 = false;
    for (const auto& v : world_poly) {
      if (std::abs(v.x - 100.0) < 1.0 && std::abs(v.y - 50.0) < 1.0) has_inner_notch_1 = true;
      if (std::abs(v.x - 50.0) < 1.0 && std::abs(v.y - 100.0) < 1.0) has_inner_notch_2 = true;
    }
    assert(has_inner_notch_1 && has_inner_notch_2);
    (void)has_inner_notch_1; (void)has_inner_notch_2;

    // Case B: Split the Union composite figure
    bool ok_split = bool_canvas.split_selected_figure();
    assert(ok_split);
    (void)ok_split;
    assert(bool_graph.node_count() == 2);
    auto restored_sel = bool_canvas.selected_nodes();
    assert(restored_sel.size() == 2);
    const auto* r1 = bool_graph.find_node(restored_sel[0]);
    const auto* r2 = bool_graph.find_node(restored_sel[1]);
    (void)r1; (void)r2;
    assert(r1 && r2);
    assert(r1->name == "Box 1" && r1->bounds.width == 100.0);
    assert(r2->name == "Box 2" && r2->bounds.width == 100.0);

    // Case C: Boolean AND / Intersection
    bool_canvas.select_node(restored_sel[0], true);
    bool_canvas.select_node(restored_sel[1], false);
    bool ok_and = bool_canvas.boolean_combine_selected(vector::core::BooleanOp::Intersection);
    assert(ok_and);
    (void)ok_and;
    assert(bool_graph.node_count() == 1);
    const auto and_id = bool_canvas.selected_nodes().front();
    const auto* and_node = bool_graph.find_node(and_id);
    (void)and_node;
    assert(and_node != nullptr);
    assert(std::abs(and_node->bounds.x - 50.0) < 1e-3);
    assert(std::abs(and_node->bounds.y - 50.0) < 1e-3);
    assert(std::abs(and_node->bounds.width - 50.0) < 1e-3);
    assert(std::abs(and_node->bounds.height - 50.0) < 1e-3);
    assert(and_node->properties.at("boolean_op") == "intersection");

    // Case D: Boolean Difference (Subtract)
    bool_graph.clear();
    vector::core::Node diff_a;
    diff_a.name = "Base Rect";
    diff_a.bounds = {0.0, 0.0, 100.0, 100.0};
    const auto da_id = bool_graph.add_node(std::move(diff_a));

    vector::core::Node diff_b;
    diff_b.name = "Cutter Rect";
    diff_b.bounds = {50.0, 50.0, 100.0, 100.0};
    const auto db_id = bool_graph.add_node(std::move(diff_b));

    bool_canvas.select_node(da_id, true);
    bool_canvas.select_node(db_id, false);
    bool ok_sub = bool_canvas.boolean_combine_selected(vector::core::BooleanOp::Difference);
    assert(ok_sub);
    (void)ok_sub;
    assert(bool_graph.node_count() == 1);
    const auto sub_id = bool_canvas.selected_nodes().front();
    const auto* sub_node = bool_graph.find_node(sub_id);
    (void)sub_node;
    assert(sub_node != nullptr);
    assert(sub_node->properties.at("boolean_op") == "difference");
    assert(sub_node->bounds.x == 0.0 && sub_node->bounds.y == 0.0);
    assert(sub_node->bounds.width == 100.0 && sub_node->bounds.height == 100.0);
    // 6-vertex L-shaped cutout
    assert(sub_node->get_world_polygon_vertices().size() == 6);

    // Case E: Boolean XOR (Symmetric Difference)
    bool_graph.clear();
    vector::core::Node xor_a;
    xor_a.name = "XOR A";
    xor_a.bounds = {0.0, 0.0, 100.0, 100.0};
    const auto xa_id = bool_graph.add_node(std::move(xor_a));

    vector::core::Node xor_b;
    xor_b.name = "XOR B";
    xor_b.bounds = {50.0, 50.0, 100.0, 100.0};
    const auto xb_id = bool_graph.add_node(std::move(xor_b));

    bool_canvas.select_node(xa_id, true);
    bool_canvas.select_node(xb_id, false);
    bool ok_xor = bool_canvas.boolean_combine_selected(vector::core::BooleanOp::Xor);
    assert(ok_xor);
    (void)ok_xor;
    assert(bool_graph.node_count() == 1);
    const auto xor_id = bool_canvas.selected_nodes().front();
    const auto* xor_node = bool_graph.find_node(xor_id);
    (void)xor_node;
    assert(xor_node != nullptr);
    assert(xor_node->properties.at("boolean_op") == "xor");
    assert(xor_node->subpath_polygons.size() == 2);

    // Case F: Disjoint shapes Intersection yields false without modifying diagram
    bool_graph.clear();
    vector::core::Node dis_a;
    dis_a.name = "Disjoint A";
    dis_a.bounds = {0.0, 0.0, 40.0, 40.0};
    const auto disa_id = bool_graph.add_node(std::move(dis_a));

    vector::core::Node dis_b;
    dis_b.name = "Disjoint B";
    dis_b.bounds = {200.0, 200.0, 40.0, 40.0};
    const auto disb_id = bool_graph.add_node(std::move(dis_b));

    bool_canvas.select_node(disa_id, true);
    bool_canvas.select_node(disb_id, false);
    bool ok_dis_and = bool_canvas.boolean_combine_selected(vector::core::BooleanOp::Intersection);
    assert(!ok_dis_and);
    (void)ok_dis_and;
    assert(bool_graph.node_count() == 2); // Unmodified!
  }

  // 22. Test Console Boolean Commands in MainWindow
  std::cout << "===> Testing Console Boolean Commands (bool union, intersect, subtract, xor)..." << std::endl;
  {
    vector::gui::MainWindow bool_win;
    auto* bcanvas = bool_win.canvas();
    auto* bgraph = bcanvas->graph_model();
    bgraph->clear();

    vector::core::Node n1;
    n1.name = "A";
    n1.bounds = {100.0, 100.0, 80.0, 80.0};
    const auto nid1 = bgraph->add_node(std::move(n1));

    vector::core::Node n2;
    n2.name = "B";
    n2.bounds = {140.0, 140.0, 80.0, 80.0};
    const auto nid2 = bgraph->add_node(std::move(n2));

    bcanvas->select_node(nid1, true);
    bcanvas->select_node(nid2, false);

    auto* c_input = bool_win.findChild<QLineEdit*>("commandInputLine");
    assert(c_input != nullptr);

    c_input->setText("bool union");
    QMetaObject::invokeMethod(c_input, "returnPressed");
    assert(bgraph->node_count() == 1);
    const auto u_id = bcanvas->selected_nodes().front();
    const auto* u_node = bgraph->find_node(u_id);
    (void)u_node;
    assert(u_node && u_node->properties.at("boolean_op") == "union");

    // Split via command
    c_input->setText("split");
    QMetaObject::invokeMethod(c_input, "returnPressed");
    assert(bgraph->node_count() == 2);

    // Subtract via command
    c_input->setText("bool subtract");
    QMetaObject::invokeMethod(c_input, "returnPressed");
    assert(bgraph->node_count() == 1);
    const auto sub_id = bcanvas->selected_nodes().front();
    const auto* sub_node = bgraph->find_node(sub_id);
    (void)sub_node;
    assert(sub_node && sub_node->properties.at("boolean_op") == "difference");
  }

  // =========================================================================
  // 17. Colinear Simplification & Border Consistency on Union Shapes
  // =========================================================================
  {
    std::cout << "===> Testing Colinear Geometry Simplification on Union Borders..." << std::endl;
    vector::core::GraphModel cgraph;
    vector::renderer::CanvasWidget ccanvas;
    ccanvas.set_graph_model(&cgraph);

    vector::core::Node rect1;
    rect1.name = "Rect 1";
    rect1.bounds = {0.0, 0.0, 100.0, 50.0};
    rect1.properties["stroke_width"] = "3.5";
    const auto r1_id = cgraph.add_node(std::move(rect1));

    vector::core::Node rect2;
    rect2.name = "Rect 2";
    rect2.bounds = {100.0, 0.0, 100.0, 50.0};
    const auto r2_id = cgraph.add_node(std::move(rect2));

    ccanvas.select_node(r1_id, true);
    ccanvas.select_node(r2_id, false);

    bool ok_union = ccanvas.boolean_combine_selected(vector::core::BooleanOp::Union);
    assert(ok_union);
    (void)ok_union;
    assert(cgraph.node_count() == 1);

    const auto uid = ccanvas.selected_nodes().front();
    const auto* unode = cgraph.find_node(uid);
    assert(unode != nullptr);
    assert(unode->bounds.x == 0.0 && unode->bounds.y == 0.0);
    assert(unode->bounds.width == 200.0 && unode->bounds.height == 50.0);
    // Should simplify colinear edge vertices: abutting rectangles merge into an exact 4-vertex rectangle
    assert(unode->get_world_polygon_vertices().size() == 4);
    // Preserves inherited stroke_width
    assert(unode->properties.at("stroke_width") == "3.5");
    (void)unode;
  }

  // =========================================================================
  // 18. Modular Sub-Section Toolbars & View Toggling
  // =========================================================================
  {
    std::cout << "===> Testing Modular Sub-Section Toolbars & View Toggling..." << std::endl;
    vector::gui::MainWindow tb_win;
    tb_win.show();

    auto* tb_file = tb_win.findChild<QToolBar*>("FileHistoryToolBar");
    auto* tb_draw = tb_win.findChild<QToolBar*>("DrawingToolsToolBar");
    auto* tb_ops = tb_win.findChild<QToolBar*>("FigureOperationsToolBar");
    auto* tb_view = tb_win.findChild<QToolBar*>("ViewNavToolBar");
    auto* tb_aids = tb_win.findChild<QToolBar*>("VisualAidsToolBar");
    auto* tb_hier = tb_win.findChild<QToolBar*>("HierarchyToolBar");

    assert(tb_file != nullptr);
    assert(tb_draw != nullptr);
    assert(tb_ops != nullptr);
    assert(tb_view != nullptr);
    assert(tb_aids != nullptr);
    assert(tb_hier != nullptr);
    (void)tb_file; (void)tb_draw; (void)tb_view; (void)tb_aids; (void)tb_hier;

    assert(tb_file->isMovable());
    assert(tb_draw->isMovable());
    assert(tb_ops->isMovable());
    assert(tb_view->isMovable());
    assert(tb_aids->isMovable());
    assert(tb_hier->isMovable());

    // Toggle visibility using toggleViewAction
    QAction* toggle_ops = tb_ops->toggleViewAction();
    assert(toggle_ops != nullptr);
    assert(tb_ops->isVisible());
    (void)toggle_ops;

    tb_ops->setVisible(false);
    assert(!tb_ops->isVisible());

    // Reset layout restores all toolbars to visible
    tb_win.reset_layout();
    assert(tb_ops->isVisible());
    assert(tb_file->isVisible());
    assert(tb_draw->isVisible());
  }

  // =========================================================================
  // 19. Menu Bar Review & Structured Submenu Grouping
  // =========================================================================
  {
    std::cout << "===> Testing Menu Bar Structure & Submenu Grouping..." << std::endl;
    vector::gui::MainWindow menu_win;
    auto* mbar = menu_win.menuBar();
    assert(mbar != nullptr);

    // Verify top-level menus
    auto find_menu = [&](const QString& title) -> QMenu* {
      for (auto* action : mbar->actions()) {
        if (action->menu() && action->text().contains(title)) {
          return action->menu();
        }
      }
      return nullptr;
    };

    auto* file_menu = find_menu("File");
    auto* edit_menu = find_menu("Edit");
    auto* insert_menu = find_menu("Insert");
    auto* view_menu = find_menu("View");
    auto* hier_menu = find_menu("Hierarchy");
    auto* themes_menu = find_menu("Themes");
    auto* help_menu = find_menu("Help");

    assert(file_menu != nullptr);
    assert(edit_menu != nullptr);
    assert(insert_menu != nullptr);
    assert(view_menu != nullptr);
    assert(hier_menu != nullptr);
    assert(themes_menu != nullptr);
    assert(help_menu != nullptr);
    (void)file_menu; (void)hier_menu; (void)themes_menu; (void)help_menu;

    // Helper to find submenu within menu
    auto find_submenu = [](QMenu* parent, const QString& title) -> QMenu* {
      for (auto* act : parent->actions()) {
        if (act->menu() && act->text().contains(title)) {
          return act->menu();
        }
      }
      return nullptr;
    };

    // Edit -> Boolean Operations submenu
    auto* bool_submenu = find_submenu(edit_menu, "Boolean Operations");
    assert(bool_submenu != nullptr);
    bool has_union = false, has_inter = false, has_sub = false, has_xor = false;
    for (auto* act : bool_submenu->actions()) {
      if (act->text().contains("Union")) has_union = true;
      if (act->text().contains("Intersection")) has_inter = true;
      if (act->text().contains("Subtract")) has_sub = true;
      if (act->text().contains("XOR")) has_xor = true;
    }
    assert(has_union && has_inter && has_sub && has_xor);
    (void)has_union; (void)has_inter; (void)has_sub; (void)has_xor;

    // Insert -> 2D Figures submenu
    auto* figures_submenu = find_submenu(insert_menu, "2D Figures");
    assert(figures_submenu != nullptr);
    (void)figures_submenu;

    // View -> Toolbars submenu
    auto* toolbars_submenu = find_submenu(view_menu, "Toolbars");
    assert(toolbars_submenu != nullptr);
    assert(toolbars_submenu->actions().size() >= 6);
    (void)toolbars_submenu;
  }

  // =========================================================================
  // 20. New Diagram Inside Hierarchical Module & Unsaved Changes Tracking
  // =========================================================================
  {
    std::cout << "===> Testing New Diagram from Inside Hierarchical Module..." << std::endl;
    vector::gui::MainWindow hwin;

    // Start with clean state
    assert(!hwin.is_document_modified());

    // 1. Add a hierarchical subsystem block
    const auto sub_id = hwin.canvas()->add_subsystem({200.0, 200.0}, "ProcessorCore");
    assert(sub_id != vector::core::InvalidNodeId);
    assert(hwin.is_document_modified());

    // Drill down inside the subsystem
    bool ok_down = hwin.canvas()->drill_down_node(sub_id);
    assert(ok_down);
    (void)ok_down;
    assert(hwin.canvas()->is_inside_subsystem());
    assert(hwin.canvas()->can_pop_scope());
    assert(hwin.canvas()->graph_model() != hwin.graph_model());

    // Add elements inside the subsystem
    const auto inner_id = hwin.canvas()->add_shape(vector::core::ShapeType::Rectangle, {80.0, 80.0}, "ALU");
    assert(inner_id != vector::core::InvalidNodeId);
    (void)inner_id;
    assert(hwin.is_document_modified());

    // 2. Start a New Diagram while drilled down inside the subsystem!
    bool ok_new = hwin.new_diagram();
    assert(ok_new);
    (void)ok_new;

    // 3. Verify clean reset to root level:
    assert(!hwin.canvas()->is_inside_subsystem());
    assert(!hwin.canvas()->can_pop_scope());
    assert(hwin.canvas()->graph_model() == hwin.graph_model());
    assert(hwin.graph_model()->node_count() == 0);
    assert(!hwin.is_document_modified());

    // 4. Verify we can immediately add shapes to root diagram without issue
    const auto new_root_node = hwin.canvas()->add_shape(vector::core::ShapeType::Rectangle, {150.0, 150.0}, "TopNode");
    assert(new_root_node != vector::core::InvalidNodeId);
    (void)new_root_node;
    assert(hwin.graph_model()->node_count() == 1);
    assert(hwin.is_document_modified());

    // 5. Test saving and modified flag clearing
    const QString temp_diag = "/tmp/test_vector_save_prompt.yaml";
    bool ok_save = hwin.save_diagram_path(temp_diag);
    assert(ok_save);
    (void)ok_save;
    assert(!hwin.is_document_modified());

    // 6. Test opening a diagram while inside a subsystem also resets cleanly
    const auto sub2_id = hwin.canvas()->add_subsystem({300.0, 300.0}, "Peripherals");
    assert(hwin.canvas()->drill_down_node(sub2_id));
    assert(hwin.canvas()->is_inside_subsystem());
    (void)sub2_id;

    bool ok_open = hwin.open_diagram_path(temp_diag);
    assert(ok_open);
    (void)ok_open;
    assert(!hwin.canvas()->is_inside_subsystem());
    assert(hwin.canvas()->graph_model() == hwin.graph_model());
    assert(hwin.graph_model()->node_count() == 1);
    assert(!hwin.is_document_modified());

    // Clean up temp file
    std::remove(temp_diag.toStdString().c_str());
  }

  // 26. Test Centering Ports on Sheet Edges and Zoom-Fit on New Hierarchy
  {
    std::cout << "===> Testing Sheet Edge Port Centering & Zoom-Fit on New Hierarchy..." << std::endl;
    vector::gui::MainWindow hwin;
    auto* canvas = hwin.canvas();
    auto* graph = hwin.graph_model();

    // Create a subsystem block with 2 inputs and 2 outputs in parent graph
    const auto block_id = canvas->add_subsystem({200.0, 200.0}, "ComputeEngine");
    assert(block_id != vector::core::InvalidNodeId);
    auto* block_node = graph->find_node(block_id);
    assert(block_node != nullptr);
    (void)block_node;

    // Add 2 inputs on West and 2 outputs on East
    vector::core::Port in1;
    in1.parent_node = block_id; in1.name = "clk"; in1.local_anchor = {0.0, 0.3};
    in1.direction = vector::core::PortDirection::West; in1.pin_type = "input"; in1.is_boundary_interface = true;
    graph->add_port(std::move(in1));

    vector::core::Port in2;
    in2.parent_node = block_id; in2.name = "rst_n"; in2.local_anchor = {0.0, 0.7};
    in2.direction = vector::core::PortDirection::West; in2.pin_type = "input"; in2.is_boundary_interface = true;
    graph->add_port(std::move(in2));

    vector::core::Port out1;
    out1.parent_node = block_id; out1.name = "data_out"; out1.local_anchor = {1.0, 0.3};
    out1.direction = vector::core::PortDirection::East; out1.pin_type = "output"; out1.is_boundary_interface = true;
    graph->add_port(std::move(out1));

    vector::core::Port out2;
    out2.parent_node = block_id; out2.name = "valid"; out2.local_anchor = {1.0, 0.7};
    out2.direction = vector::core::PortDirection::East; out2.pin_type = "output"; out2.is_boundary_interface = true;
    graph->add_port(std::move(out2));

    // Drill down into the newly created hierarchy
    bool ok_drill = canvas->drill_down_node(block_id);
    assert(ok_drill);
    (void)ok_drill;
    assert(canvas->is_inside_subsystem());
    auto* child_g = canvas->graph_model();
    assert(child_g != nullptr);
    assert(child_g->node_count() == 4);

    const auto page = canvas->page_bounds();
    const double margin = canvas->page_margin();
    const double expected_in_x = page.x + margin;
    const double expected_out_x = page.x + page.width - margin - 84.0; // port_w is 84.0
    (void)expected_in_x; (void)expected_out_x;

    size_t in_count = 0;
    size_t out_count = 0;
    for (const auto& [nid, n] : child_g->nodes()) {
      if (n.shape == vector::core::ShapeType::InputPort) {
        in_count++;
        assert(std::abs(n.bounds.x - expected_in_x) < 1e-3);
        // Verify vertically centered within sheet
        assert(n.bounds.y >= page.y && n.bounds.bottom() <= page.bottom());
      } else if (n.shape == vector::core::ShapeType::OutputPort) {
        out_count++;
        assert(std::abs(n.bounds.x - expected_out_x) < 1e-3);
        assert(n.bounds.y >= page.y && n.bounds.bottom() <= page.bottom());
      }
    }
    assert(in_count == 2);
    assert(out_count == 2);

    // Verify zoom-fit content encloses the sheet and ports properly
    canvas->fit_content();
    assert(canvas->zoom_level() > 0.0);
    assert(canvas->zoom_level() <= 4.0);

    // Return back to root
    assert(canvas->pop_hierarchy_scope());
    assert(!canvas->is_inside_subsystem());
  }

  // 27. Test Definition Synchronization vs Instance Drift Mode (Inclusive Language)
  {
    std::cout << "===> Testing Definition Synchronization & Instance Drift Architecture..." << std::endl;
    vector::core::GraphModel top_graph;
    vector::core::GraphModel child_graph;

    // Create child diagram with internal ports
    vector::core::Node in_marker;
    in_marker.name = "clk_in";
    in_marker.shape = vector::core::ShapeType::InputPort;
    in_marker.bounds = {20.0, 100.0, 84.0, 28.0};
    auto in_id = child_graph.add_node(std::move(in_marker));
    vector::core::Port child_in;
    child_in.parent_node = in_id; child_in.name = "clk_in"; child_in.local_anchor = {1.0, 0.5};
    child_in.direction = vector::core::PortDirection::East; child_in.pin_type = "input"; child_in.is_boundary_interface = true;
    child_graph.add_port(std::move(child_in));

    vector::core::Node out_marker;
    out_marker.name = "data_out";
    out_marker.shape = vector::core::ShapeType::OutputPort;
    out_marker.bounds = {400.0, 100.0, 84.0, 28.0};
    auto out_id = child_graph.add_node(std::move(out_marker));
    vector::core::Port child_out;
    child_out.parent_node = out_id; child_out.name = "data_out"; child_out.local_anchor = {0.0, 0.5};
    child_out.direction = vector::core::PortDirection::West; child_out.pin_type = "output"; child_out.is_boundary_interface = true;
    child_graph.add_port(std::move(child_out));

    // Instance 1: Follow Primary Definition
    vector::core::Node inst1;
    inst1.name = "Instance_Synchronized";
    inst1.bounds = {100.0, 100.0, 250.0, 150.0};
    inst1.properties["definition_sync_mode"] = "follow_primary";
    auto inst1_id = top_graph.add_node(std::move(inst1));
    auto* node1 = top_graph.find_node(inst1_id);

    // Sync instance 1: resets dimensions to default symbol fit
    vector::core::SymbolGenerator::sync_symbol_from_diagram(*node1, top_graph, child_graph);
    assert(node1->port_ids.size() == 2);
    assert(node1->bounds.width == 160.0); // Reset to primary definition default
    assert(node1->bounds.height == 100.0);

    // Instance 2: Allow Drift (Independent Instance Override)
    vector::core::Node inst2;
    inst2.name = "Instance_Drifting";
    inst2.bounds = {400.0, 100.0, 320.0, 220.0}; // Custom dimensions
    inst2.properties["definition_sync_mode"] = "allow_drift";
    auto inst2_id = top_graph.add_node(std::move(inst2));
    auto* node2 = top_graph.find_node(inst2_id);

    // Manually add custom port pin placement
    vector::core::Port custom_p;
    custom_p.parent_node = inst2_id; custom_p.name = "clk_in"; custom_p.local_anchor = {0.0, 0.25};
    custom_p.direction = vector::core::PortDirection::West; custom_p.pin_type = "input"; custom_p.is_boundary_interface = true;
    top_graph.add_port(std::move(custom_p));

    // Sync instance 2: MUST preserve custom dimensions and existing pin placements!
    vector::core::SymbolGenerator::sync_symbol_from_diagram(*node2, top_graph, child_graph);
    assert(node2->bounds.width == 320.0);   // Preserved!
    assert(node2->bounds.height == 220.0);  // Preserved!
    assert(node2->port_ids.size() == 2);    // New port appended without clobbering custom pin

    auto* p_clk = top_graph.find_port(node2->port_ids.front());
    assert(p_clk != nullptr);
    assert(p_clk->local_anchor.y == 0.25);  // Preserved custom offset!
    (void)p_clk;
  }

  // 28. Test Symbol Editor Dialog and Console Commands
  {
    std::cout << "===> Testing Symbol Editor Dialog and Console Symbol/Drift Commands..." << std::endl;
    vector::gui::MainWindow hwin;
    auto* canvas = hwin.canvas();
    auto* graph = hwin.graph_model();

    const auto sub_id = canvas->add_subsystem({250.0, 250.0}, "DSP_Block");
    assert(sub_id != vector::core::InvalidNodeId);
    auto* sub_node = graph->find_node(sub_id);
    assert(sub_node != nullptr);

    // Test Dialog creation and setup
    vector::gui::SymbolEditorDialog dlg(*sub_node, *graph, nullptr, &hwin);
    assert(!dlg.is_drift_allowed());

    // Test Console drift on/off commands
    auto* console = hwin.findChild<vector::gui::CommandConsoleDock*>("CommandConsoleDock");
    assert(console != nullptr);

    // Select the subsystem
    canvas->select_node(sub_id);
    assert(sub_node->selected);

    // Execute "drift on" via console
    console->set_canvas(canvas);
    auto* input_line = console->findChild<QLineEdit*>();
    assert(input_line != nullptr);
    input_line->setText("drift on");
    QMetaObject::invokeMethod(input_line, "returnPressed");

    assert(sub_node->properties["definition_sync_mode"] == "allow_drift");

    // Execute "drift off" via console
    input_line->setText("drift off");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(sub_node->properties["definition_sync_mode"] == "follow_primary");

    // Execute "symbol fit" via console
    input_line->setText("symbol fit");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(sub_node->bounds.width >= 140.0);
    assert(sub_node->bounds.height >= 80.0);
  }

  // 29. Test Direct Vector Rendering Pipeline for All Shapes & Wires
  {
    std::cout << "===> Testing Smooth Antialiased Vector Drawing Pipeline (Nodes, Wires, Badges)..." << std::endl;
    vector::gui::MainWindow hwin;
    auto* canvas = hwin.canvas();

    // Add variety of figures
    const auto r_id = canvas->add_shape(vector::core::ShapeType::Rectangle, {100.0, 100.0}, "Rect1");
    const auto rr_id = canvas->add_shape(vector::core::ShapeType::RoundedRect, {250.0, 100.0}, "RoundRect1");
    const auto el_id = canvas->add_shape(vector::core::ShapeType::Ellipse, {400.0, 100.0}, "Ellipse1");
    const auto sub_id = canvas->add_subsystem({250.0, 250.0}, "Subsystem1");
    const auto wire_id = canvas->add_wire_by_points({100.0, 100.0}, {250.0, 250.0}, "Wire1");

    assert(r_id != vector::core::InvalidNodeId);
    assert(rr_id != vector::core::InvalidNodeId);
    assert(el_id != vector::core::InvalidNodeId);
    assert(sub_id != vector::core::InvalidNodeId);
    assert(wire_id != vector::core::InvalidEdgeId);
    (void)r_id; (void)rr_id; (void)el_id; (void)sub_id; (void)wire_id;

    // Execute frame rendering to ensure zero crashes and clean drawing pass
    canvas->render_frame_for_test();

    // Select each and verify rendering under selection
    canvas->select_node(r_id);
    canvas->render_frame_for_test();

    canvas->select_node(rr_id);
    canvas->render_frame_for_test();

    canvas->select_node(el_id);
    canvas->render_frame_for_test();

    canvas->select_node(sub_id);
    canvas->render_frame_for_test();
  }

  // 30. Test Interactive Shape Placement Preview, Ghost Tracking & Snapping
  {
    std::cout << "===> Testing Interactive Shape Placement Preview, Ghost Tracking & Snapping..." << std::endl;
    vector::gui::MainWindow hwin;
    hwin.resize(1024, 768);
    auto* canvas = hwin.canvas();
    assert(canvas != nullptr);
    canvas->graph_model()->clear();

    // Initial state: Select mode, no placement active
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(!canvas->is_placement_tool_active());

    // Add an initial anchor node at world (100, 100)
    const auto anchor_id = canvas->add_shape(vector::core::ShapeType::Rectangle, {100.0, 100.0}, "AnchorNode");
    assert(anchor_id != vector::core::InvalidNodeId);

    // Enter AddRectangle tool mode
    canvas->set_tool_mode(vector::renderer::ToolMode::AddRectangle);
    assert(canvas->is_placement_tool_active());
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddRectangle);

    // Move mouse near the anchor node's top edge (e.g. world 102.0, 100.0)
    (void)anchor_id;
    const auto s_pos = canvas->world_to_screen({102.0, 100.0});
    const QPointF q_sp(s_pos.x, s_pos.y);
    QMouseEvent move_evt(QEvent::MouseMove, q_sp, q_sp, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move_evt);

    // Verify placement preview bounds was updated with dimensions 120x70
    const auto& prev_b = canvas->placement_preview_bounds();
    assert(prev_b.is_valid());
    assert(std::abs(prev_b.width - 120.0) < 1e-3);
    assert(std::abs(prev_b.height - 70.0) < 1e-3);
    (void)prev_b;

    // Verify smart alignment guides or grid snapping active
    assert(canvas->active_smart_guides().size() >= 1 || prev_b.center().x == 100.0);

    // Render frame to ensure ghost painting pass succeeds
    canvas->render_frame_for_test();

    // Click left button to drop shape
    QMouseEvent press_evt(QEvent::MouseButtonPress, q_sp, q_sp, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press_evt);

    // Verify shape was added and mode returned to Select
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(!canvas->is_placement_tool_active());
    assert(canvas->graph_model()->node_count() == 2);

    // Test Escape cancellation
    canvas->set_tool_mode(vector::renderer::ToolMode::AddEllipse);
    assert(canvas->is_placement_tool_active());
    QKeyEvent esc_evt(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &esc_evt);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(!canvas->is_placement_tool_active());

    // Test Right-Click cancellation on polygon tool
    canvas->set_tool_mode(vector::renderer::ToolMode::AddPolygon);
    assert(canvas->is_polygon_drawing_active());
    assert(!canvas->is_placement_tool_active());
    QMouseEvent rclick_evt(QEvent::MouseButtonPress, q_sp, q_sp, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &rclick_evt);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(!canvas->is_polygon_drawing_active());

    // Test All Tool Preview Rendering Passes (RoundedRect, HierarchicalBlock, InputPort, OutputPort)
    canvas->set_tool_mode(vector::renderer::ToolMode::AddRoundedRect);
    canvas->render_frame_for_test();
    canvas->set_tool_mode(vector::renderer::ToolMode::AddHierarchicalBlock);
    canvas->render_frame_for_test();
    canvas->set_tool_mode(vector::renderer::ToolMode::AddInputPort);
    canvas->render_frame_for_test();
    canvas->set_tool_mode(vector::renderer::ToolMode::AddOutputPort);
    canvas->render_frame_for_test();
    canvas->cancel_placement();
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    canvas->render_frame_for_test();
  }

  // 31. Test Interactive Point-and-Click Polygon Creation Tool & Closed Borders
  {
    std::cout << "===> Testing Point-and-Click Polygon Creation & Complete Closed Borders..." << std::endl;
    vector::gui::MainWindow hwin;
    hwin.resize(1024, 768);
    auto* canvas = hwin.canvas();
    assert(canvas != nullptr);

    // Initial node count
    const size_t initial_count = canvas->graph_model()->node_count();

    // 1. Activate Point-and-Click Polygon Drawing
    canvas->set_tool_mode(vector::renderer::ToolMode::AddPolygon);
    assert(canvas->is_polygon_drawing_active());
    assert(canvas->in_progress_polygon_points().empty());

    // 2. Click Vertex 1: (50, 50)
    auto s1 = canvas->world_to_screen({50.0, 50.0});
    QPointF qs1(s1.x, s1.y);
    QMouseEvent m1(QEvent::MouseMove, qs1, qs1, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &m1);
    QMouseEvent p1(QEvent::MouseButtonPress, qs1, qs1, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &p1);
    assert(canvas->in_progress_polygon_points().size() == 1);

    // 3. Click Vertex 2: (150, 50)
    auto s2 = canvas->world_to_screen({150.0, 50.0});
    QPointF qs2(s2.x, s2.y);
    QMouseEvent m2(QEvent::MouseMove, qs2, qs2, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &m2);
    QMouseEvent p2(QEvent::MouseButtonPress, qs2, qs2, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &p2);
    assert(canvas->in_progress_polygon_points().size() == 2);

    // 4. Click Vertex 3: (150, 150)
    auto s3 = canvas->world_to_screen({150.0, 150.0});
    QPointF qs3(s3.x, s3.y);
    QMouseEvent m3(QEvent::MouseMove, qs3, qs3, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &m3);
    QMouseEvent p3(QEvent::MouseButtonPress, qs3, qs3, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &p3);
    assert(canvas->in_progress_polygon_points().size() == 3);

    // 5. Click Vertex 4: (50, 150)
    auto s4 = canvas->world_to_screen({50.0, 150.0});
    QPointF qs4(s4.x, s4.y);
    QMouseEvent m4(QEvent::MouseMove, qs4, qs4, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &m4);
    QMouseEvent p4(QEvent::MouseButtonPress, qs4, qs4, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &p4);
    assert(canvas->in_progress_polygon_points().size() == 4);

    // Render frame while drawing overlay is active
    canvas->render_frame_for_test();

    // 6. Right-click to finish polygon
    QMouseEvent r_finish(QEvent::MouseButtonPress, qs4, qs4, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &r_finish);

    // Verify finished state
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(!canvas->is_polygon_drawing_active());
    assert(canvas->in_progress_polygon_points().empty());
    assert(canvas->graph_model()->node_count() == initial_count + 1);

    // Verify newly added polygon geometry
    const auto nid = canvas->graph_model()->node_count();
    const auto* poly_node = canvas->graph_model()->find_node(nid);
    assert(poly_node != nullptr);
    assert(poly_node->shape == vector::core::ShapeType::CustomPolygon);
    assert(poly_node->polygon_vertices.size() == 4);
    assert(std::abs(poly_node->bounds.width - 100.0) < 1e-3);
    assert(std::abs(poly_node->bounds.height - 100.0) < 1e-3);
    (void)initial_count;
    (void)poly_node;

    // Verify regular hexagon has complete closed borders
    const auto hex_id = canvas->add_polygon({300.0, 300.0}, 6, "RegularHexagon");
    assert(hex_id != vector::core::InvalidNodeId);
    (void)hex_id;
    canvas->render_frame_for_test();
  }

  // 32. Test Z-Order Stacking Operations (Bring to Front, Send to Back, Bring Forward, Send Backward, Hit-Testing, Undo/Redo)
  {
    std::cout << "===> Testing Stacking Order Operations (Bring to Front, Send to Back, etc.)..." << std::endl;
    vector::gui::MainWindow hwin;
    hwin.resize(1024, 768);
    auto* canvas = hwin.findChild<vector::renderer::CanvasWidget*>();
    assert(canvas != nullptr);
    auto* history = canvas->command_history();
    (void)history;

    // Reset diagram to clean state
    canvas->select_all();
    canvas->delete_selected();
    assert(canvas->graph_model()->node_count() == 0);

    // Create 3 overlapping shapes: A (bottom), B (middle), C (top)
    // All overlapping at point (200, 200)
    const auto id_a = canvas->add_shape(vector::core::ShapeType::Rectangle, {200.0, 200.0}, "ShapeA", false);
    const auto id_b = canvas->add_shape(vector::core::ShapeType::Rectangle, {200.0, 200.0}, "ShapeB", false);
    const auto id_c = canvas->add_shape(vector::core::ShapeType::Rectangle, {200.0, 200.0}, "ShapeC", false);

    assert(id_a != vector::core::InvalidNodeId);
    assert(id_b != vector::core::InvalidNodeId);
    assert(id_c != vector::core::InvalidNodeId);

    auto* n_a = canvas->graph_model()->find_node(id_a);
    auto* n_b = canvas->graph_model()->find_node(id_b);
    auto* n_c = canvas->graph_model()->find_node(id_c);
    assert(n_a && n_b && n_c);
    (void)n_a; (void)n_b; (void)n_c;

    // Initial z-order must be A < B < C
    assert(n_a->z_order < n_b->z_order);
    assert(n_b->z_order < n_c->z_order);

    // Hit-testing at center (200, 200) must pick C (the topmost node)
    auto s_pt = canvas->world_to_screen({200.0, 200.0});
    QPointF qs_pt(s_pt.x, s_pt.y);
    QMouseEvent press_c(QEvent::MouseButtonPress, qs_pt, qs_pt, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press_c);
    assert(canvas->selected_nodes().size() == 1);
    assert(canvas->selected_nodes().front() == id_c);

    // Select ShapeA and Bring to Front
    canvas->select_node(id_a, true);
    assert(canvas->selected_nodes().size() == 1);
    assert(canvas->selected_nodes().front() == id_a);
    bool ok = canvas->bring_to_front_selected();
    assert(ok);
    // Now ShapeA must be on top: B < C < A
    assert(n_b->z_order < n_c->z_order);
    assert(n_c->z_order < n_a->z_order);

    // Hit-testing at (200, 200) must now pick ShapeA
    canvas->clear_selection();
    QMouseEvent press_a(QEvent::MouseButtonPress, qs_pt, qs_pt, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press_a);
    assert(canvas->selected_nodes().size() == 1);
    assert(canvas->selected_nodes().front() == id_a);

    // Send ShapeA to Back
    ok = canvas->send_to_back_selected();
    assert(ok);
    // Now ShapeA must be at the bottom: A < B < C
    assert(n_a->z_order < n_b->z_order);
    assert(n_b->z_order < n_c->z_order);

    // Bring Forward on ShapeA: swaps A with B -> B < A < C
    ok = canvas->bring_forward_selected();
    assert(ok);
    assert(n_b->z_order < n_a->z_order);
    assert(n_a->z_order < n_c->z_order);

    // Bring Forward again on ShapeA: swaps A with C -> B < C < A
    ok = canvas->bring_forward_selected();
    assert(ok);
    assert(n_b->z_order < n_c->z_order);
    assert(n_c->z_order < n_a->z_order);

    // Send Backward on ShapeA: swaps A with C -> B < A < C
    ok = canvas->send_backward_selected();
    assert(ok);
    assert(n_b->z_order < n_a->z_order);
    assert(n_a->z_order < n_c->z_order);

    // Test Undo: reverts Send Backward -> B < C < A
    if (history) {
      assert(history->can_undo());
      history->undo();
      assert(n_b->z_order < n_c->z_order);
      assert(n_c->z_order < n_a->z_order);

      // Test Redo: re-applies Send Backward -> B < A < C
      assert(history->can_redo());
      history->redo();
      assert(n_b->z_order < n_a->z_order);
      assert(n_a->z_order < n_c->z_order);
    }

    // Test Console Commands for Z-order:
    auto* console = hwin.findChild<vector::gui::CommandConsoleDock*>();
    assert(console != nullptr);
    auto* input_line = console->findChild<QLineEdit*>();
    assert(input_line != nullptr);

    // Select ShapeB
    canvas->select_node(id_b, true);

    // Execute console command "bring to front"
    input_line->setText("bring to front");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    // Now ShapeB must be on top: A < C < B
    assert(n_a->z_order < n_c->z_order);
    assert(n_c->z_order < n_b->z_order);

    // Execute console command "send to back"
    input_line->setText("send to back");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    // Now ShapeB must be on bottom: B < A < C
    assert(n_b->z_order < n_a->z_order);
    assert(n_a->z_order < n_c->z_order);

    // Execute console command "forward"
    input_line->setText("forward");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    // Now ShapeB moved one step up: A < B < C
    assert(n_a->z_order < n_b->z_order);
    assert(n_b->z_order < n_c->z_order);

    // Execute console command "backward"
    input_line->setText("backward");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    // Now ShapeB moved one step down: B < A < C
    assert(n_b->z_order < n_a->z_order);
    assert(n_a->z_order < n_c->z_order);

    // Execute console command "order front"
    input_line->setText("order front");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(n_a->z_order < n_c->z_order);
    assert(n_c->z_order < n_b->z_order);

    // Render frame to ensure sorted vector drawing pass executes cleanly without error
    canvas->render_frame_for_test();
    (void)ok;
  }

  // 33. Test Status Line Hierarchy Path, Typography Styling, XML Default & SVG/Raster Image Export
  {
    std::cout << "===> Testing Status Bar Hierarchy Path, Quick Styling, XML Default & SVG/Raster Exporters..." << std::endl;
    vector::gui::MainWindow hwin;
    auto* canvas = hwin.canvas();
    auto* graph = hwin.graph_model();
    auto* status_lbl = hwin.status_hierarchy_label();
    auto* btn_up = hwin.btn_pop_scope_status();

    assert(status_lbl != nullptr);
    assert(btn_up != nullptr);
    (void)status_lbl;
    (void)btn_up;

    // Initial state: Top scope, Up button disabled
    assert(status_lbl->text().contains("Top"));
    assert(!btn_up->isEnabled());

    // 1. Hierarchy drilldown and status bar reflection
    const auto sub_id = canvas->add_subsystem({200.0, 200.0}, "FilterBlock");
    assert(sub_id != vector::core::InvalidNodeId);
    (void)sub_id;
    assert(canvas->drill_down_node(sub_id));
    assert(canvas->is_inside_subsystem());

    // Status bar hierarchy breadcrumb must now show "FilterBlock" and Up button must be enabled
    assert(status_lbl->text().contains("FilterBlock"));
    assert(btn_up->isEnabled());

    // Click Up button on status bar -> ascends back to Top
    btn_up->click();
    assert(!canvas->is_inside_subsystem());
    assert(status_lbl->text().contains("Top"));
    assert(!btn_up->isEnabled());

    // 2. Quick Styling & Typography on Canvas Nodes
    const auto rect_id = canvas->add_shape(vector::core::ShapeType::Rectangle, {300.0, 200.0}, "StyledNode");
    assert(rect_id != vector::core::InvalidNodeId);
    canvas->select_node(rect_id, false);

    // Apply font family, font size, bold, italic, underline
    canvas->set_selected_nodes_font_family("Arial");
    canvas->set_selected_nodes_font_size(16);
    canvas->set_selected_nodes_font_bold(true);
    canvas->set_selected_nodes_font_italic(true);
    canvas->set_selected_nodes_font_underline(true);
    canvas->set_selected_nodes_fill_color("#3B82F6");
    canvas->set_selected_nodes_stroke_color("#EF4444");
    canvas->set_selected_nodes_text_color("#FFFFFF");
    canvas->set_selected_nodes_stroke_width(2.5);

    auto* n = graph->find_node(rect_id);
    assert(n != nullptr);
    (void)n;
    assert(n->properties["font_family"] == "Arial");
    assert(n->properties["font_size"] == "16");
    assert(n->properties["font_bold"] == "true");
    assert(n->properties["font_italic"] == "true");
    assert(n->properties["font_underline"] == "true");
    assert(n->properties["fill_color"] == "#3B82F6");
    assert(n->properties["stroke_color"] == "#EF4444");
    assert(n->properties["text_color"] == "#FFFFFF");
    assert(n->properties["stroke_width"] == "2.5");

    // Render frame to ensure per-node typography rendering executes cleanly
    canvas->render_frame_for_test();

    // 3. XML Default Save and Open roundtrip
    const QString xml_path = "/tmp/test_diagram_default.xml";
    std::remove(xml_path.toStdString().c_str());

    bool ok_save = hwin.save_diagram_path(xml_path);
    assert(ok_save);
    assert(QFileInfo::exists(xml_path));

    // Verify XML format (must contain mxfile and mxCell)
    std::ifstream xml_in(xml_path.toStdString());
    assert(xml_in.is_open());
    std::string xml_content((std::istreambuf_iterator<char>(xml_in)),
                             std::istreambuf_iterator<char>());
    assert(xml_content.find("<mxfile") != std::string::npos);
    assert(xml_content.find("<mxCell") != std::string::npos);

    // Re-open XML diagram
    bool ok_open = hwin.open_diagram_path(xml_path);
    assert(ok_open);
    assert(hwin.graph_model()->node_count() >= 2);
    auto* reloaded_n = hwin.graph_model()->find_node_by_name("StyledNode");
    assert(reloaded_n != nullptr);
    assert(reloaded_n->properties["fill_color"] == "#3B82F6");
    assert(reloaded_n->properties["stroke_color"] == "#EF4444");
    assert(reloaded_n->properties["text_color"] == "#FFFFFF");
    std::remove(xml_path.toStdString().c_str());

    // 4. Scalable Vector Graphics (SVG) and Raster Image (PNG / JPEG) Export
    const QString svg_path = "/tmp/test_export_vector.svg";
    const QString png_path = "/tmp/test_export_raster.png";
    const QString jpg_path = "/tmp/test_export_raster.jpg";
    std::remove(svg_path.toStdString().c_str());
    std::remove(png_path.toStdString().c_str());
    std::remove(jpg_path.toStdString().c_str());

    bool ok_svg = hwin.export_svg_path(svg_path);
    assert(ok_svg);
    assert(QFileInfo::exists(svg_path));
    std::ifstream svg_in(svg_path.toStdString());
    std::string svg_content((std::istreambuf_iterator<char>(svg_in)),
                             std::istreambuf_iterator<char>());
    assert(svg_content.find("<svg") != std::string::npos);
    assert(svg_content.find("StyledNode") != std::string::npos);
    std::remove(svg_path.toStdString().c_str());

    bool ok_png = hwin.export_image_path(png_path, "PNG");
    assert(ok_png);
    assert(QFileInfo::exists(png_path));
    assert(QFileInfo(png_path).size() > 0);
    std::remove(png_path.toStdString().c_str());

    bool ok_jpg = hwin.export_image_path(jpg_path, "JPEG");
    assert(ok_jpg);
    assert(QFileInfo::exists(jpg_path));
    assert(QFileInfo(jpg_path).size() > 0);
    std::remove(jpg_path.toStdString().c_str());

    // 5. Console Commands for Typography, Color & Export
    auto* console = hwin.findChild<vector::gui::CommandConsoleDock*>();
    assert(console != nullptr);
    auto* input_line = console->findChild<QLineEdit*>();
    assert(input_line != nullptr);

    // Select StyledNode
    canvas->select_node(reloaded_n->id, false);

    // Test console "font 24"
    input_line->setText("font 24");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(reloaded_n->properties["font_size"] == "24");

    // Test console "fill #10B981"
    input_line->setText("fill #10B981");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(reloaded_n->properties["fill_color"] == "#10B981");

    // Test console "stroke 4.0"
    input_line->setText("stroke 4.0");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(reloaded_n->properties["stroke_width"] == "4");

    // Test console "export svg /tmp/test_console_export.svg"
    const QString console_svg = "/tmp/test_console_export.svg";
    std::remove(console_svg.toStdString().c_str());
    input_line->setText("export svg " + console_svg);
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(QFileInfo::exists(console_svg));
    std::remove(console_svg.toStdString().c_str());

    // Test console "style reset"
    input_line->setText("style reset");
    QMetaObject::invokeMethod(input_line, "returnPressed");
    assert(reloaded_n->properties.find("fill_color") == reloaded_n->properties.end());

    canvas->render_frame_for_test();
    (void)ok_save;
    (void)ok_open;
    (void)ok_svg;
    (void)ok_png;
    (void)ok_jpg;
  }

  // 34. Test Nested Hierarchical Submodules Drill-down Loop Prevention & Multi-level Scope Traversal
  {
    std::cout << "===> Testing Nested Submodules Drill-Down, Status Bar Path Truncation & Loop Prevention..." << std::endl;
    vector::gui::MainWindow hwin;
    auto* canvas = hwin.canvas();
    auto* root_graph = hwin.graph_model();
    auto* status_lbl = hwin.status_hierarchy_label();
    assert(canvas != nullptr);
    assert(root_graph != nullptr);
    assert(status_lbl != nullptr);

    // Initial state: hierarchy is shown on bottom status bar, NOT on toolbar
    assert(status_lbl->text().contains("Top"));

    // Step 1: Add a Subsystem in root diagram
    const vector::core::NodeId sub1_id = canvas->add_subsystem(vector::core::Vec2d{100.0, 100.0}, "SubsystemAlpha", false);
    assert(sub1_id != vector::core::InvalidNodeId);
    auto* sub1_node = root_graph->find_node(sub1_id);
    assert(sub1_node != nullptr);
    const std::string sub1_uuid = sub1_node->properties["subsystem_uuid"];
    assert(!sub1_uuid.empty());

    // Step 2: Drill down into SubsystemAlpha
    bool ok_drill1 = canvas->drill_down_node(sub1_id);
    assert(ok_drill1);
    auto* sub1_graph = canvas->graph_model();
    assert(sub1_graph != nullptr);
    assert(sub1_graph != root_graph);
    assert(canvas->current_breadcrumb_path().find("SubsystemAlpha") != std::string::npos);
    assert(status_lbl->text().contains("SubsystemAlpha"));

    // Step 3: Add a nested Submodule inside SubsystemAlpha
    const vector::core::NodeId sub2_id = canvas->add_subsystem(vector::core::Vec2d{200.0, 200.0}, "SubmoduleBeta", false);
    assert(sub2_id != vector::core::InvalidNodeId);
    auto* sub2_node = sub1_graph->find_node(sub2_id);
    assert(sub2_node != nullptr);
    const std::string sub2_uuid = sub2_node->properties["subsystem_uuid"];
    assert(!sub2_uuid.empty());
    assert(sub2_uuid != sub1_uuid); // Crucial: UUIDs must be distinct!

    // Step 4: Drill down into SubmoduleBeta
    bool ok_drill2 = canvas->drill_down_node(sub2_id);
    assert(ok_drill2);
    auto* sub2_graph = canvas->graph_model();
    assert(sub2_graph != nullptr);
    assert(sub2_graph != sub1_graph);
    assert(sub2_graph != root_graph);
    assert(canvas->current_breadcrumb_path().find("SubmoduleBeta") != std::string::npos);
    assert(status_lbl->text().contains("SubmoduleBeta"));

    // Step 5: Inside SubmoduleBeta, add a leaf node
    const vector::core::NodeId leaf_id = canvas->add_shape(vector::core::ShapeType::Rectangle, vector::core::Vec2d{300.0, 300.0}, "LeafNode", false);
    assert(leaf_id != vector::core::InvalidNodeId);
    assert(sub2_graph->find_node(leaf_id) != nullptr);

    // Step 6: Test status bar breadcrumb truncation when hierarchy becomes too large
    // A path with > 3 segments or exceeding max characters must start with "..." and show the last few elements
    QString deep_path = "Top / SystemMain / ProcessingUnit / ArithmeticLogic / RegisterBank";
    QString truncated = vector::gui::MainWindow::format_status_hierarchy_path(deep_path, 38, 3);
    assert(truncated.startsWith("..."));
    assert(truncated.contains("ArithmeticLogic"));
    assert(truncated.contains("RegisterBank"));
    assert(!truncated.contains("Top")); // Top must be elided

    QString long_name_path = "Top / VeryLongSubsystemNameWithLotsOfLetters / AnotherExcessivelyLongSubmoduleName";
    QString truncated_long = vector::gui::MainWindow::format_status_hierarchy_path(long_name_path, 38, 3);
    assert(truncated_long.startsWith("..."));
    assert(truncated_long.length() <= 38);

    // Step 7: Pop scope back to SubsystemAlpha
    bool ok_pop1 = canvas->pop_hierarchy_scope();
    assert(ok_pop1);
    assert(canvas->graph_model() == sub1_graph);
    assert(canvas->current_breadcrumb_path().find("SubsystemAlpha") != std::string::npos);
    assert(status_lbl->text().contains("SubsystemAlpha"));

    // Step 8: Pop scope back to Top
    bool ok_pop2 = canvas->pop_hierarchy_scope();
    assert(ok_pop2);
    assert(canvas->graph_model() == root_graph);
    assert(canvas->current_breadcrumb_path() == "Top");
    assert(status_lbl->text().contains("Top"));

    // Step 9: Re-drill down into SubsystemAlpha, then into SubmoduleBeta
    bool ok_redrill1 = canvas->drill_down_node(sub1_id);
    assert(ok_redrill1);
    assert(canvas->graph_model() == sub1_graph);

    bool ok_redrill2 = canvas->drill_down_node(sub2_id);
    assert(ok_redrill2);
    assert(canvas->graph_model() == sub2_graph);
    // Verify LeafNode is preserved inside SubmoduleBeta
    assert(sub2_graph->find_node_by_name("LeafNode") != nullptr);

    // Pop back to top
    canvas->pop_hierarchy_scope();
    canvas->pop_hierarchy_scope();
    assert(canvas->graph_model() == root_graph);
    assert(status_lbl->text().contains("Top"));

    (void)status_lbl;
    (void)sub2_graph;
    (void)leaf_id;
    (void)ok_drill1;
    (void)ok_drill2;
    (void)ok_pop1;
    (void)ok_pop2;
    (void)ok_redrill1;
    (void)ok_redrill2;
  }

  // 33. Test Wire Crossing Styles, Multipoint Arrows & Auto-Routing
  {
    std::cout << "===> Testing Wire Crossing Styles, Multipoint Arrows & Auto-Routing..." << std::endl;
    auto* root_graph = canvas.graph_model();
    assert(root_graph != nullptr);

    // 1. Wire Crossing Styles
    assert(canvas.wire_crossing_style() == vector::renderer::WireCrossingStyle::DotPriority);
    canvas.set_wire_crossing_style(vector::renderer::WireCrossingStyle::BridgeLoopPriority);
    assert(canvas.wire_crossing_style() == vector::renderer::WireCrossingStyle::BridgeLoopPriority);
    canvas.set_wire_crossing_style(vector::renderer::WireCrossingStyle::DotPriority);
    assert(canvas.wire_crossing_style() == vector::renderer::WireCrossingStyle::DotPriority);

    // 2. Auto-routing arrow with obstacle avoidance & connect points
    vector::core::Node src_box;
    src_box.name = "RouteSource";
    src_box.bounds = {50.0, 100.0, 100.0, 60.0};
    auto src_id = root_graph->add_node(std::move(src_box));

    vector::core::Node dst_box1;
    dst_box1.name = "RouteDest1";
    dst_box1.bounds = {400.0, 100.0, 100.0, 60.0};
    auto dst1_id = root_graph->add_node(std::move(dst_box1));

    // Obstacle between them
    vector::core::Node obs_box;
    obs_box.name = "ObstacleBlock";
    obs_box.bounds = {200.0, 80.0, 100.0, 100.0};
    auto obs_id = root_graph->add_node(std::move(obs_box));

    // Generate arrow
    auto arrow_id = canvas.auto_route_arrow(src_id, vector::core::InvalidPortId, dst1_id, vector::core::InvalidPortId);
    assert(arrow_id != vector::core::InvalidEdgeId);

    auto* edge = root_graph->find_edge(arrow_id);
    assert(edge != nullptr);
    assert(!edge->arrowhead.empty());
    assert(edge->waypoints.size() >= 2);
    assert(!edge->is_multipoint());

    // 3. Multipoint Arrow: Add destination branches to multiple targets
    vector::core::Node dst_box2;
    dst_box2.name = "RouteDest2";
    dst_box2.bounds = {400.0, 250.0, 100.0, 60.0};
    auto dst2_id = root_graph->add_node(std::move(dst_box2));

    vector::core::Node dst_box3;
    dst_box3.name = "RouteDest3";
    dst_box3.bounds = {400.0, 400.0, 100.0, 60.0};
    auto dst3_id = root_graph->add_node(std::move(dst_box3));

    // Add branch to Dest2
    bool b1 = canvas.add_arrow_branch(arrow_id, dst2_id, vector::core::InvalidPortId);
    assert(b1);
    assert(edge->is_multipoint());
    assert(edge->extra_destinations.size() == 1);
    assert(edge->extra_destinations[0].waypoints.size() >= 2);
    assert(!edge->extra_destinations[0].arrowhead.empty());

    // Add branch to Dest3 via selected arrow
    canvas.select_edge(arrow_id);
    bool b2 = canvas.add_arrow_branch_to_selected(dst3_id, vector::core::InvalidPortId);
    assert(b2);
    assert(edge->extra_destinations.size() == 2);
    assert(edge->extra_destinations[1].waypoints.size() >= 2);
    assert(!edge->extra_destinations[1].arrowhead.empty());

    // 4. Test Multipoint Branch Alignment Specification
    assert(edge->is_branches_aligned());
    assert(canvas.is_edge_branches_aligned(arrow_id));
    const auto& wps0 = edge->extra_destinations[0].waypoints;
    const auto& wps1 = edge->extra_destinations[1].waypoints;
    // Both branch entry lines must start from the exact same common bus spine X coordinate
    assert(wps0.size() >= 2 && wps1.size() >= 2);
    assert(std::abs(wps0[wps0.size() - 2].x - wps1[wps1.size() - 2].x) < 1e-3);
    (void)b1; (void)b2; (void)wps0; (void)wps1;

    // Toggle alignment off (independent taps)
    canvas.set_edge_branches_aligned(arrow_id, false);
    assert(!edge->is_branches_aligned());
    assert(!canvas.is_edge_branches_aligned(arrow_id));

    // Toggle alignment back on via selected edge API
    canvas.set_selected_edge_branches_aligned(true);
    assert(edge->is_branches_aligned());
    assert(canvas.is_edge_branches_aligned(arrow_id));

    // 5. Test ESC Key Returns Tool to Select Mode
    canvas.set_tool_mode(vector::renderer::ToolMode::AddRectangle);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::AddRectangle);
    QKeyEvent esc_rect(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &esc_rect);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::Select);

    canvas.set_tool_mode(vector::renderer::ToolMode::AddConnector);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::AddConnector);
    QKeyEvent esc_conn(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &esc_conn);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::Select);

    canvas.set_tool_mode(vector::renderer::ToolMode::AddPolygon);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::AddPolygon);
    QKeyEvent esc_poly(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &esc_poly);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::Select);

    canvas.set_tool_mode(vector::renderer::ToolMode::AddHierarchicalBlock);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::AddHierarchicalBlock);
    QKeyEvent esc_block(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &esc_block);
    assert(canvas.tool_mode() == vector::renderer::ToolMode::Select);

    // 6. Render canvas with both wire crossing styles (Style 1 and Style 2)
    canvas.set_wire_crossing_style(vector::renderer::WireCrossingStyle::DotPriority);
    canvas.render_frame_for_test();

    canvas.set_wire_crossing_style(vector::renderer::WireCrossingStyle::BridgeLoopPriority);
    canvas.render_frame_for_test();

    (void)obs_id;
  }

  // 34. Test Shapes Dock (Exclusive repository for diagram figures, dynamic Add/Remove)
  //     and Tools Dock (Pure tools palette, compact selector footprint)
  {
    std::cout << "[Test 34] Verifying Shapes Dock and Tools Dock conversion..." << std::endl;
    vector::gui::MainWindow win;
    win.show();

    // A: Test Shapes Dock
    auto* shapes_dock = win.dock_shapes();
    assert(shapes_dock != nullptr);
    assert(shapes_dock->windowTitle() == "Shapes");
    assert(shapes_dock->objectName() == "ShapesDock");

    const int initial_shapes = shapes_dock->total_shape_count();
    assert(initial_shapes >= 25); // Minimum constructive basis set: 13 primitives + 8 N-gons + 5 ports + 2 connectors
    assert(shapes_dock->has_shape("Rectangle Block"));
    assert(shapes_dock->has_shape("Right-Angled Triangle"));
    assert(shapes_dock->has_shape("Isosceles Triangle"));
    assert(shapes_dock->has_shape("State Ellipse & Circle"));
    assert(shapes_dock->has_shape("Rounded Rectangle"));
    assert(shapes_dock->has_shape("Diamond / Rhombus"));
    assert(shapes_dock->has_shape("Parallelogram"));
    assert(shapes_dock->has_shape("Trapezoid"));
    assert(shapes_dock->has_shape("Cross"));
    assert(shapes_dock->has_shape("Star"));
    assert(shapes_dock->has_shape("Cylinder"));
    assert(shapes_dock->has_shape("Arrow Block"));
    assert(shapes_dock->has_shape("Custom Polygon Tool"));
    assert(shapes_dock->has_shape("Pentagon"));
    assert(shapes_dock->has_shape("Hexagon"));
    assert(shapes_dock->has_shape("Heptagon"));
    assert(shapes_dock->has_shape("Octagon"));
    assert(shapes_dock->has_shape("Decagon"));
    assert(shapes_dock->has_shape("Dodecagon"));
    assert(shapes_dock->has_shape("Input Port"));
    assert(shapes_dock->has_shape("Output Port"));
    assert(shapes_dock->has_shape("Bidirectional Port"));
    assert(shapes_dock->has_shape("Subsystem"));
    assert(shapes_dock->has_shape("Connector"));

    // Verify Shapes Tree Icon Size is enlarged for high visibility
    assert(shapes_dock->tree_widget()->iconSize().width() >= 24);
    assert(shapes_dock->tree_widget()->iconSize().height() >= 24);

    // B: Test Adding custom shape
    shapes_dock->add_shape_item("Motor Controller", vector::gui::StencilLibraryDock::StencilItemType::ShapeRect, 0);
    assert(shapes_dock->total_shape_count() == initial_shapes + 1);
    assert(shapes_dock->has_shape("Motor Controller"));

    shapes_dock->add_shape_item("Dodecagon", vector::gui::StencilLibraryDock::StencilItemType::ShapePolygon, 12);
    assert(shapes_dock->total_shape_count() == initial_shapes + 2);
    assert(shapes_dock->has_shape("Dodecagon"));

    // C: Test Adding shape from Canvas selection
    auto* canvas = win.canvas();
    assert(canvas != nullptr);
    const auto center = canvas->visible_world_frustum().center();
    auto test_node_id = canvas->add_shape(vector::core::ShapeType::Rectangle, center, "Sensor Node");
    canvas->select_node(test_node_id);
    assert(canvas->selected_count() == 1);

    shapes_dock->add_from_selection();
    assert(shapes_dock->total_shape_count() == initial_shapes + 3);
    assert(shapes_dock->has_shape("Sensor Node"));

    // D: Test Removing shapes
    bool removed = shapes_dock->remove_shape("Motor Controller");
    assert(removed);
    assert(!shapes_dock->has_shape("Motor Controller"));
    assert(shapes_dock->total_shape_count() == initial_shapes + 2);

    bool removed_dodec = shapes_dock->remove_shape("Dodecagon");
    assert(removed_dodec);
    assert(!shapes_dock->has_shape("Dodecagon"));
    assert(shapes_dock->total_shape_count() == initial_shapes + 1);

    // Test restoring default shapes
    shapes_dock->reset_default_shapes();
    assert(shapes_dock->total_shape_count() == initial_shapes);
    assert(!shapes_dock->has_shape("Sensor Node"));

    // Verify Custom Polygon Shape basis insertion
    auto right_tri_id = canvas->add_custom_polygon_shape(center, {
      {0.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}
    }, "Test Right Tri", 100.0, 80.0);
    assert(right_tri_id != vector::core::InvalidNodeId);
    auto* right_tri_node = canvas->graph_model()->find_node(right_tri_id);
    assert(right_tri_node != nullptr);
    assert(right_tri_node->polygon_vertices.size() == 3);
    (void)right_tri_id; (void)right_tri_node;

    // E: Test Tools Dock
    auto* tools_dock = win.dock_tools();
    assert(tools_dock != nullptr);
    assert(tools_dock->windowTitle() == "Tools");
    assert(tools_dock->objectName() == "DockTools");

    // Verify tools dock contains compact buttons with 28x28 size
    auto tool_buttons = tools_dock->findChildren<QToolButton*>();
    assert(!tool_buttons.isEmpty());
    for (auto* btn : tool_buttons) {
      assert(btn->width() <= 32); // Compact sizing (28px fixed)
      assert(btn->height() <= 32);
      (void)btn;
    }

    // F: Test Compact Selector in Tools Dock and Tool Mode synchronization
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    QToolButton* select_btn = nullptr;
    QToolButton* wire_btn = nullptr;
    for (auto* btn : tool_buttons) {
      if (btn->toolTip().contains("Select & Transform")) {
        select_btn = btn;
      } else if (btn->toolTip().contains("Orthogonal Net Connector")) {
        wire_btn = btn;
      }
    }
    assert(select_btn != nullptr);
    assert(wire_btn != nullptr);
    assert(select_btn->isCheckable());
    assert(select_btn->isChecked());
    assert(!wire_btn->isChecked());

    // Switch tool to Connector mode
    canvas->set_tool_mode(vector::renderer::ToolMode::AddConnector);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddConnector);
    assert(wire_btn->isChecked());
    assert(!select_btn->isChecked());

    // Hit ESC key -> must return to Select mode and update selector button state!
    QKeyEvent esc_key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &esc_key);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(select_btn->isChecked());
    assert(!wire_btn->isChecked());

    // G: Verify Compact Selector UI metrics in Shapes Dock
    assert(shapes_dock->search_edit()->height() <= 26);
    assert(shapes_dock->add_button()->height() <= 26);
    assert(shapes_dock->remove_button()->height() <= 26);
    assert(shapes_dock->insert_button()->height() <= 26);
    (void)initial_shapes; (void)removed; (void)removed_dodec; (void)select_btn; (void)wire_btn;

    std::cout << "[Test 34] Shapes Dock and Tools Dock passed verification successfully!" << std::endl;
  }

  // =========================================================================
  // Test 35: Figure Interactive Rotation, Angle Snapping & Rotated Port Tracking
  // =========================================================================
  {
    std::cout << "\n[Test 35] Running Figure Interactive Rotation & Angle Snapping Tests..." << std::endl;
    vector::gui::MainWindow win;
    win.resize(1024, 768);
    win.show();
    QApplication::processEvents();

    auto* canvas = win.canvas();
    auto* model = canvas->graph_model();
    assert(canvas != nullptr && model != nullptr);

    // Clear existing nodes for a clean slate
    while (model->node_count() > 0) {
      const auto nid = model->nodes().begin()->first;
      model->remove_node(nid);
    }
    canvas->refresh_spatial_index();

    // 1. Add a rectangular figure at (300, 200) with size 100 x 60
    vector::core::Node rot_node;
    rot_node.id = 5001;
    rot_node.name = "Rotatable Block";
    rot_node.bounds = {300.0, 200.0, 100.0, 60.0};
    rot_node.rotation_deg = 0.0;
    rot_node.shape = vector::core::ShapeType::Rectangle;

    // Add an East port (anchor = 1.0, 0.5)
    vector::core::Port east_port;
    east_port.id = 6001;
    east_port.parent_node = 5001;
    east_port.name = "E_OUT";
    east_port.local_anchor = {1.0, 0.5};
    east_port.direction = vector::core::PortDirection::East;
    rot_node.port_ids.push_back(6001);

    const auto added_nid = model->add_node(std::move(rot_node));
    model->add_port(std::move(east_port));
    canvas->refresh_spatial_index();
    canvas->select_node(added_nid, true);
    QApplication::processEvents();

    auto* n = model->find_node(added_nid);
    assert(n != nullptr);
    assert(std::abs(n->rotation_deg) < 1e-4);
    assert(std::abs(canvas->get_node_rotation(added_nid)) < 1e-4);

    // Verify Inspector has updated to 0.0°
    auto* insp_rot = win.inspector_rotation();
    assert(insp_rot != nullptr);
    assert(std::abs(insp_rot->value() - 0.0) < 1e-2);

    // 2. Test GridManager Angle Snapping Functionality
    const auto* grid_mgr = canvas->grid_manager();
    assert(grid_mgr != nullptr);
    (void)grid_mgr;

    // Snapping to 15-degree steps
    assert(std::abs(grid_mgr->snap_angle(14.2, 15.0) - 15.0) < 1e-3);
    assert(std::abs(grid_mgr->snap_angle(31.0, 15.0) - 30.0) < 1e-3);
    assert(std::abs(grid_mgr->snap_angle(44.8, 15.0) - 45.0) < 1e-3);

    // Priority Magnetic Lock on Cardinals (0°, 90°, 180°, 270°, 360°)
    assert(std::abs(grid_mgr->snap_angle(88.2, 15.0, 4.0) - 90.0) < 1e-3);
    assert(std::abs(grid_mgr->snap_angle(92.4, 15.0, 4.0) - 90.0) < 1e-3);
    assert(std::abs(grid_mgr->snap_angle(181.5, 15.0, 4.0) - 180.0) < 1e-3);
    assert(std::abs(grid_mgr->snap_angle(268.8, 15.0, 4.0) - 270.0) < 1e-3);
    assert(std::abs(grid_mgr->snap_angle(358.5, 15.0, 4.0) - 0.0) < 1e-3);
    assert(std::abs(grid_mgr->snap_angle(1.8, 15.0, 4.0) - 0.0) < 1e-3);

    // 3. Test Programmatic Rotation Actions
    // Rotate +45 degrees
    canvas->rotate_selected(45.0);
    assert(std::abs(n->rotation_deg - 45.0) < 1e-3);
    assert(std::abs(canvas->get_node_rotation(added_nid) - 45.0) < 1e-3);

    // Rotate +90 degrees (45 -> 135)
    canvas->rotate_selected(90.0);
    assert(std::abs(n->rotation_deg - 135.0) < 1e-3);

    // Rotate -90 degrees CCW (135 -> 45)
    canvas->rotate_selected(-90.0);
    assert(std::abs(n->rotation_deg - 45.0) < 1e-3);

    // Set absolute rotation to 90.0 degrees
    canvas->set_selected_nodes_rotation(90.0);
    assert(std::abs(n->rotation_deg - 90.0) < 1e-3);

    // 4. Test Rotated Port Position Tracking
    // Center is (350, 230). Unrotated East port was at (400, 230).
    // When rotated 90° CW, the East port rotates around (350, 230) by 90° CW to (350, 280) (South)!
    const auto* p_port = model->find_port(6001);
    assert(p_port != nullptr);
    vector::core::Vec2d p_pos_90 = n->resolve_port_position(*p_port);
    assert(std::abs(p_pos_90.x - 350.0) < 1e-3);
    assert(std::abs(p_pos_90.y - 280.0) < 1e-3);

    // Rotate to 180° -> East port rotates to (300, 230) (West)!
    canvas->set_node_rotation(added_nid, 180.0);
    vector::core::Vec2d p_pos_180 = n->resolve_port_position(*p_port);
    assert(std::abs(p_pos_180.x - 300.0) < 1e-3);
    assert(std::abs(p_pos_180.y - 230.0) < 1e-3);

    // Rotate to 270° -> East port rotates to (350, 180) (North)!
    canvas->set_node_rotation(added_nid, 270.0);
    vector::core::Vec2d p_pos_270 = n->resolve_port_position(*p_port);
    assert(std::abs(p_pos_270.x - 350.0) < 1e-3);
    assert(std::abs(p_pos_270.y - 180.0) < 1e-3);

    // Reset rotation to 0° -> port back at (400, 230)
    canvas->set_selected_nodes_rotation(0.0);
    vector::core::Vec2d p_pos_0 = n->resolve_port_position(*p_port);
    assert(std::abs(p_pos_0.x - 400.0) < 1e-3);
    assert(std::abs(p_pos_0.y - 230.0) < 1e-3);

    // 5. Test Rotated Figure Hit-Testing & Bounding Box
    // Rotate by 90°
    canvas->set_node_rotation(added_nid, 90.0);
    const auto aabb_90 = n->bounding_box();
    // 100x60 rectangle rotated 90° has extents 60x100 centered at (350, 230)
    assert(std::abs(aabb_90.width - 60.0) < 1e-3);
    assert(std::abs(aabb_90.height - 100.0) < 1e-3);
    assert(std::abs(aabb_90.x - 320.0) < 1e-3);
    assert(std::abs(aabb_90.y - 180.0) < 1e-3);

    // Center point (350, 230) is inside
    assert(n->contains_point({350.0, 230.0}));
    // Point (350, 275) is inside South tail of rotated shape
    assert(n->contains_point({350.0, 275.0}));
    // Point (305, 230) was inside unrotated shape, but outside the 60px wide rotated shape
    assert(!n->contains_point({305.0, 230.0}));

    // 6. Test Interactive Drag on Rotation Lollipop Handle
    // Reset to 0°
    canvas->set_node_rotation(added_nid, 0.0);
    // Node bounds: (300, 200, 100, 60), center (350, 230). TopCenter is (350, 200).
    // Rotation stalk is at (350, 200 - 22) = (350, 178)
    const auto stalk_scr = canvas->world_to_screen({350.0, 178.0});

    // Press on rotation stalk
    const QPointF p_stalk(stalk_scr.x, stalk_scr.y);
    QMouseEvent press_stalk(QEvent::MouseButtonPress, p_stalk, p_stalk,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press_stalk);

    // Drag to the right: point (350 + 120, 230) in world coordinates -> screen coordinates
    const auto right_scr = canvas->world_to_screen({470.0, 230.0});
    const QPointF p_right(right_scr.x, right_scr.y);
    QMouseEvent drag_move(QEvent::MouseMove, p_right, p_right,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &drag_move);

    // With angle snapping enabled, moving horizontally right from center snaps exactly to 90.0° cardinal
    assert(std::abs(n->rotation_deg - 90.0) < 1e-3);

    // Release mouse
    QMouseEvent release_mouse(QEvent::MouseButtonRelease, p_right, p_right,
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release_mouse);
    assert(std::abs(n->rotation_deg - 90.0) < 1e-3);

    // 7. Test Undo / Redo for Rotation
    auto* cmd_history = canvas->command_history();
    assert(cmd_history != nullptr);
    assert(cmd_history->can_undo());

    // Undo rotation -> returns to 0.0°
    cmd_history->undo();
    assert(std::abs(n->rotation_deg - 0.0) < 1e-3);

    // Redo rotation -> returns to 90.0°
    cmd_history->redo();
    assert(std::abs(n->rotation_deg - 90.0) < 1e-3);

    // 8. Test Inspector Spinbox Interaction
    win.update_inspector();
    assert(std::abs(insp_rot->value() - 180.0) < 1e-2);

    // Set value via spinbox to 180.0°
    insp_rot->setValue(180.0);
    // Trigger editingFinished
    emit insp_rot->editingFinished();
    assert(std::abs(n->rotation_deg - 180.0) < 1e-3);

    // 9. Test Angle Snap Buttons Across All Surfaces (Status Bar, Toolbar, DockTools, Inspector)
    auto* btn_status = win.btn_angle_snap_status();
    auto* act_snap = win.action_toggle_angle_snap();
    auto* btn_dock_snap = win.btn_dock_angle_snap();
    auto* btn_insp_snap = win.btn_insp_angle_snap();
    auto* btn_tb_snap = win.btn_tb_angle_snap();
    assert(btn_status != nullptr);
    assert(act_snap != nullptr);
    assert(btn_dock_snap != nullptr);
    assert(btn_insp_snap != nullptr);
    assert(btn_tb_snap != nullptr);
    assert(btn_tb_snap->menu() != nullptr);
    (void)btn_tb_snap;

    // Initial state: Angle Snap is enabled with 15° step
    assert(canvas->is_angle_snapping_enabled());
    assert(btn_status->text() == "∠ SNAP: 15°");
    assert(act_snap->isChecked());
    assert(btn_dock_snap->isChecked());
    assert(btn_insp_snap->isChecked());

    // Click Status Bar button -> toggles Angle Snap OFF
    btn_status->click();
    QApplication::processEvents();
    assert(!canvas->is_angle_snapping_enabled());
    assert(btn_status->text() == "∠ SNAP: OFF");
    assert(!act_snap->isChecked());
    assert(!btn_dock_snap->isChecked());
    assert(!btn_insp_snap->isChecked());

    // Click DockTools Angle Snap button -> toggles Angle Snap back ON
    btn_dock_snap->click();
    QApplication::processEvents();
    assert(canvas->is_angle_snapping_enabled());
    assert(btn_status->text() == "∠ SNAP: 15°");
    assert(act_snap->isChecked());
    assert(btn_dock_snap->isChecked());
    assert(btn_insp_snap->isChecked());

    // Toggle via Action -> toggles OFF
    act_snap->toggle();
    QApplication::processEvents();
    assert(!canvas->is_angle_snapping_enabled());
    assert(btn_status->text() == "∠ SNAP: OFF");

    // Click Inspector Angle Snap button -> toggles back ON
    btn_insp_snap->click();
    QApplication::processEvents();
    assert(canvas->is_angle_snapping_enabled());
    assert(btn_status->text() == "∠ SNAP: 15°");

    // Test Quick Rotate Buttons on Dock and Inspector
    auto* btn_dock_cw = win.btn_dock_rot_cw();
    auto* btn_dock_ccw = win.btn_dock_rot_ccw();
    auto* btn_insp_cw = win.btn_insp_rot_cw();
    auto* btn_insp_ccw = win.btn_insp_rot_ccw();
    assert(btn_dock_cw != nullptr && btn_dock_ccw != nullptr);
    assert(btn_insp_cw != nullptr && btn_insp_ccw != nullptr);

    // Node is currently at 180.0°
    btn_dock_cw->click(); // 180 -> 270
    assert(std::abs(n->rotation_deg - 270.0) < 1e-3);
    btn_dock_ccw->click(); // 270 -> 180
    assert(std::abs(n->rotation_deg - 180.0) < 1e-3);

    btn_insp_cw->click(); // 180 -> 270
    assert(std::abs(n->rotation_deg - 270.0) < 1e-3);
    btn_insp_ccw->click(); // 270 -> 180
    assert(std::abs(n->rotation_deg - 180.0) < 1e-3);

    // Test Angle Step Change reflects across status label
    canvas->set_angle_snap_step(45.0);
    QApplication::processEvents();
    assert(btn_status->text() == "∠ SNAP: 45°");

    canvas->set_angle_snap_step(30.0);
    QApplication::processEvents();
    assert(btn_status->text() == "∠ SNAP: 30°");

    canvas->set_angle_snap_step(15.0);
    QApplication::processEvents();
    assert(btn_status->text() == "∠ SNAP: 15°");

    // Verify render frame with rotated figures does not throw
    canvas->render_frame_for_test();

    (void)p_pos_90; (void)p_pos_180; (void)p_pos_270; (void)p_pos_0; (void)aabb_90;
    std::cout << "[Test 35] Figure Interactive Rotation & Angle Snapping passed successfully!" << std::endl;
  }

  // =========================================================================
  // Test 36: Keyboard Shortcuts & Help Menu Documentation Verification
  // =========================================================================
  {
    std::cout << "\n[Test 36] Running Keyboard Shortcuts & Documentation Verification..." << std::endl;
    vector::gui::MainWindow win;
    win.resize(1024, 768);
    win.show();
    QApplication::processEvents();

    auto* canvas = win.canvas();
    assert(canvas != nullptr);

    // 1. Test Canvas Tool Shortcuts (Ctrl+1, Ctrl+3, Ctrl+8, Ctrl+9)
    // Ctrl+3 -> Connector / Wire Tool
    QKeyEvent key_ctrl3(QEvent::KeyPress, Qt::Key_3, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &key_ctrl3);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddConnector);

    // Ctrl+8 -> Rectangle Block
    QKeyEvent key_ctrl8(QEvent::KeyPress, Qt::Key_8, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &key_ctrl8);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddRectangle);

    // Ctrl+9 -> State Ellipse
    QKeyEvent key_ctrl9(QEvent::KeyPress, Qt::Key_9, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &key_ctrl9);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddEllipse);

    // Ctrl+1 -> Pointer / Select Tool
    QKeyEvent key_ctrl1(QEvent::KeyPress, Qt::Key_1, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &key_ctrl1);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);

    // Escape returns to Select mode
    QKeyEvent key_w(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    QApplication::sendEvent(canvas, &key_w);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddConnector);

    QKeyEvent key_esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &key_esc);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);

    // 2. Test Grid & Snap Shortcuts (Shift+F9, F9, Alt+A)
    auto* grid_mgr = canvas->grid_manager();
    assert(grid_mgr != nullptr);

    const bool initial_grid_vis = grid_mgr->config().visible;
    (void)initial_grid_vis;
    QKeyEvent key_shift_f9(QEvent::KeyPress, Qt::Key_F9, Qt::ShiftModifier);
    QApplication::sendEvent(canvas, &key_shift_f9);
    assert(grid_mgr->config().visible != initial_grid_vis);
    // Toggle back
    QApplication::sendEvent(canvas, &key_shift_f9);
    assert(grid_mgr->config().visible == initial_grid_vis);

    const bool initial_snap = grid_mgr->is_snap_enabled();
    (void)initial_snap;
    QKeyEvent key_f9(QEvent::KeyPress, Qt::Key_F9, Qt::NoModifier);
    QApplication::sendEvent(canvas, &key_f9);
    assert(grid_mgr->is_snap_enabled() != initial_snap);
    QApplication::sendEvent(canvas, &key_f9);
    assert(grid_mgr->is_snap_enabled() == initial_snap);

    const bool initial_angle_snap = canvas->is_angle_snapping_enabled();
    (void)initial_angle_snap;
    QKeyEvent key_alt_a(QEvent::KeyPress, Qt::Key_A, Qt::AltModifier);
    QApplication::sendEvent(canvas, &key_alt_a);
    assert(canvas->is_angle_snapping_enabled() != initial_angle_snap);
    QApplication::sendEvent(canvas, &key_alt_a);
    assert(canvas->is_angle_snapping_enabled() == initial_angle_snap);

    // 3. Test Rotation Shortcuts (Ctrl+R, Ctrl+Shift+R, Ctrl+L)
    auto* model = canvas->graph_model();
    assert(model != nullptr);
    vector::core::Node test_node;
    test_node.id = 7001;
    test_node.name = "Shortcut Test Block";
    test_node.bounds = {200.0, 200.0, 100.0, 50.0};
    test_node.rotation_deg = 0.0;
    const auto nid = model->add_node(std::move(test_node));
    canvas->select_node(nid, true);

    auto* n = model->find_node(nid);
    assert(n != nullptr);
    assert(std::abs(n->rotation_deg) < 1e-3);

    // Ctrl+R -> Rotate 90° CW
    QKeyEvent key_ctrl_r(QEvent::KeyPress, Qt::Key_R, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &key_ctrl_r);
    assert(std::abs(n->rotation_deg - 90.0) < 1e-3);

    // Ctrl+Shift+R -> Rotate 90° CCW (back to 0°)
    QKeyEvent key_ctrl_shift_r(QEvent::KeyPress, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::sendEvent(canvas, &key_ctrl_shift_r);
    assert(std::abs(n->rotation_deg - 0.0) < 1e-3);

    // Ctrl+L -> Rotate 90° CCW (Left Rotate)
    QKeyEvent key_ctrl_l(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &key_ctrl_l);
    assert(std::abs(n->rotation_deg - 270.0) < 1e-3);

    // 4. Test Arrow Key Nudge (1px normal, 10px with Shift)
    const double initial_x = n->bounds.x;
    (void)initial_x;
    QKeyEvent key_right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(canvas, &key_right);
    assert(std::abs(n->bounds.x - (initial_x + 1.0)) < 1e-3);

    QKeyEvent key_shift_right(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QApplication::sendEvent(canvas, &key_shift_right);
    assert(std::abs(n->bounds.x - (initial_x + 11.0)) < 1e-3);

    // 5. Verify Help Menu & Documentation Dialog
    auto* act_help = win.action_keyboard_shortcuts();
    (void)act_help;
    assert(act_help != nullptr);
    assert(act_help->shortcuts().contains(QKeySequence(Qt::Key_F1)));

    // Verify KeyboardShortcutsDialog instantiate and search filtering
    vector::gui::KeyboardShortcutsDialog dlg(&win);
    assert(dlg.windowTitle().contains("Shortcuts"));

    // Verify search filtering works without throwing
    dlg.filter_table("select");
    dlg.filter_table("rotate");
    dlg.filter_table("wire");
    dlg.filter_table(""); // Reset filter

    // Test markdown export to clipboard
    dlg.copy_to_clipboard();
    const QString clip_text = QGuiApplication::clipboard()->text();
    assert(clip_text.contains("VECTOR Keyboard Shortcuts"));
    assert(clip_text.contains("Ctrl+1"));
    assert(clip_text.contains("Ctrl+3"));
    assert(clip_text.contains("Shift+F9"));

    std::cout << "[Test 36] Keyboard Shortcuts & Documentation Verification passed successfully!" << std::endl;
  }

  // =========================================================================
  // 37. Test Unified Mouse Pointer Shape Placement & Drag-and-Drop
  // =========================================================================
  {
    std::cout << "[Test 37] Testing Unified Mouse Pointer Placement and Drag-and-Drop..." << std::endl;
    vector::gui::MainWindow win;
    win.show();
    auto* canvas = win.canvas();
    auto* shapes_dock = win.dock_shapes();
    assert(canvas != nullptr);
    assert(shapes_dock != nullptr);

    auto* tree = shapes_dock->tree_widget();
    assert(tree != nullptr);

    // 1. Verify Double-Clicking Stencil Shapes Attaches Candidate to Mouse Pointer
    auto find_item = [&](const QString& text) -> QTreeWidgetItem* {
      for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* cat = tree->topLevelItem(i);
        for (int j = 0; j < cat->childCount(); ++j) {
          if (cat->child(j)->text(0).contains(text, Qt::CaseInsensitive)) {
            return cat->child(j);
          }
        }
      }
      return nullptr;
    };

    // A: Test Right-Angled Triangle
    auto* right_tri_item = find_item("Right-Angled Triangle");
    assert(right_tri_item != nullptr);
    const size_t initial_nodes = canvas->graph_model()->node_count();

    // Double-click triggers item
    tree->itemDoubleClicked(right_tri_item, 0);
    assert(canvas->is_placement_tool_active());
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddCustomShape);
    assert(canvas->active_placement_template().category == vector::renderer::PlacementCategory::CustomPolygon);
    assert(canvas->active_placement_template().name == "Right Triangle");
    assert(canvas->placement_preview_bounds().is_valid());
    assert(canvas->placement_preview_bounds().width > 0.0);
    // Node is NOT placed yet (remains attached to pointer)
    assert(canvas->graph_model()->node_count() == initial_nodes);

    // Move pointer and simulate left-click on canvas to place shape
    const QPoint click_pos(250, 200);
    QMouseEvent press_event(QEvent::MouseButtonPress, click_pos, click_pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press_event);

    // Shape is now instantiated on canvas, and tool mode reverts to Select
    assert(!canvas->is_placement_tool_active());
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(canvas->graph_model()->node_count() == initial_nodes + 1);

    // Verify placed node properties
    const auto& last_placed_id = canvas->selected_nodes().front();
    const auto* placed_node = canvas->graph_model()->find_node(last_placed_id);
    assert(placed_node != nullptr);
    assert(placed_node->name == "Right Triangle");
    assert(placed_node->shape == vector::core::ShapeType::CustomPolygon);
    assert(placed_node->polygon_vertices.size() == 3);
    (void)initial_nodes; (void)placed_node;

    // B: Test Star
    auto* star_item = find_item("5-Pointed Star");
    assert(star_item != nullptr);
    tree->itemDoubleClicked(star_item, 0);
    assert(canvas->is_placement_tool_active());
    assert(canvas->tool_mode() == vector::renderer::ToolMode::AddCustomShape);
    assert(canvas->active_placement_template().name == "Star");

    // Click canvas to place Star
    const QPoint star_click(400, 300);
    QMouseEvent star_press(QEvent::MouseButtonPress, star_click, star_click, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &star_press);
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);
    assert(canvas->graph_model()->node_count() == initial_nodes + 2);
    const auto* star_node = canvas->graph_model()->find_node(canvas->selected_nodes().front());
    assert(star_node != nullptr);
    assert(star_node->name == "Star");
    assert(star_node->polygon_vertices.size() == 10);
    (void)star_node;

    // C: Test Regular Polygon (Hexagon)
    auto* hex_item = find_item("Hexagon (6-gon)");
    assert(hex_item != nullptr);
    tree->itemDoubleClicked(hex_item, 0);
    assert(canvas->is_placement_tool_active());
    assert(canvas->active_placement_template().category == vector::renderer::PlacementCategory::RegularPolygon);
    assert(canvas->active_placement_template().polygon_sides == 6);

    // Cancel placement with Escape key
    QKeyEvent esc_key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &esc_key);
    assert(!canvas->is_placement_tool_active());
    assert(canvas->tool_mode() == vector::renderer::ToolMode::Select);

    // D: Test Hierarchical Ports
    auto* port_item = find_item("Hierarchical Input Port");
    assert(port_item != nullptr);
    tree->itemDoubleClicked(port_item, 0);
    assert(canvas->is_placement_tool_active());
    assert(canvas->active_placement_template().category == vector::renderer::PlacementCategory::HierarchicalPort);
    assert(canvas->active_placement_template().is_input_port == true);

    // Click canvas to place port
    const QPoint port_click(150, 150);
    QMouseEvent port_press(QEvent::MouseButtonPress, port_click, port_click, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &port_press);
    assert(!canvas->is_placement_tool_active());
    assert(canvas->graph_model()->node_count() == initial_nodes + 3);

    // 2. Verify Drag-and-Drop from Palette directly onto Canvas
    const size_t pre_drag_count = canvas->graph_model()->node_count();

    // A: Drag a Diamond onto the canvas
    auto* diamond_item = find_item("Diamond / Rhombus");
    assert(diamond_item != nullptr);
    const int d_type = diamond_item->data(0, Qt::UserRole).toInt();
    const int d_param = diamond_item->data(0, Qt::UserRole + 1).toInt();
    const QString d_custom = diamond_item->data(0, Qt::UserRole + 2).toString();

    auto d_tmpl = vector::gui::StencilLibraryDock::template_from_stencil_data(d_type, d_param, d_custom);
    QByteArray d_bytes = vector::renderer::serialize_placement_template(d_tmpl);

    QMimeData* mime_data = new QMimeData();
    mime_data->setData("application/x-vector-stencil", d_bytes);

    const QPoint drop_pt(320, 240);

    // Simulate drag enter
    QDragEnterEvent enter_ev(drop_pt, Qt::CopyAction, mime_data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &enter_ev);
    assert(enter_ev.isAccepted());
    assert(canvas->placement_preview_bounds().is_valid());

    // Simulate drag move
    QDragMoveEvent move_ev(drop_pt, Qt::CopyAction, mime_data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move_ev);
    assert(move_ev.isAccepted());

    // Simulate drop
    QDropEvent drop_ev(drop_pt, Qt::CopyAction, mime_data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &drop_ev);
    assert(drop_ev.isAccepted());
    assert(canvas->graph_model()->node_count() == pre_drag_count + 1);

    const auto* dropped_diamond = canvas->graph_model()->find_node(canvas->selected_nodes().front());
    assert(dropped_diamond != nullptr);
    assert(dropped_diamond->name == "Diamond");
    assert(dropped_diamond->polygon_vertices.size() == 4);
    (void)pre_drag_count; (void)dropped_diamond;

    // B: Test drag-and-drop of Cross
    auto* cross_item = find_item("Orthogonal Cross");
    assert(cross_item != nullptr);
    const int c_type = cross_item->data(0, Qt::UserRole).toInt();
    const int c_param = cross_item->data(0, Qt::UserRole + 1).toInt();
    auto c_tmpl = vector::gui::StencilLibraryDock::template_from_stencil_data(c_type, c_param, "");
    QByteArray c_bytes = vector::renderer::serialize_placement_template(c_tmpl);

    QMimeData* cross_mime = new QMimeData();
    cross_mime->setData("application/x-vector-stencil", c_bytes);

    QDropEvent cross_drop(QPoint(500, 350), Qt::CopyAction, cross_mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &cross_drop);
    assert(canvas->graph_model()->node_count() == pre_drag_count + 2);

    const auto* dropped_cross = canvas->graph_model()->find_node(canvas->selected_nodes().front());
    assert(dropped_cross != nullptr);
    assert(dropped_cross->name == "Cross");
    assert(dropped_cross->polygon_vertices.size() == 12);
    (void)dropped_cross;

    std::cout << "[Test 37] Unified Mouse Pointer Placement and Drag-and-Drop passed successfully!" << std::endl;
  }

  // 38. Test Symbol Instantiation Anchor & Drag-to-Resize Control Point
  {
    std::cout << "===> Testing Symbol Instantiation Anchor & Drag-to-Resize Control Point..." << std::endl;
    vector::gui::MainWindow hwin;
    hwin.resize(1024, 768);
    auto* canvas = hwin.canvas();
    assert(canvas != nullptr);
    auto* graph = canvas->graph_model();
    assert(graph != nullptr);
    graph->clear();

    // A: Test Node defaults and property persistence
    vector::core::Node test_node;
    assert(test_node.symbol_anchor().x == 0.0 && test_node.symbol_anchor().y == 0.0);
    assert(test_node.symbol_resize_point().x == 1.0 && test_node.symbol_resize_point().y == 1.0);

    test_node.set_symbol_anchor({0.0, 0.5});
    test_node.set_symbol_resize_point({1.0, 1.0});
    assert(test_node.symbol_anchor().x == 0.0 && test_node.symbol_anchor().y == 0.5);
    assert(test_node.symbol_resize_point().x == 1.0 && test_node.symbol_resize_point().y == 1.0);

    // B: Test SymbolEditorDialog anchor and resize configuration
    const auto nid = graph->add_node(test_node);
    auto* placed_node = graph->find_node(nid);
    assert(placed_node != nullptr);

    {
      vector::gui::SymbolEditorDialog dlg(*placed_node, *graph, nullptr, &hwin);
      dlg.refresh_preview();
      dlg.apply_and_accept();
    }
    assert(placed_node->symbol_anchor().x == 0.0 && placed_node->symbol_anchor().y == 0.5);

    // C: Test Interactive Drag-to-Resize Placement
    vector::renderer::ShapePlacementTemplate tmpl;
    tmpl.category = vector::renderer::PlacementCategory::ShapeType;
    tmpl.shape_type = vector::core::ShapeType::Rectangle;
    tmpl.name = "ResizedSymbol";
    tmpl.width = 100.0;
    tmpl.height = 50.0;
    tmpl.anchor_point = {0.0, 0.0};
    tmpl.resize_point = {1.0, 1.0};

    canvas->start_shape_placement(tmpl);
    assert(canvas->is_placement_tool_active());

    // 1. Move to (100, 100)
    const auto s_anchor = canvas->world_to_screen({100.0, 100.0});
    const QPointF q_anchor(s_anchor.x, s_anchor.y);
    QMouseEvent move1(QEvent::MouseMove, q_anchor, q_anchor, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move1);

    // 2. Press at (100, 100) (pins anchor)
    QMouseEvent press1(QEvent::MouseButtonPress, q_anchor, q_anchor, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press1);

    // 3. Drag to (300, 250) (stretch width = 200, height = 150)
    const auto s_resize = canvas->world_to_screen({300.0, 250.0});
    const QPointF q_resize(s_resize.x, s_resize.y);
    QMouseEvent drag1(QEvent::MouseMove, q_resize, q_resize, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &drag1);

    const auto& prev_bounds = canvas->placement_preview_bounds();
    assert(prev_bounds.is_valid());
    assert(std::abs(prev_bounds.width - 200.0) < 1.0);
    assert(std::abs(prev_bounds.height - 150.0) < 1.0);
    (void)prev_bounds;

    // 4. Release at (300, 250) (finalizes instantiation)
    QMouseEvent rel1(QEvent::MouseButtonRelease, q_resize, q_resize, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &rel1);

    assert(!canvas->is_placement_tool_active());
    assert(graph->node_count() == 2);

    const auto* new_node = graph->find_node(canvas->selected_nodes().front());
    assert(new_node != nullptr);
    assert(std::abs(new_node->bounds.width - 200.0) < 1.0);
    assert(std::abs(new_node->bounds.height - 150.0) < 1.0);
    assert(new_node->symbol_anchor().x == 0.0 && new_node->symbol_anchor().y == 0.0);
    assert(new_node->symbol_resize_point().x == 1.0 && new_node->symbol_resize_point().y == 1.0);
    (void)new_node;

    // D: Test Shift-Constrained Aspect Ratio Locking
    tmpl.name = "RatioLockedSymbol";
    tmpl.width = 100.0;
    tmpl.height = 50.0; // 2:1 aspect ratio
    canvas->start_shape_placement(tmpl);

    QMouseEvent press2(QEvent::MouseButtonPress, q_anchor, q_anchor, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press2);

    // Drag to (300, 250) with Shift (locked to 2:1 ratio -> width 200, height 100)
    QMouseEvent drag2(QEvent::MouseMove, q_resize, q_resize, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(canvas, &drag2);

    const auto& ratio_prev = canvas->placement_preview_bounds();
    assert(std::abs(ratio_prev.width / ratio_prev.height - 2.0) < 1e-3);
    (void)ratio_prev;

    QMouseEvent rel2(QEvent::MouseButtonRelease, q_resize, q_resize, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QApplication::sendEvent(canvas, &rel2);

    const auto* locked_node = graph->find_node(canvas->selected_nodes().front());
    assert(locked_node != nullptr);
    assert(std::abs(locked_node->bounds.width / locked_node->bounds.height - 2.0) < 1e-3);
    (void)locked_node;

    // E: Test 2-Click Mode (Click anchor, move cursor, click to place)
    tmpl.name = "TwoClickSymbol";
    canvas->start_shape_placement(tmpl);

    // Click 1 (press and release < 8px)
    QMouseEvent c1_press(QEvent::MouseButtonPress, q_anchor, q_anchor, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &c1_press);
    QMouseEvent c1_rel(QEvent::MouseButtonRelease, q_anchor, q_anchor, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &c1_rel);

    // Canvas stays in placement mode!
    assert(canvas->is_placement_tool_active());

    // Move to (400, 300)
    const auto s_dest = canvas->world_to_screen({400.0, 300.0});
    const QPointF q_dest(s_dest.x, s_dest.y);
    QMouseEvent c_move(QEvent::MouseMove, q_dest, q_dest, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &c_move);

    // Click 2 to finalize
    QMouseEvent c2_press(QEvent::MouseButtonPress, q_dest, q_dest, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &c2_press);

    assert(!canvas->is_placement_tool_active());
    const auto* two_click_node = graph->find_node(canvas->selected_nodes().front());
    assert(two_click_node != nullptr);
    assert(std::abs(two_click_node->bounds.width - 300.0) < 1.0);
    assert(std::abs(two_click_node->bounds.height - 200.0) < 1.0);
    (void)two_click_node;

    std::cout << "[Test 38] Symbol Instantiation Anchor & Drag-to-Resize Control Point passed successfully!" << std::endl;
  }

  // 39. Test Smart Parametric Shape Handles (Arrow Points & Attached Lines Modification)
  {
    std::cout << "===> Testing Smart Parametric Shape Handles (Arrow Points & Attached Lines)..." << std::endl;
    vector::gui::MainWindow hwin;
    hwin.resize(1024, 768);
    auto* canvas = hwin.canvas();
    assert(canvas != nullptr);
    auto* graph = canvas->graph_model();
    assert(graph != nullptr);
    graph->clear();

    // 1. Create Arrow Block shape
    vector::core::Node arrow_node;
    arrow_node.name = "Arrow Block";
    arrow_node.shape = vector::core::ShapeType::CustomPolygon;
    arrow_node.bounds = {100.0, 100.0, 200.0, 100.0};
    arrow_node.polygon_vertices = {
      {0.0, 0.2}, {0.6, 0.2}, {0.6, 0.0},
      {1.0, 0.5}, {0.6, 1.0}, {0.6, 0.8}, {0.0, 0.8}
    };
    arrow_node.properties["parametric_type"] = "arrow";
    const auto nid = graph->add_node(std::move(arrow_node));
    assert(nid != vector::core::InvalidNodeId);

    // 2. Select the arrow
    canvas->select_node(nid, false);
    assert(canvas->selected_nodes().size() == 1);

    // 3. Verify parametric kind and handles
    const auto* node = graph->find_node(nid);
    assert(node != nullptr);
    assert(vector::core::get_parametric_kind(*node) == vector::core::ParametricShapeKind::Arrow);
    const auto handles = vector::core::get_parametric_handles(*node);
    assert(handles.size() == 5);
    assert(handles[0].normalized_pos.x == 0.6 && handles[0].normalized_pos.y == 0.2); // Top shoulder
    assert(handles[1].normalized_pos.x == 0.6 && handles[1].normalized_pos.y == 0.8); // Bottom shoulder

    // 4. Hit test top shoulder point handle
    // World point of handle 0: (100 + 0.6 * 200, 100 + 0.2 * 100) = (220, 120)
    const auto s_h0 = canvas->world_to_screen({220.0, 120.0});
    vector::core::NodeId hit_node = vector::core::InvalidNodeId;
    const auto ht0 = canvas->hit_test_handles(s_h0, &hit_node);
    assert(ht0 == vector::renderer::HandleType::ParametricPoint0);
    assert(hit_node == nid);
    (void)ht0;

    // 5. Hit test shoulder line (e.g. at world 220, 150)
    // Edge segment moving is disabled by default to prevent accidental segment dragging
    assert(!canvas->is_segment_moving_enabled());
    const auto s_line_sh = canvas->world_to_screen({220.0, 150.0});
    const auto ht_sh_disabled = canvas->hit_test_handles(s_line_sh, &hit_node);
    assert(ht_sh_disabled == vector::renderer::HandleType::None);
    (void)ht_sh_disabled;

    // Enable segment moving: line handles become interactive
    canvas->set_segment_moving_enabled(true);
    assert(canvas->is_segment_moving_enabled());
    assert(canvas->is_segment_moving_active());
    const auto ht_sh = canvas->hit_test_handles(s_line_sh, &hit_node);
    assert(ht_sh == vector::renderer::HandleType::ParametricLineShoulder);
    (void)ht_sh;

    // 6. Hit test top shaft line (e.g. at world 160, 120)
    const auto s_line_st = canvas->world_to_screen({160.0, 120.0});
    const auto ht_st = canvas->hit_test_handles(s_line_st, &hit_node);
    assert(ht_st == vector::renderer::HandleType::ParametricLineShaftTop);
    (void)ht_st;

    // 7. Test dragging top shoulder point horizontally (freeform by default)
    // Click shoulder point
    const QPointF q_h0(s_h0.x, s_h0.y);
    QMouseEvent press_h0(QEvent::MouseButtonPress, q_h0, q_h0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press_h0);

    // Drag to world (180, 130) -> sx becomes (180 - 100)/200 = 0.4, sy0 becomes (130 - 100)/100 = 0.3
    const auto s_drag1 = canvas->world_to_screen({180.0, 130.0});
    const QPointF q_drag1(s_drag1.x, s_drag1.y);
    QMouseEvent move_h0(QEvent::MouseMove, q_drag1, q_drag1, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move_h0);

    // Verify all 4 shoulder vertices moved to 0.4, and top shaft moved to 0.3
    const auto* mod_node1 = graph->find_node(nid);
    assert(std::abs(mod_node1->polygon_vertices[1].x - 0.4) < 1e-3);
    assert(std::abs(mod_node1->polygon_vertices[2].x - 0.4) < 1e-3);
    assert(std::abs(mod_node1->polygon_vertices[4].x - 0.4) < 1e-3);
    assert(std::abs(mod_node1->polygon_vertices[5].x - 0.4) < 1e-3);
    assert(std::abs(mod_node1->polygon_vertices[0].y - 0.3) < 1e-3);
    assert(std::abs(mod_node1->polygon_vertices[1].y - 0.3) < 1e-3);
    // Freeform: bottom shaft remained at 0.8!
    assert(std::abs(mod_node1->polygon_vertices[5].y - 0.8) < 1e-3);
    assert(std::abs(mod_node1->polygon_vertices[6].y - 0.8) < 1e-3);
    (void)mod_node1;

    // Release mouse
    QMouseEvent rel_h0(QEvent::MouseButtonRelease, q_drag1, q_drag1, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &rel_h0);

    // 8. Test Alt-drag for symmetric modification across centerline
    // Click shoulder point again at new location (180, 130)
    QMouseEvent press_h0_sym(QEvent::MouseButtonPress, q_drag1, q_drag1, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    QApplication::sendEvent(canvas, &press_h0_sym);

    // Drag with Alt down to Y = 135 (sy0 = 0.35)
    const auto s_drag2 = canvas->world_to_screen({180.0, 135.0});
    const QPointF q_drag2(s_drag2.x, s_drag2.y);
    QMouseEvent move_h0_sym(QEvent::MouseMove, q_drag2, q_drag2, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    QApplication::sendEvent(canvas, &move_h0_sym);

    const auto* mod_node2 = graph->find_node(nid);
    assert(std::abs(mod_node2->polygon_vertices[0].y - 0.35) < 1e-3);
    assert(std::abs(mod_node2->polygon_vertices[1].y - 0.35) < 1e-3);
    // Symmetrically mirrored across centerline: sy1 = 1.0 - 0.35 = 0.65!
    assert(std::abs(mod_node2->polygon_vertices[5].y - 0.65) < 1e-3);
    assert(std::abs(mod_node2->polygon_vertices[6].y - 0.65) < 1e-3);
    (void)mod_node2;

    QMouseEvent rel_h0_sym(QEvent::MouseButtonRelease, q_drag2, q_drag2, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    QApplication::sendEvent(canvas, &rel_h0_sym);

    // 9. Test Undo and Redo of point modification
    assert(canvas->command_history() != nullptr);
    assert(canvas->command_history()->can_undo());
    canvas->command_history()->undo();

    const auto* undone_node = graph->find_node(nid);
    assert(undone_node != nullptr);
    assert(std::abs(undone_node->polygon_vertices[5].y - 0.8) < 1e-3);

    canvas->command_history()->redo();
    const auto* redone_node = graph->find_node(nid);
    assert(redone_node != nullptr);
    assert(std::abs(redone_node->polygon_vertices[5].y - 0.65) < 1e-3);
    (void)undone_node; (void)redone_node;

    // Render frame to ensure overlay with handles paints cleanly
    canvas->render_frame_for_test();

    std::cout << "[Test 39] Smart Parametric Shape Handles (Arrow Points & Attached Lines) passed successfully!" << std::endl;
  }

  // 40. Test Custom Control Points & Vertex Editing (Symbol Editor & Drawing Area)
  {
    std::cout << "===> Testing Custom Control Points & Vertex Editing (Symbol Editor & Drawing Area)..." << std::endl;
    vector::gui::MainWindow hwin;
    hwin.resize(1024, 768);
    auto* canvas = hwin.canvas();
    assert(canvas != nullptr);
    auto* graph = canvas->graph_model();
    assert(graph != nullptr);
    graph->clear();

    // 1. Test Control Point Configuration in SymbolEditorDialog
    vector::core::Node test_node;
    test_node.name = "CustomEDAComponent";
    test_node.shape = vector::core::ShapeType::Rectangle;
    test_node.bounds = {150.0, 150.0, 200.0, 120.0};
    const auto nid = graph->add_node(std::move(test_node));
    auto* placed_node = graph->find_node(nid);
    assert(placed_node != nullptr);

    {
      vector::gui::SymbolEditorDialog dlg(*placed_node, *graph, nullptr, &hwin);
      // Default rectangle initial control points (4 corners)
      assert(dlg.control_points().size() == 4);
      assert(dlg.point_table_->rowCount() == 4);

      // A: Add control point via API
      dlg.add_control_point({0.5, 0.0});
      assert(dlg.control_points().size() == 5);
      assert(dlg.point_table_->rowCount() == 5);

      // B: Add control point via UI button
      dlg.btn_add_point_->click();
      assert(dlg.control_points().size() == 6);
      assert(dlg.point_table_->rowCount() == 6);

      // C: Remove a control point via UI button
      dlg.point_table_->selectRow(1);
      dlg.btn_remove_point_->click();
      assert(dlg.control_points().size() == 5);
      assert(dlg.point_table_->rowCount() == 5);

      dlg.refresh_preview();
      dlg.apply_and_accept();
    }

    // Verify node updated with custom control points and shape
    assert(placed_node->polygon_vertices.size() == 5);
    assert(placed_node->shape == vector::core::ShapeType::CustomPolygon);
    assert(placed_node->properties["parametric_type"] == "custom_polygon");

    // 2. Test Control Point Insertion & Editing in Main Drawing Area
    canvas->select_node(nid, false);
    assert(canvas->selected_nodes().size() == 1);

    const size_t initial_count = placed_node->polygon_vertices.size();
    // Insert control point at world (250, 150) (top edge midpoint)
    const bool ins_ok = canvas->insert_control_point_at_world(nid, {250.0, 150.0});
    assert(ins_ok);
    assert(placed_node->polygon_vertices.size() == initial_count + 1);
    (void)initial_count;
    (void)ins_ok;

    // Verify Undo/Redo for added control point
    assert(canvas->command_history() != nullptr);
    assert(canvas->command_history()->can_undo());
    canvas->command_history()->undo();
    assert(placed_node->polygon_vertices.size() == initial_count);

    canvas->command_history()->redo();
    assert(placed_node->polygon_vertices.size() == initial_count + 1);

    // 3. Test Moving the Control Point (and Attached Lines)
    const int new_pt_idx = 1;
    const auto pt_handle = vector::renderer::make_parametric_point_handle(new_pt_idx);
    // Drag point to (250, 130) -> ny becomes (130 - 150)/120 < 0 -> clamped to 0.0
    // Drag to (250, 170) -> ny = (170 - 150)/120 = 0.1667
    canvas->apply_parametric_drag(*placed_node, pt_handle, {250.0, 170.0}, false);
    assert(std::abs(placed_node->polygon_vertices[new_pt_idx].y - ((170.0 - 150.0) / 120.0)) < 1e-3);

    // 4. Test Moving Attached Line Segment
    const auto line_handle = vector::renderer::make_parametric_line_handle(new_pt_idx);
    const double before_x1 = placed_node->polygon_vertices[new_pt_idx].x;
    canvas->apply_parametric_drag(*placed_node, line_handle, {270.0, 170.0}, false);
    // Line drag moves both vertices of the segment
    assert(placed_node->polygon_vertices[new_pt_idx].x != before_x1);
    (void)before_x1;

    // 5. Test Double-Click on Shape Edge to Add Point
    const size_t pre_dc_count = placed_node->polygon_vertices.size();
    // World point near right edge: bounds x=150, w=200 -> right edge is at x=350, y=210
    const auto s_edge = canvas->world_to_screen({350.0, 210.0});
    const QPointF q_edge(s_edge.x, s_edge.y);
    QMouseEvent dbl_click(QEvent::MouseButtonDblClick, q_edge, q_edge, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &dbl_click);
    assert(placed_node->polygon_vertices.size() == pre_dc_count + 1);
    (void)pre_dc_count;

    // 6. Test Removing Control Point
    const size_t pre_rem_count = placed_node->polygon_vertices.size();
    const bool rem_ok = canvas->remove_control_point(nid, 2);
    assert(rem_ok);
    assert(placed_node->polygon_vertices.size() == pre_rem_count - 1);
    (void)pre_rem_count;
    (void)rem_ok;

    // Undo removal
    canvas->command_history()->undo();
    assert(placed_node->polygon_vertices.size() == pre_rem_count);

    // 7. Test New Shapes Instant Control Point Creation
    const auto new_shape_id = canvas->add_shape(vector::core::ShapeType::Rectangle, {500.0, 400.0}, "NewBox", false);
    assert(new_shape_id != vector::core::InvalidNodeId);
    auto* new_shape = graph->find_node(new_shape_id);
    assert(new_shape != nullptr);
    assert(new_shape->polygon_vertices.empty());

    // Insert point immediately on new shape -> converts to polygon and inserts point
    const bool new_ins_ok = canvas->insert_control_point_at_world(new_shape_id, {500.0, 370.0});
    assert(new_ins_ok);
    assert(new_shape->shape == vector::core::ShapeType::CustomPolygon);
    assert(new_shape->polygon_vertices.size() == 5);
    (void)new_shape;
    (void)new_ins_ok;

    // Clean frame rendering test
    canvas->render_frame_for_test();

    std::cout << "[Test 40] Shape Control Points & Vertex Editing in Symbol Editor & Drawing Area passed successfully!" << std::endl;
  }

  // 41. Test Command Console Shape & Control Point Scripting with Code
  {
    std::cout << "===> Testing Command Console Shape & Control Point Scripting with Code..." << std::endl;
    vector::gui::MainWindow hwin;
    hwin.resize(1024, 768);
    auto* canvas = hwin.canvas();
    assert(canvas != nullptr);
    auto* graph = canvas->graph_model();
    assert(graph != nullptr);
    graph->clear();

    auto* console = hwin.console_dock();
    assert(console != nullptr);

    // 1. Test segmove command
    assert(!canvas->is_segment_moving_enabled());
    console->execute_command("segmove on");
    assert(canvas->is_segment_moving_enabled());
    console->execute_command("segmove off");
    assert(!canvas->is_segment_moving_enabled());
    console->execute_command("segmove toggle");
    assert(canvas->is_segment_moving_enabled());
    console->execute_command("segmove off");
    assert(!canvas->is_segment_moving_enabled());

    // 2. Add rectangle via code command: add rect <x,y> <w,h> [name]
    console->execute_command("add rect 100,150 180,90 CodeBox");
    assert(graph->node_count() == 1);
    auto* n1 = graph->find_node_by_name("CodeBox");
    assert(n1 != nullptr);
    assert(n1->bounds.x == 100.0 && n1->bounds.y == 150.0);
    assert(n1->bounds.width == 180.0 && n1->bounds.height == 90.0);

    // 3. Add circle via code: add circle <x,y> <r> [name]
    console->execute_command("add circle 400,200 45 CodeCircle");
    assert(graph->node_count() == 2);
    auto* n2 = graph->find_node_by_name("CodeCircle");
    assert(n2 != nullptr);
    assert(n2->bounds.width == 90.0 && n2->bounds.height == 90.0);

    // 4. Add arrow shape via code: add arrow <x,y> <w,h> [name]
    console->execute_command("add arrow 600,200 140,80 CodeArrow");
    assert(graph->node_count() == 3);
    auto* n3 = graph->find_node_by_name("CodeArrow");
    assert(n3 != nullptr);
    assert(n3->polygon_vertices.size() == 7);
    assert(vector::core::get_parametric_kind(*n3) == vector::core::ParametricShapeKind::Arrow);

    // 5. Modify shape with code on selected node
    canvas->select_node(n1->id, false);
    assert(canvas->selected_nodes().size() == 1);

    // Modify width
    console->execute_command("modify w 240");
    assert(n1->bounds.width == 240.0);

    // Modify height
    console->execute_command("modify h 120");
    assert(n1->bounds.height == 120.0);

    // Modify position
    console->execute_command("modify pos 120,160");
    assert(n1->bounds.x == 120.0 && n1->bounds.y == 160.0);

    // Modify rotation
    console->execute_command("modify rot 30");
    assert(std::abs(n1->rotation_deg - 30.0) < 1e-4);

    // Modify appearance
    console->execute_command("modify fill #2563EB");
    assert(n1->properties["fill_color"] == "#2563EB");

    // 6. Modify shape by target name
    console->execute_command("modify CodeCircle w 110");
    assert(n2->bounds.width == 110.0);
    (void)n2;

    // 7. Manipulate control points with code: point add, point list, point set, point del
    canvas->select_node(n3->id, false);
    assert(canvas->selected_nodes().size() == 1);

    // List points
    console->execute_command("point list");

    // Add control point
    const size_t arrow_pt_count = n3->polygon_vertices.size();
    console->execute_command("point add 620,200");
    assert(n3->polygon_vertices.size() == arrow_pt_count + 1);

    // Set control point
    console->execute_command("point set 0 0.05,0.25");
    assert(std::abs(n3->polygon_vertices[0].x - 0.05) < 1e-3);
    assert(std::abs(n3->polygon_vertices[0].y - 0.25) < 1e-3);

    // Del control point
    console->execute_command("point del 0");
    assert(n3->polygon_vertices.size() == arrow_pt_count);
    (void)arrow_pt_count;

    // 8. Rename shape with code
    console->execute_command("modify CodeBox name RenamedBox");
    assert(graph->find_node_by_name("RenamedBox") != nullptr);

    canvas->render_frame_for_test();
    std::cout << "[Test 41] Command Console Shape & Control Point Scripting with Code passed successfully!" << std::endl;
  }

  // 42. Test Bidirectional Port Visual Differentiation, Stencil Custom Sections, Built-in Protection & Canvas Drag-to-Shapes
  {
    std::cout << "\n[Test 42] Testing Bidirectional Port Differentiation & Shapes Custom Sections..." << std::endl;
    vector::gui::MainWindow win;
    win.show();

    auto* canvas = win.canvas();
    auto* graph = win.graph_model();
    auto* stencil_dock = win.findChild<vector::gui::StencilLibraryDock*>("ShapesDock");
    assert(canvas != nullptr && graph != nullptr && stencil_dock != nullptr);

    // 1. Port Visual Differentiation (InputPort vs OutputPort vs BidirectionalPort)
    const auto in_id = canvas->add_hierarchical_port({100.0, 100.0}, "in", "data_in", false);
    const auto out_id = canvas->add_hierarchical_port({300.0, 100.0}, "out", "data_out", false);
    const auto bi_id = canvas->add_hierarchical_port({200.0, 100.0}, "inout", "bus_io", false);

    const auto* in_node = graph->find_node(in_id);
    const auto* out_node = graph->find_node(out_id);
    const auto* bi_node = graph->find_node(bi_id);
    assert(in_node != nullptr && out_node != nullptr && bi_node != nullptr);

    // Check ShapeType
    assert(in_node->shape == vector::core::ShapeType::InputPort);
    assert(out_node->shape == vector::core::ShapeType::OutputPort);
    assert(bi_node->shape == vector::core::ShapeType::BidirectionalPort);

    // Check helper predicates
    assert(in_node->is_input_port() && !in_node->is_output_port() && !in_node->is_bidirectional_port());
    assert(!out_node->is_input_port() && out_node->is_output_port() && !out_node->is_bidirectional_port());
    assert(!bi_node->is_input_port() && !bi_node->is_output_port() && bi_node->is_bidirectional_port());
    assert(in_node->is_interface_port() && out_node->is_interface_port() && bi_node->is_interface_port());

    // Check color differentiation: Input = Emerald (#10B981), Output = Amber (#F59E0B), Bidirectional = Purple (#A855F7)
    assert(in_node->properties.at("fill_color") == "#10B981");
    assert(out_node->properties.at("fill_color") == "#F59E0B");
    assert(bi_node->properties.at("fill_color") == "#A855F7");

    // Check boundary interface pins: Input and Output have 1 pin, Bidirectional has 2 pins (East and West)
    assert(in_node->port_ids.size() == 1);
    assert(out_node->port_ids.size() == 1);
    assert(bi_node->port_ids.size() == 2);

    const auto* bi_p1 = graph->find_port(bi_node->port_ids[0]);
    const auto* bi_p2 = graph->find_port(bi_node->port_ids[1]);
    assert(bi_p1 != nullptr && bi_p2 != nullptr);
    assert((bi_p1->direction == vector::core::PortDirection::East && bi_p2->direction == vector::core::PortDirection::West) ||
           (bi_p1->direction == vector::core::PortDirection::West && bi_p2->direction == vector::core::PortDirection::East));

    // Render frame to ensure paintGL renders badges cleanly
    canvas->render_frame_for_test();

    // 2. Built-in Section & Shape Protection
    assert(stencil_dock->has_section("CSG Basis"));
    assert(stencil_dock->has_section("Regular Polygons"));
    assert(stencil_dock->has_section("Hierarchy & EDA Ports"));
    assert(stencil_dock->has_section("Connectors & Routing"));

    // Attempting to delete built-in sections must fail
    assert(!stencil_dock->remove_section("⚙ Hierarchy & EDA Ports"));
    assert(!stencil_dock->remove_section("📐 2D Geometric Primitives (CSG Basis)"));
    assert(stencil_dock->has_section("Hierarchy & EDA Ports"));

    // Attempting to delete built-in shapes must fail
    assert(stencil_dock->has_shape("Bidirectional Port"));
    assert(!stencil_dock->remove_shape("Bidirectional Port"));
    assert(stencil_dock->has_shape("Bidirectional Port"));
    assert(!stencil_dock->remove_shape("Hierarchical Input Port"));
    assert(stencil_dock->has_shape("Hierarchical Input Port"));

    // 3. User Custom Section Management (Create, Add Elements, Remove)
    auto* user_sec = stencil_dock->create_new_section("⚡ Power & Mixed-Signal");
    assert(user_sec != nullptr);
    assert(stencil_dock->has_section("Power & Mixed-Signal"));
    assert(!stencil_dock->is_category_builtin(user_sec));
    assert(stencil_dock->custom_section_count() >= 1);

    // Add elements into user custom section
    auto* custom_item = stencil_dock->add_shape_item(
        "LDO Regulator", vector::gui::StencilLibraryDock::StencilItemType::ShapeRect, 0, "⚡ Power & Mixed-Signal");
    assert(custom_item != nullptr);
    assert(stencil_dock->has_shape("LDO Regulator"));
    assert(!stencil_dock->is_item_builtin(custom_item));

    // User can delete custom shapes from custom sections
    assert(stencil_dock->remove_shape("LDO Regulator"));
    assert(!stencil_dock->has_shape("LDO Regulator"));

    // 4. Drag & Drop Elements from Canvas into Shapes Section
    canvas->clear_selection();
    canvas->select_node(bi_id, false);
    assert(canvas->selected_count() == 1);

    // Capture from selection into Shapes dock
    stencil_dock->add_from_selection();
    assert(stencil_dock->has_shape("bus_io"));

    // Test direct drop handling via handle_dropped_template
    vector::renderer::ShapePlacementTemplate drop_tmpl;
    drop_tmpl.name = "CustomBidirectionalTransceiver";
    drop_tmpl.category = vector::renderer::PlacementCategory::HierarchicalPort;
    drop_tmpl.shape_type = vector::core::ShapeType::BidirectionalPort;
    drop_tmpl.port_type = "inout";
    drop_tmpl.width = 110.0;
    drop_tmpl.height = 34.0;
    drop_tmpl.anchor_point = {0.0, 0.0};
    drop_tmpl.resize_point = {1.0, 1.0};

    auto* dropped_node_item = stencil_dock->handle_dropped_template(drop_tmpl, user_sec);
    assert(dropped_node_item != nullptr);
    assert(stencil_dock->has_shape("CustomBidirectionalTransceiver"));
    assert(!stencil_dock->is_item_builtin(dropped_node_item));

    // Roundtrip verification: double-click / trigger the custom shape attaches template to cursor
    stencil_dock->trigger_item(dropped_node_item);
    assert(canvas->is_placement_tool_active());
    assert(canvas->active_placement_template().name == "CustomBidirectionalTransceiver");
    assert(canvas->active_placement_template().shape_type == vector::core::ShapeType::BidirectionalPort);
    assert(canvas->active_placement_template().port_type == "inout");

    (void)in_node;
    (void)out_node;
    (void)bi_node;
    (void)bi_p1;
    (void)bi_p2;
    (void)custom_item;
    (void)dropped_node_item;

    // Clear placement tool
    canvas->cancel_placement();
    assert(!canvas->is_placement_tool_active());

    // User can delete custom section
    assert(stencil_dock->remove_section("⚡ Power & Mixed-Signal"));
    assert(!stencil_dock->has_section("Power & Mixed-Signal"));

    canvas->render_frame_for_test();
    std::cout << "[Test 42] Bidirectional Port Differentiation & Shapes Custom Sections passed successfully!" << std::endl;
  }

  std::cout << "===> All Theme System, Snapping & Hierarchical Port Smoke Tests passed successfully!" << std::endl;
  return 0;
}
