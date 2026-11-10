// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef VECTOR_ENABLE_QT
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#endif

#include "vector/core/Geometry.hpp"
#include "vector/core/GraphModel.hpp"
#include "vector/core/HierarchicalBlock.hpp"
#include "vector/core/SpatialIndex.hpp"
#include "vector/core/Command.hpp"
#include "vector/grid/GridManager.hpp"
#include "vector/renderer/CanvasWidget.hpp"
#include "vector/io/DrawioParser.hpp"
#include "vector/io/StorageEngine.hpp"

#include <fstream>
#include <memory>

#ifdef VECTOR_ENABLE_QT
class VectorMainWindow : public QMainWindow {
  Q_OBJECT

public:
  VectorMainWindow() {
    setWindowTitle("VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing");
    resize(1360, 860);

    // Initialize Core Engine
    graph_ = std::make_unique<vector::core::GraphModel>();
    spatial_index_ = std::make_unique<vector::core::FlatBvhSpatialIndex>();
    command_history_ = std::make_unique<vector::core::CommandHistory>();
    grid_manager_ = std::make_unique<vector::grid::GridManager>();
    hierarchy_ctx_ = std::make_unique<vector::core::HierarchyContext>(graph_.get());

    // Central Canvas
    canvas_ = new vector::renderer::CanvasWidget(this);
    canvas_->set_graph_model(graph_.get());
    canvas_->set_spatial_index(spatial_index_.get());
    canvas_->set_command_history(command_history_.get());
    canvas_->set_grid_manager(grid_manager_.get());
    canvas_->set_hierarchy_context(hierarchy_ctx_.get());
    setCentralWidget(canvas_);

    create_actions();
    create_menus();
    create_toolbars();
    create_docks();

    // Hook Signals
    connect(canvas_, &vector::renderer::CanvasWidget::status_message_requested,
            this, [this](const QString& msg) {
      statusBar()->showMessage(msg);
    });

    connect(canvas_, &vector::renderer::CanvasWidget::hierarchy_scope_changed,
            this, [this](const QString& path) {
      breadcrumb_label_->setText(QString(" 📍 %1 ").arg(path));
    });

    connect(canvas_, &vector::renderer::CanvasWidget::selection_changed,
            this, &VectorMainWindow::update_inspector);

    command_history_->set_on_history_changed([this]() {
      action_undo_->setEnabled(command_history_->can_undo());
      action_redo_->setEnabled(command_history_->can_redo());
    });

    // Seed default starter diagram
    setup_sample_diagram();
    statusBar()->showMessage("VECTOR ready. Select a tool from the toolbar or palette to insert figures.", 4000);
  }

private:
  void create_actions() {
    // File Actions
    action_new_ = new QAction("&New Diagram", this);
    action_new_->setShortcut(QKeySequence::New);
    connect(action_new_, &QAction::triggered, this, &VectorMainWindow::new_diagram);

    action_open_ = new QAction("&Open Diagram...", this);
    action_open_->setShortcut(QKeySequence::Open);
    connect(action_open_, &QAction::triggered, this, &VectorMainWindow::open_diagram);

    action_save_ = new QAction("&Save Diagram...", this);
    action_save_->setShortcut(QKeySequence::Save);
    connect(action_save_, &QAction::triggered, this, &VectorMainWindow::save_diagram);

    action_export_drawio_ = new QAction("Export &Draw.io XML...", this);
    connect(action_export_drawio_, &QAction::triggered, this, &VectorMainWindow::export_drawio);

    action_export_yaml_ = new QAction("Export &YAML...", this);
    connect(action_export_yaml_, &QAction::triggered, this, &VectorMainWindow::save_diagram);

    action_exit_ = new QAction("E&xit", this);
    action_exit_->setShortcut(QKeySequence::Quit);
    connect(action_exit_, &QAction::triggered, this, &QWidget::close);

    // Edit Actions
    action_undo_ = new QAction("&Undo", this);
    action_undo_->setShortcut(QKeySequence::Undo);
    action_undo_->setEnabled(false);
    connect(action_undo_, &QAction::triggered, this, [this]() {
      command_history_->undo();
      canvas_->refresh_spatial_index();
      canvas_->update();
    });

    action_redo_ = new QAction("&Redo", this);
    action_redo_->setShortcut(QKeySequence::Redo);
    action_redo_->setEnabled(false);
    connect(action_redo_, &QAction::triggered, this, [this]() {
      command_history_->redo();
      canvas_->refresh_spatial_index();
      canvas_->update();
    });

    action_delete_ = new QAction("&Delete", this);
    action_delete_->setShortcut(QKeySequence::Delete);
    connect(action_delete_, &QAction::triggered, canvas_, &vector::renderer::CanvasWidget::delete_selected);

    action_select_all_ = new QAction("Select &All", this);
    action_select_all_->setShortcut(QKeySequence::SelectAll);
    connect(action_select_all_, &QAction::triggered, canvas_, &vector::renderer::CanvasWidget::select_all);

    // Tool Actions
    tool_action_group_ = new QActionGroup(this);
    tool_action_group_->setExclusive(true);

    action_tool_select_ = new QAction("↖ Select", tool_action_group_);
    action_tool_select_->setCheckable(true);
    action_tool_select_->setChecked(true);
    action_tool_select_->setShortcut(QKeySequence(Qt::Key_S));
    connect(action_tool_select_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(vector::renderer::ToolMode::Select);
    });

    action_tool_rect_ = new QAction("▭ Rectangle", tool_action_group_);
    action_tool_rect_->setCheckable(true);
    action_tool_rect_->setShortcut(QKeySequence(Qt::Key_R));
    connect(action_tool_rect_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(vector::renderer::ToolMode::AddRectangle);
    });

    action_tool_rounded_ = new QAction("▢ Rounded", tool_action_group_);
    action_tool_rounded_->setCheckable(true);
    connect(action_tool_rounded_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(vector::renderer::ToolMode::AddRoundedRect);
    });

    action_tool_ellipse_ = new QAction("⬭ Ellipse", tool_action_group_);
    action_tool_ellipse_->setCheckable(true);
    action_tool_ellipse_->setShortcut(QKeySequence(Qt::Key_E));
    connect(action_tool_ellipse_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(vector::renderer::ToolMode::AddEllipse);
    });

    action_tool_block_ = new QAction("⧉ Subsystem Block", tool_action_group_);
    action_tool_block_->setCheckable(true);
    action_tool_block_->setShortcut(QKeySequence(Qt::Key_H));
    connect(action_tool_block_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(vector::renderer::ToolMode::AddHierarchicalBlock);
    });

    action_tool_wire_ = new QAction("⚡ Wire / Connector", tool_action_group_);
    action_tool_wire_->setCheckable(true);
    action_tool_wire_->setShortcut(QKeySequence(Qt::Key_C));
    connect(action_tool_wire_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(vector::renderer::ToolMode::AddConnector);
    });

    connect(canvas_, &vector::renderer::CanvasWidget::tool_mode_changed,
            this, [this](vector::renderer::ToolMode mode) {
      if (mode == vector::renderer::ToolMode::Select) action_tool_select_->setChecked(true);
      else if (mode == vector::renderer::ToolMode::AddRectangle) action_tool_rect_->setChecked(true);
      else if (mode == vector::renderer::ToolMode::AddRoundedRect) action_tool_rounded_->setChecked(true);
      else if (mode == vector::renderer::ToolMode::AddEllipse) action_tool_ellipse_->setChecked(true);
      else if (mode == vector::renderer::ToolMode::AddHierarchicalBlock) action_tool_block_->setChecked(true);
      else if (mode == vector::renderer::ToolMode::AddConnector) action_tool_wire_->setChecked(true);
    });

    // View Actions
    action_zoom_in_ = new QAction("Zoom &In", this);
    action_zoom_in_->setShortcut(QKeySequence::ZoomIn);
    connect(action_zoom_in_, &QAction::triggered, this, [this]() {
      canvas_->zoom_at(vector::core::Vec2d{canvas_->width() * 0.5, canvas_->height() * 0.5}, 1.25);
      canvas_->update();
    });

    action_zoom_out_ = new QAction("Zoom &Out", this);
    action_zoom_out_->setShortcut(QKeySequence::ZoomOut);
    connect(action_zoom_out_, &QAction::triggered, this, [this]() {
      canvas_->zoom_at(vector::core::Vec2d{canvas_->width() * 0.5, canvas_->height() * 0.5}, 0.8);
      canvas_->update();
    });

    action_fit_view_ = new QAction("&Fit Content", this);
    action_fit_view_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(action_fit_view_, &QAction::triggered, this, [this]() {
      canvas_->fit_content();
      canvas_->update();
    });

    action_reset_view_ = new QAction("&Reset View", this);
    action_reset_view_->setShortcut(QKeySequence(Qt::Key_Space));
    connect(action_reset_view_, &QAction::triggered, this, [this]() {
      canvas_->reset_view();
      canvas_->update();
    });

    action_toggle_grid_ = new QAction("Show &Grid", this);
    action_toggle_grid_->setCheckable(true);
    action_toggle_grid_->setChecked(true);
    action_toggle_grid_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    connect(action_toggle_grid_, &QAction::toggled, this, [this](bool checked) {
      auto cfg = grid_manager_->config();
      cfg.visible = checked;
      grid_manager_->set_config(cfg);
      canvas_->update();
    });

    action_toggle_snap_ = new QAction("Enable &Snapping", this);
    action_toggle_snap_->setCheckable(true);
    action_toggle_snap_->setChecked(true);
    connect(action_toggle_snap_, &QAction::toggled, this, [this](bool checked) {
      grid_manager_->set_snap_flags(checked ? vector::grid::SnapFlags::All : vector::grid::SnapFlags::None);
    });

    // Hierarchy Actions
    action_drill_down_ = new QAction("&Drill Down (Push)", this);
    action_drill_down_->setShortcut(QKeySequence(Qt::Key_Return));
    connect(action_drill_down_, &QAction::triggered, canvas_, &vector::renderer::CanvasWidget::drill_down_selected);

    action_pop_scope_ = new QAction("&Up to Parent (Pop)", this);
    action_pop_scope_->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(action_pop_scope_, &QAction::triggered, canvas_, &vector::renderer::CanvasWidget::pop_hierarchy_scope);
  }

  void create_menus() {
    // File Menu
    auto* menu_file = menuBar()->addMenu("&File");
    menu_file->addAction(action_new_);
    menu_file->addAction(action_open_);
    menu_file->addAction(action_save_);
    menu_file->addSeparator();
    menu_file->addAction(action_export_drawio_);
    menu_file->addAction(action_export_yaml_);
    menu_file->addSeparator();
    menu_file->addAction(action_exit_);

    // Edit Menu
    auto* menu_edit = menuBar()->addMenu("&Edit");
    menu_edit->addAction(action_undo_);
    menu_edit->addAction(action_redo_);
    menu_edit->addSeparator();
    menu_edit->addAction(action_delete_);
    menu_edit->addAction(action_select_all_);

    // Insert Menu
    auto* menu_insert = menuBar()->addMenu("&Insert");
    menu_insert->addAction(action_tool_select_);
    menu_insert->addSeparator();
    menu_insert->addAction(action_tool_rect_);
    menu_insert->addAction(action_tool_rounded_);
    menu_insert->addAction(action_tool_ellipse_);
    menu_insert->addAction(action_tool_block_);
    menu_insert->addSeparator();
    menu_insert->addAction(action_tool_wire_);

    // View Menu
    auto* menu_view = menuBar()->addMenu("&View");
    menu_view->addAction(action_zoom_in_);
    menu_view->addAction(action_zoom_out_);
    menu_view->addAction(action_fit_view_);
    menu_view->addAction(action_reset_view_);
    menu_view->addSeparator();
    menu_view->addAction(action_toggle_grid_);
    menu_view->addAction(action_toggle_snap_);

    // Hierarchy Menu
    auto* menu_hierarchy = menuBar()->addMenu("&Hierarchy");
    menu_hierarchy->addAction(action_drill_down_);
    menu_hierarchy->addAction(action_pop_scope_);

    // Help Menu
    auto* menu_help = menuBar()->addMenu("&Help");
    menu_help->addAction("About VECTOR...", this, [this]() {
      QMessageBox::about(this, "About VECTOR",
        "<h3>VECTOR 1.0.0</h3>"
        "<p><b>Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing</b></p>"
        "<p>High-performance open-source 2D diagramming and schematic engine.<br>"
        "Built with modern C++20 and Qt 6 on Linux (Wayland / X11).</p>"
        "<p>Licensed under GNU GPLv3.</p>");
    });
  }

  void create_toolbars() {
    // Main Creation Toolbar
    auto* tb = addToolBar("Tools");
    tb->setMovable(false);

    tb->addAction(action_tool_select_);
    tb->addSeparator();
    tb->addAction(action_tool_rect_);
    tb->addAction(action_tool_rounded_);
    tb->addAction(action_tool_ellipse_);
    tb->addAction(action_tool_block_);
    tb->addAction(action_tool_wire_);
    tb->addSeparator();

    tb->addAction(action_undo_);
    tb->addAction(action_redo_);
    tb->addAction(action_delete_);
    tb->addSeparator();

    tb->addAction(action_zoom_in_);
    tb->addAction(action_zoom_out_);
    tb->addAction(action_fit_view_);
    tb->addSeparator();

    tb->addAction(action_toggle_grid_);
    tb->addAction(action_toggle_snap_);
    tb->addSeparator();

    // Breadcrumb Navigation in Toolbar
    breadcrumb_label_ = new QLabel(" 📍 Top ", this);
    breadcrumb_label_->setStyleSheet(
      "QLabel { background: #E2E8F0; color: #1A202C; border-radius: 4px; padding: 4px 8px; font-weight: bold; }"
    );
    tb->addWidget(breadcrumb_label_);

    auto* btn_pop = new QPushButton("⬆ Up", this);
    btn_pop->setToolTip("Return to parent hierarchy level (Esc)");
    connect(btn_pop, &QPushButton::clicked, canvas_, &vector::renderer::CanvasWidget::pop_hierarchy_scope);
    tb->addWidget(btn_pop);
  }

  void create_docks() {
    // 1. Figures & Components Palette Dock
    auto* dock_palette = new QDockWidget("Figures & Blocks Palette", this);
    dock_palette->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* palette_widget = new QWidget(dock_palette);
    auto* palette_layout = new QVBoxLayout(palette_widget);
    palette_layout->setSpacing(8);

    auto make_btn = [this, palette_layout](const QString& title, vector::core::ShapeType shape) {
      auto* btn = new QPushButton(title, this);
      btn->setStyleSheet("QPushButton { padding: 8px; text-align: left; font-weight: 500; }");
      connect(btn, &QPushButton::clicked, this, [this, shape]() {
        const auto center = canvas_->visible_world_frustum().center();
        canvas_->add_shape(shape, center);
      });
      palette_layout->addWidget(btn);
    };

    auto* lbl_shapes = new QLabel("<b>Insert Figures at Center:</b>", this);
    palette_layout->addWidget(lbl_shapes);
    make_btn("+ Rectangle Block", vector::core::ShapeType::Rectangle);
    make_btn("+ Rounded Figure", vector::core::ShapeType::RoundedRect);
    make_btn("+ State Ellipse", vector::core::ShapeType::Ellipse);
    make_btn("+ Subsystem (Hierarchical)", vector::core::ShapeType::HierarchicalBlock);

    palette_layout->addSpacing(16);
    auto* lbl_wires = new QLabel("<b>Wiring:</b>", this);
    palette_layout->addWidget(lbl_wires);

    auto* btn_wire = new QPushButton("⚡ Active Wiring Tool", this);
    btn_wire->setStyleSheet("QPushButton { padding: 8px; text-align: left; font-weight: bold; background: #EBF8FF; }");
    connect(btn_wire, &QPushButton::clicked, this, [this]() {
      canvas_->set_tool_mode(vector::renderer::ToolMode::AddConnector);
    });
    palette_layout->addWidget(btn_wire);

    palette_layout->addStretch();
    dock_palette->setWidget(palette_widget);
    addDockWidget(Qt::LeftDockWidgetArea, dock_palette);

    // 2. Element Inspector Dock
    auto* dock_inspector = new QDockWidget("Properties Inspector", this);
    dock_inspector->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    auto* inspector_widget = new QWidget(dock_inspector);
    auto* form_layout = new QFormLayout(inspector_widget);

    inspector_name_ = new QLineEdit(this);
    connect(inspector_name_, &QLineEdit::editingFinished, this, [this]() {
      if (active_inspected_node_ != vector::core::InvalidNodeId && graph_) {
        if (auto* n = graph_->find_node(active_inspected_node_)) {
          n->name = inspector_name_->text().toStdString();
          canvas_->update();
        }
      }
    });

    inspector_x_ = new QDoubleSpinBox(this);
    inspector_x_->setRange(-100000.0, 100000.0);
    inspector_y_ = new QDoubleSpinBox(this);
    inspector_y_->setRange(-100000.0, 100000.0);
    inspector_w_ = new QDoubleSpinBox(this);
    inspector_w_->setRange(10.0, 5000.0);
    inspector_h_ = new QDoubleSpinBox(this);
    inspector_h_->setRange(10.0, 5000.0);
    inspector_rotation_ = new QDoubleSpinBox(this);
    inspector_rotation_->setRange(0.0, 360.0);
    inspector_rotation_->setSingleStep(15.0);
    inspector_rotation_->setSuffix("°");
    inspector_rotation_->setWrapping(true);

    auto update_geom = [this]() {
      if (active_inspected_node_ != vector::core::InvalidNodeId && graph_) {
        if (auto* n = graph_->find_node(active_inspected_node_)) {
          n->bounds.x = inspector_x_->value();
          n->bounds.y = inspector_y_->value();
          n->bounds.width = inspector_w_->value();
          n->bounds.height = inspector_h_->value();
          n->rotation_deg = inspector_rotation_->value();
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
      }
    };

    connect(inspector_x_, &QDoubleSpinBox::editingFinished, this, update_geom);
    connect(inspector_y_, &QDoubleSpinBox::editingFinished, this, update_geom);
    connect(inspector_w_, &QDoubleSpinBox::editingFinished, this, update_geom);
    connect(inspector_h_, &QDoubleSpinBox::editingFinished, this, update_geom);
    connect(inspector_rotation_, &QDoubleSpinBox::editingFinished, this, update_geom);

    form_layout->addRow("Name:", inspector_name_);
    form_layout->addRow("X:", inspector_x_);
    form_layout->addRow("Y:", inspector_y_);
    form_layout->addRow("Width:", inspector_w_);
    form_layout->addRow("Height:", inspector_h_);
    form_layout->addRow("Rotation:", inspector_rotation_);

    dock_inspector->setWidget(inspector_widget);
    addDockWidget(Qt::RightDockWidgetArea, dock_inspector);
  }

  void update_inspector() {
    if (!graph_) return;
    for (const auto& [nid, n] : graph_->nodes()) {
      if (n.selected) {
        active_inspected_node_ = nid;
        inspector_name_->setText(QString::fromStdString(n.name));
        inspector_x_->setValue(n.bounds.x);
        inspector_y_->setValue(n.bounds.y);
        inspector_w_->setValue(n.bounds.width);
        inspector_h_->setValue(n.bounds.height);
        inspector_rotation_->setValue(n.rotation_deg);
        return;
      }
    }
    active_inspected_node_ = vector::core::InvalidNodeId;
    inspector_name_->clear();
    inspector_x_->setValue(0.0);
    inspector_y_->setValue(0.0);
    inspector_w_->setValue(0.0);
    inspector_h_->setValue(0.0);
    inspector_rotation_->setValue(0.0);
  }

  void new_diagram() {
    if (graph_) {
      graph_->clear();
      canvas_->refresh_spatial_index();
      canvas_->reset_view();
      canvas_->update();
      statusBar()->showMessage("New diagram created.", 3000);
    }
  }

  void open_diagram() {
    const QString filename = QFileDialog::getOpenFileName(
        this, "Open Diagram", "", "Supported Formats (*.yaml *.drawio *.xml *.diag);;All Files (*)");
    if (filename.isEmpty()) return;

    std::ifstream in(filename.toStdString());
    if (!in.is_open()) {
      QMessageBox::warning(this, "Error", "Cannot open selected file.");
      return;
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    if (filename.endsWith(".drawio", Qt::CaseInsensitive) || filename.endsWith(".xml", Qt::CaseInsensitive)) {
      vector::io::DrawioParser parser;
      auto res = parser.import_xml(content, *graph_);
      if (!res.success) {
        QMessageBox::warning(this, "Import Error", QString::fromStdString(res.error_message));
        return;
      }
    } else {
      vector::io::StorageEngine storage;
      auto res = storage.deserialize_yaml(content, *graph_);
      if (!res.success) {
        QMessageBox::warning(this, "Import Error", QString::fromStdString(res.error_message));
        return;
      }
    }

    canvas_->refresh_spatial_index();
    canvas_->fit_content();
    canvas_->update();
    statusBar()->showMessage(QString("Opened %1 (%2 nodes, %3 edges)")
        .arg(filename)
        .arg(graph_->node_count())
        .arg(graph_->edge_count()));
  }

  void save_diagram() {
    const QString filename = QFileDialog::getSaveFileName(
        this, "Save Diagram", "diagram.yaml", "VECTOR YAML (*.yaml);;All Files (*)");
    if (filename.isEmpty()) return;

    vector::io::StorageEngine storage;
    std::string yaml_out = storage.serialize_yaml(*graph_);

    std::ofstream out(filename.toStdString());
    if (!out.is_open()) {
      QMessageBox::warning(this, "Error", "Cannot write to selected file.");
      return;
    }
    out << yaml_out;
    statusBar()->showMessage(QString("Saved diagram to %1").arg(filename), 4000);
  }

  void export_drawio() {
    const QString filename = QFileDialog::getSaveFileName(
        this, "Export Draw.io XML", "diagram.drawio", "Draw.io Diagram (*.drawio *.xml);;All Files (*)");
    if (filename.isEmpty()) return;

    vector::io::DrawioParser parser;
    std::string xml = parser.export_xml(*graph_);

    std::ofstream out(filename.toStdString());
    if (!out.is_open()) {
      QMessageBox::warning(this, "Error", "Cannot write to export file.");
      return;
    }
    out << xml;
    statusBar()->showMessage(QString("Exported Draw.io XML to %1").arg(filename), 4000);
  }

  void setup_sample_diagram() {
    // Create initial architecture demonstration
    vector::core::Node n1;
    n1.name = "Input Sensor Array";
    n1.bounds = {100.0, 160.0, 180.0, 80.0};
    n1.shape = vector::core::ShapeType::Rectangle;
    const vector::core::NodeId n1_id = graph_->add_node(std::move(n1));

    vector::core::Port p1;
    p1.parent_node = n1_id;
    p1.name = "E";
    p1.local_anchor = {1.0, 0.5};
    p1.direction = vector::core::PortDirection::East;
    const vector::core::PortId p1_id = graph_->add_port(std::move(p1));

    vector::core::Node n2;
    n2.name = "DSP Filter Block";
    n2.bounds = {420.0, 160.0, 200.0, 90.0};
    n2.shape = vector::core::ShapeType::HierarchicalBlock;
    const vector::core::NodeId n2_id = graph_->add_node(std::move(n2));

    vector::core::Port p2;
    p2.parent_node = n2_id;
    p2.name = "W";
    p2.local_anchor = {0.0, 0.5};
    p2.direction = vector::core::PortDirection::West;
    const vector::core::PortId p2_id = graph_->add_port(std::move(p2));

    vector::core::Edge edge;
    edge.name = "SPI Bus";
    edge.source_node = n1_id;
    edge.source_port = p1_id;
    edge.target_node = n2_id;
    edge.target_port = p2_id;
    edge.waypoints = {
      {280.0, 200.0},
      {350.0, 200.0},
      {350.0, 205.0},
      {420.0, 205.0}
    };
    graph_->add_edge(std::move(edge));

    canvas_->refresh_spatial_index();
  }

  // Core Members
  std::unique_ptr<vector::core::GraphModel> graph_;
  std::unique_ptr<vector::core::FlatBvhSpatialIndex> spatial_index_;
  std::unique_ptr<vector::core::CommandHistory> command_history_;
  std::unique_ptr<vector::grid::GridManager> grid_manager_;
  std::unique_ptr<vector::core::HierarchyContext> hierarchy_ctx_;

  vector::renderer::CanvasWidget* canvas_{nullptr};
  QLabel* breadcrumb_label_{nullptr};

  // Inspector Members
  vector::core::NodeId active_inspected_node_{vector::core::InvalidNodeId};
  QLineEdit* inspector_name_{nullptr};
  QDoubleSpinBox* inspector_x_{nullptr};
  QDoubleSpinBox* inspector_y_{nullptr};
  QDoubleSpinBox* inspector_w_{nullptr};
  QDoubleSpinBox* inspector_h_{nullptr};
  QDoubleSpinBox* inspector_rotation_{nullptr};

  // Actions
  QAction* action_new_{nullptr};
  QAction* action_open_{nullptr};
  QAction* action_save_{nullptr};
  QAction* action_export_drawio_{nullptr};
  QAction* action_export_yaml_{nullptr};
  QAction* action_exit_{nullptr};

  QAction* action_undo_{nullptr};
  QAction* action_redo_{nullptr};
  QAction* action_delete_{nullptr};
  QAction* action_select_all_{nullptr};

  QActionGroup* tool_action_group_{nullptr};
  QAction* action_tool_select_{nullptr};
  QAction* action_tool_rect_{nullptr};
  QAction* action_tool_rounded_{nullptr};
  QAction* action_tool_ellipse_{nullptr};
  QAction* action_tool_block_{nullptr};
  QAction* action_tool_wire_{nullptr};

  QAction* action_zoom_in_{nullptr};
  QAction* action_zoom_out_{nullptr};
  QAction* action_fit_view_{nullptr};
  QAction* action_reset_view_{nullptr};
  QAction* action_toggle_grid_{nullptr};
  QAction* action_toggle_snap_{nullptr};

  QAction* action_drill_down_{nullptr};
  QAction* action_pop_scope_{nullptr};
};
#endif

int main(int argc, char* argv[]) {
#ifdef VECTOR_ENABLE_QT
  QApplication app(argc, argv);
  app.setApplicationName("VECTOR");
  app.setApplicationDisplayName("VECTOR - Viewport Engine");

  VectorMainWindow window;
  window.show();

  return app.exec();
#else
  (void)argc;
  (void)argv;
  return 0;
#endif
}

#ifdef VECTOR_ENABLE_QT
#include "main_gui.moc"
#endif
