// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_GUI_MAIN_WINDOW_HPP
#define MADI_GUI_MAIN_WINDOW_HPP

#include "madi/core/Command.hpp"
#include "madi/core/Geometry.hpp"
#include "madi/core/GraphModel.hpp"
#include "madi/core/HierarchicalBlock.hpp"
#include "madi/core/SpatialIndex.hpp"
#include "madi/grid/GridManager.hpp"
#include "madi/gui/AboutMadiDialog.hpp"
#include "madi/gui/CommandConsoleDock.hpp"
#include "madi/gui/FormatInspectorDock.hpp"
#include "madi/gui/HierarchyTreeDock.hpp"
#include "madi/gui/IconFactory.hpp"
#include "madi/gui/KeyboardShortcutsDialog.hpp"
#include "madi/gui/SnapGridSettingsDialog.hpp"
#include "madi/gui/StencilLibraryDock.hpp"
#include "madi/gui/SymbolEditorDialog.hpp"
#include "madi/gui/ThemeManager.hpp"
#include "madi/io/DrawioParser.hpp"
#include "madi/io/StorageEngine.hpp"
#include "madi/renderer/CanvasWidget.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QSysInfo>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <fstream>
#include <memory>

namespace madi::gui {

class DiagramModelListener : public madi::core::IGraphModelListener {
public:
  explicit DiagramModelListener(std::function<void()> cb) : cb_(std::move(cb)) {}
  void on_node_added(const madi::core::Node&) override { if (cb_) cb_(); }
  void on_node_removed(madi::core::NodeId) override { if (cb_) cb_(); }
  void on_node_modified(const madi::core::Node&) override { if (cb_) cb_(); }
  void on_edge_added(const madi::core::Edge&) override { if (cb_) cb_(); }
  void on_edge_removed(madi::core::EdgeId) override { if (cb_) cb_(); }
  void on_edge_modified(const madi::core::Edge&) override { if (cb_) cb_(); }
  void on_port_added(const madi::core::Port&) override { if (cb_) cb_(); }
  void on_port_removed(madi::core::PortId) override { if (cb_) cb_(); }
  void on_model_cleared() override {}
private:
  std::function<void()> cb_;
};

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
    setWindowTitle("Madí - Model, Align & Diagram Interconnects");
    resize(1360, 860);

    // Initialize Core Engine
    graph_ = std::make_unique<madi::core::GraphModel>();
    spatial_index_ = std::make_unique<madi::core::FlatBvhSpatialIndex>();
    command_history_ = std::make_unique<madi::core::CommandHistory>();
    grid_manager_ = std::make_unique<madi::grid::GridManager>();
    hierarchy_ctx_ = std::make_unique<madi::core::HierarchyContext>(graph_.get());

    // Central Canvas Viewport
    canvas_ = new madi::renderer::CanvasWidget(this);
    canvas_->setObjectName("mainDiagramCanvas");
    canvas_->set_graph_model(graph_.get());
    canvas_->set_spatial_index(spatial_index_.get());
    canvas_->set_command_history(command_history_.get());
    canvas_->set_grid_manager(grid_manager_.get());
    canvas_->set_hierarchy_context(hierarchy_ctx_.get());
    setCentralWidget(canvas_);

    model_listener_ = std::make_unique<DiagramModelListener>([this]() {
      on_model_changed();
    });
    graph_->add_listener(model_listener_.get());

    setDockNestingEnabled(true);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    create_actions();
    create_docks();
    create_toolbars();
    create_menus();

    // Initialize Theme System
    auto& tm = ThemeManager::instance();
    tm.scan_themes();
    if (tm.find_ui_theme("solarized-dark")) {
      tm.set_active_ui_theme("solarized-dark");
    } else if (!tm.ui_themes().empty()) {
      tm.set_active_ui_theme(tm.ui_themes().front().id);
    }
    if (tm.find_diagram_theme("solarized-dark")) {
      tm.set_active_diagram_theme("solarized-dark");
      canvas_->set_diagram_style(to_canvas_style(tm.current_diagram_theme()));
    } else if (!tm.diagram_themes().empty()) {
      tm.set_active_diagram_theme(tm.diagram_themes().front().id);
      canvas_->set_diagram_style(to_canvas_style(tm.current_diagram_theme()));
    }

    connect(&tm, &ThemeManager::ui_theme_changed, this, [this](const UITheme&) {
      rebuild_theme_menus();
      update_tool_icons();
    });
    connect(&tm, &ThemeManager::diagram_theme_changed, this, [this](const DiagramTheme& dt) {
      canvas_->set_diagram_style(to_canvas_style(dt));
      rebuild_theme_menus();
    });

    update_tool_icons();

    // Status Bar Controls - Hierarchy Path, Coordinates, Selection & Tools
    auto* hier_status_widget = new QWidget(this);
    auto* hier_status_layout = new QHBoxLayout(hier_status_widget);
    hier_status_layout->setContentsMargins(0, 0, 0, 0);
    hier_status_layout->setSpacing(4);

    status_hierarchy_label_ = new QLabel(" 📍 Scope: Top ", hier_status_widget);
    status_hierarchy_label_->setStyleSheet(
      "QLabel { background: #1E293B; color: #38BDF8; border: 1px solid #334155; border-radius: 4px; padding: 2px 8px; font-weight: bold; font-size: 11px; }"
    );
    status_hierarchy_label_->setToolTip("Current Hierarchical Scope");

    btn_pop_scope_status_ = new QPushButton("⬆ Up", hier_status_widget);
    btn_pop_scope_status_->setToolTip("Ascend to Parent Scope (Ctrl+U)");
    btn_pop_scope_status_->setStyleSheet("QPushButton { padding: 2px 6px; font-size: 11px; font-weight: bold; }");
    btn_pop_scope_status_->setEnabled(false);
    connect(btn_pop_scope_status_, &QPushButton::clicked, this, [this]() {
      if (canvas_) canvas_->pop_hierarchy_scope();
    });

    hier_status_layout->addWidget(status_hierarchy_label_);
    hier_status_layout->addWidget(btn_pop_scope_status_);
    statusBar()->addPermanentWidget(hier_status_widget);

    status_coord_label_ = new QLabel("X: 0 mm   Y: 0 mm", this);
    statusBar()->addPermanentWidget(status_coord_label_);

    status_sel_label_ = new QLabel("", this);
    statusBar()->addPermanentWidget(status_sel_label_);

    btn_snap_status_ = new QPushButton("SNAP: Normal (6px)", this);
    btn_snap_status_->setToolTip("Snap Mode (F9 / Ctrl+B): Left-click to toggle, Right-click for Snap & Grid settings");
    btn_snap_status_->setStyleSheet("QPushButton { padding: 2px 8px; font-weight: bold; }");
    btn_snap_status_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn_snap_status_, &QPushButton::clicked, this, [this]() {
      grid_manager_->set_snap_enabled(!grid_manager_->is_snap_enabled());
      update_snap_ui();
    });
    connect(btn_snap_status_, &QPushButton::customContextMenuRequested, this, [this](const QPoint& pt) {
      show_snap_status_context_menu(btn_snap_status_->mapToGlobal(pt));
    });
    statusBar()->addPermanentWidget(btn_snap_status_);

    btn_angle_snap_status_ = new QPushButton("∠ SNAP: 15°", this);
    btn_angle_snap_status_->setToolTip("Angle Snap (Alt+A): Left-click to toggle, Right-click for Angle Snap options (15°, 30°, 45°, 90°)");
    btn_angle_snap_status_->setStyleSheet("QPushButton { padding: 2px 8px; font-weight: bold; }");
    btn_angle_snap_status_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn_angle_snap_status_, &QPushButton::clicked, this, [this]() {
      if (canvas_) {
        canvas_->set_angle_snapping_enabled(!canvas_->is_angle_snapping_enabled());
        update_angle_snap_ui();
      }
    });
    connect(btn_angle_snap_status_, &QPushButton::customContextMenuRequested, this, [this](const QPoint& pt) {
      show_angle_snap_context_menu(btn_angle_snap_status_->mapToGlobal(pt));
    });
    statusBar()->addPermanentWidget(btn_angle_snap_status_);

    auto* zoom_container = new QWidget(this);
    auto* zoom_layout = new QHBoxLayout(zoom_container);
    zoom_layout->setContentsMargins(0, 0, 0, 0);
    zoom_layout->setSpacing(4);

    btn_zoom_out_status_ = new QPushButton("-", zoom_container);
    btn_zoom_out_status_->setFixedSize(22, 22);
    connect(btn_zoom_out_status_, &QPushButton::clicked, this, [this]() {
      canvas_->zoom_at(madi::core::Vec2d{canvas_->width() * 0.5, canvas_->height() * 0.5}, 0.8);
      canvas_->update();
    });

    zoom_slider_ = new QSlider(Qt::Horizontal, zoom_container);
    zoom_slider_->setRange(10, 400);
    zoom_slider_->setValue(100);
    zoom_slider_->setFixedWidth(100);
    connect(zoom_slider_, &QSlider::valueChanged, this, [this](int val) {
      canvas_->set_zoom_level(val / 100.0);
    });

    btn_zoom_in_status_ = new QPushButton("+", zoom_container);
    btn_zoom_in_status_->setFixedSize(22, 22);
    connect(btn_zoom_in_status_, &QPushButton::clicked, this, [this]() {
      canvas_->zoom_at(madi::core::Vec2d{canvas_->width() * 0.5, canvas_->height() * 0.5}, 1.25);
      canvas_->update();
    });

    zoom_percent_label_ = new QLabel("100%", zoom_container);
    zoom_percent_label_->setStyleSheet("QLabel { font-weight: bold; min-width: 42px; }");

    zoom_layout->addWidget(btn_zoom_out_status_);
    zoom_layout->addWidget(zoom_slider_);
    zoom_layout->addWidget(btn_zoom_in_status_);
    zoom_layout->addWidget(zoom_percent_label_);

    statusBar()->addPermanentWidget(zoom_container);

    // Hook Signals
    connect(canvas_, &madi::renderer::CanvasWidget::status_message_requested,
            this, [this](const QString& msg) {
      statusBar()->showMessage(msg);
    });

    connect(canvas_, &madi::renderer::CanvasWidget::segment_moving_toggled,
            this, [this](bool enabled) {
      if (action_toggle_segment_moving_ && action_toggle_segment_moving_->isChecked() != enabled) {
        action_toggle_segment_moving_->setChecked(enabled);
      }
    });

    connect(canvas_, &madi::renderer::CanvasWidget::cursor_position_changed,
            this, [this](const QString& pos_str) {
      status_coord_label_->setText(pos_str);
    });

    connect(canvas_, &madi::renderer::CanvasWidget::zoom_level_changed,
            this, [this](double z) {
      const int pct = static_cast<int>(std::round(z * 100.0));
      zoom_percent_label_->setText(QString("%1%").arg(pct));
      zoom_slider_->blockSignals(true);
      zoom_slider_->setValue(std::clamp(pct, 10, 400));
      zoom_slider_->blockSignals(false);
    });

    connect(canvas_, &madi::renderer::CanvasWidget::focus_console_requested,
            this, [this]() {
      if (console_dock_) {
        console_dock_->show();
        console_dock_->focus_input();
      }
    });

    connect(canvas_, &madi::renderer::CanvasWidget::ruler_context_menu_requested,
            this, &MainWindow::show_ruler_context_menu);
    connect(canvas_, &madi::renderer::CanvasWidget::snap_settings_requested,
            this, [this]() {
      open_snap_grid_settings(2);
    });
    connect(canvas_, &madi::renderer::CanvasWidget::snap_mode_toggled,
            this, [this](bool) {
      update_snap_ui();
    });
    connect(canvas_, &madi::renderer::CanvasWidget::angle_snap_toggled,
            this, [this](bool) {
      update_angle_snap_ui();
    });
    connect(canvas_, &madi::renderer::CanvasWidget::angle_snap_step_changed,
            this, [this](double) {
      update_angle_snap_ui();
    });

    connect(canvas_, &madi::renderer::CanvasWidget::hierarchy_scope_changed,
            this, [this](const QString& path) {
      update_status_hierarchy(path);
      if (btn_pop_scope_status_) {
        btn_pop_scope_status_->setEnabled(canvas_->can_pop_scope());
      }
      action_pop_scope_->setEnabled(canvas_->can_pop_scope());
      if (hierarchy_dock_) {
        hierarchy_dock_->set_graph_model(canvas_->graph_model());
        hierarchy_dock_->set_scope_info(path, canvas_->can_pop_scope());
      }
      update_inspector();
    });

    connect(canvas_, &madi::renderer::CanvasWidget::selection_changed,
            this, [this]() {
      update_inspector();
      if (hierarchy_dock_) {
        hierarchy_dock_->refresh();
      }
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ != madi::core::InvalidNodeId && cur_g) {
        if (const auto* sn = cur_g->find_node(active_inspected_node_)) {
          status_sel_label_->setText(QString("Selected: %1 (%2 × %3 px)")
              .arg(QString::fromStdString(sn->name))
              .arg(static_cast<int>(sn->bounds.width))
              .arg(static_cast<int>(sn->bounds.height)));
        }
      } else {
        status_sel_label_->clear();
      }
    });

    connect(canvas_, &madi::renderer::CanvasWidget::node_rotation_changed,
            this, [this](madi::core::NodeId nid, double deg) {
      if (nid == active_inspected_node_ && inspector_rotation_) {
        inspector_rotation_->blockSignals(true);
        inspector_rotation_->setValue(deg);
        inspector_rotation_->blockSignals(false);
      }
    });

    command_history_->set_on_history_changed([this]() {
      on_model_changed();
      action_undo_->setEnabled(command_history_->can_undo());
      action_redo_->setEnabled(command_history_->can_redo());
      if (btn_dock_undo_) btn_dock_undo_->setEnabled(command_history_->can_undo());
      if (btn_dock_redo_) btn_dock_redo_->setEnabled(command_history_->can_redo());
    });

    connect(canvas_, &madi::renderer::CanvasWidget::diagram_modified,
            this, [this]() { on_model_changed(); });

    // Seed default starter diagram
    suppress_model_modified_ = true;
    setup_sample_diagram();
    suppress_model_modified_ = false;
    set_document_modified(false);
    update_window_title();
    if (hierarchy_dock_) {
      hierarchy_dock_->set_graph_model(graph_.get());
    }

    // Connect Console persistence callbacks
    if (console_dock_) {
      console_dock_->set_persistence_callbacks(
          [this]() { return is_remember_settings_accepted(); },
          [this](bool b) { set_remember_settings_accepted(b); },
          [this]() { reset_layout(); });
    }

    // Restore window positions, themes, colours, and settings if user accepted
    if (is_remember_settings_accepted()) {
      restore_settings();
    }

    update_snap_ui();
    update_angle_snap_ui();
    setup_system_tray();
    statusBar()->showMessage("Madí ready. Select a tool from the toolbar or palette to insert figures.", 4000);
  }

  ~MainWindow() override {
    if (tray_icon_) {
      tray_icon_->hide();
      delete tray_icon_;
      tray_icon_ = nullptr;
    }
    if (graph_ && model_listener_) {
      graph_->remove_listener(model_listener_.get());
    }
  }

  [[nodiscard]] madi::renderer::CanvasWidget* canvas() const noexcept { return canvas_; }
  [[nodiscard]] madi::grid::GridManager* grid_manager() const noexcept { return grid_manager_.get(); }
  [[nodiscard]] madi::core::GraphModel* graph_model() const noexcept { return graph_.get(); }
  [[nodiscard]] madi::core::GraphModel* current_diagram_graph() const noexcept {
    if (canvas_ && canvas_->graph_model()) return canvas_->graph_model();
    return graph_.get();
  }
  [[nodiscard]] QLabel* status_hierarchy_label() const noexcept { return status_hierarchy_label_; }
  [[nodiscard]] QPushButton* btn_pop_scope_status() const noexcept { return btn_pop_scope_status_; }
  [[nodiscard]] QToolBar* formatting_toolbar() const noexcept { return toolbar_format_; }
  [[nodiscard]] QDockWidget* dock_tools() const noexcept { return dock_palette_; }
  [[nodiscard]] StencilLibraryDock* dock_shapes() const noexcept { return stencil_dock_; }
  [[nodiscard]] QDoubleSpinBox* inspector_x() const noexcept { return inspector_x_; }
  [[nodiscard]] QDoubleSpinBox* inspector_y() const noexcept { return inspector_y_; }
  [[nodiscard]] QDoubleSpinBox* inspector_w() const noexcept { return inspector_w_; }
  [[nodiscard]] QDoubleSpinBox* inspector_h() const noexcept { return inspector_h_; }
  [[nodiscard]] QDoubleSpinBox* inspector_rotation() const noexcept { return inspector_rotation_; }
  [[nodiscard]] QPushButton* btn_angle_snap_status() const noexcept { return btn_angle_snap_status_; }
  [[nodiscard]] QAction* action_toggle_angle_snap() const noexcept { return action_toggle_angle_snap_; }
  [[nodiscard]] QToolButton* btn_tb_angle_snap() const noexcept { return btn_tb_angle_snap_; }
  [[nodiscard]] QToolButton* btn_dock_angle_snap() const noexcept { return btn_dock_angle_snap_; }
  [[nodiscard]] QToolButton* btn_dock_rot_cw() const noexcept { return btn_dock_rot_cw_; }
  [[nodiscard]] QToolButton* btn_dock_rot_ccw() const noexcept { return btn_dock_rot_ccw_; }
  [[nodiscard]] QToolButton* btn_insp_rot_cw() const noexcept { return btn_insp_rot_cw_; }
  [[nodiscard]] QToolButton* btn_insp_rot_ccw() const noexcept { return btn_insp_rot_ccw_; }
  [[nodiscard]] QToolButton* btn_insp_angle_snap() const noexcept { return btn_insp_angle_snap_; }
  [[nodiscard]] QAction* action_keyboard_shortcuts() const noexcept { return action_keyboard_shortcuts_; }
  [[nodiscard]] CommandConsoleDock* console_dock() const noexcept { return console_dock_; }
  [[nodiscard]] QAction* action_toggle_segment_moving() const noexcept { return action_toggle_segment_moving_; }

  static QIcon create_app_icon() {
    QIcon icon;
    if (QFile::exists(":/icons/icon.svg")) {
      icon.addFile(":/icons/icon.svg");
    }
    if (QFile::exists(":/icons/madi.svg")) {
      icon.addFile(":/icons/madi.svg");
    }
    if (QFile::exists(":/icons/vector.svg")) {
      icon.addFile(":/icons/vector.svg");
    }

    // Generate standard raster pixmaps directly from icon.svg for WMs and system tray
    QIcon svg_icon(":/icons/icon.svg");
    if (!svg_icon.isNull()) {
      for (int s : {16, 22, 24, 32, 48, 64, 128, 256, 512}) {
        QPixmap pm = svg_icon.pixmap(s, s);
        if (!pm.isNull()) {
          icon.addPixmap(pm);
        }
      }
    } else {
      QPixmap base;
      if (QFile::exists(":/icons/icon.png")) {
        base.load(":/icons/icon.png");
      } else if (QFile::exists(":/icons/madi.png")) {
        base.load(":/icons/madi.png");
      }
      if (!base.isNull()) {
        for (int s : {16, 22, 24, 32, 48, 64, 128, 256, 512}) {
          icon.addPixmap(base.scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
      }
    }

    if (icon.isNull()) {
      icon = QIcon::fromTheme("madi", QIcon(":/icons/icon.svg"));
    }
    return icon;
  }

  void setup_system_tray() {
    QIcon app_icon = create_app_icon();
    setWindowIcon(app_icon);

    if (tray_icon_) {
      tray_icon_->hide();
      delete tray_icon_;
      tray_icon_ = nullptr;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
      return;
    }

    tray_icon_ = new QSystemTrayIcon(app_icon, this);
    tray_icon_->setToolTip("Madí - Model, Align & Diagram Interconnects");

    auto* tray_menu = new QMenu(this);
    auto* act_show = tray_menu->addAction(app_icon, "Show Madí");
    connect(act_show, &QAction::triggered, this, [this]() {
      if (isMinimized()) {
        showNormal();
      }
      show();
      raise();
      activateWindow();
    });

    tray_menu->addSeparator();
    auto* act_new = tray_menu->addAction("New Diagram");
    connect(act_new, &QAction::triggered, this, [this]() { new_diagram(); });
    auto* act_open = tray_menu->addAction("Open Diagram...");
    connect(act_open, &QAction::triggered, this, [this]() { open_diagram(); });
    auto* act_save = tray_menu->addAction("Save Diagram");
    connect(act_save, &QAction::triggered, this, [this]() { save_diagram(); });

    tray_menu->addSeparator();
    auto* act_about = tray_menu->addAction("About Madí...");
    connect(act_about, &QAction::triggered, this, &MainWindow::show_about_dialog);

    tray_menu->addSeparator();
    auto* act_quit = tray_menu->addAction("Quit Madí");
    connect(act_quit, &QAction::triggered, qApp, &QApplication::quit);

    tray_icon_->setContextMenu(tray_menu);

    connect(tray_icon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
      if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isMinimized()) {
          showNormal();
        }
        show();
        raise();
        activateWindow();
      }
    });

    tray_icon_->show();
  }

  void show_about_dialog() {
    AboutMadiDialog dlg(this);
    dlg.exec();
  }

  [[nodiscard]] static QString format_status_hierarchy_path(const QString& raw_path, int max_chars = 38, int max_elements = 3) {
    if (raw_path.isEmpty()) {
      return "Top";
    }

    const QStringList parts = raw_path.split(" / ", Qt::SkipEmptyParts);
    if (parts.size() <= 1) {
      if (raw_path.length() > max_chars) {
        return "..." + raw_path.right(std::max(8, max_chars - 3));
      }
      return raw_path;
    }

    // If it becomes too large (more elements than max_elements, or string length > max_chars):
    // only show the last few elements and start with "... / "
    if (parts.size() > max_elements || raw_path.length() > max_chars) {
      int keep_count = std::min(static_cast<int>(parts.size()) - 1, max_elements);
      if (keep_count < 1) keep_count = 1;

      while (keep_count >= 1) {
        QString candidate = "...";
        for (int i = parts.size() - keep_count; i < parts.size(); ++i) {
          candidate += " / " + parts[i];
        }
        if (candidate.length() <= max_chars || keep_count == 1) {
          if (candidate.length() > max_chars && keep_count == 1) {
            return "... / ..." + parts.last().right(std::max(6, max_chars - 10));
          }
          return candidate;
        }
        --keep_count;
      }
    }

    return raw_path;
  }

  void update_status_hierarchy(const QString& path) {
    const QString display_text = format_status_hierarchy_path(path);
    if (status_hierarchy_label_) {
      status_hierarchy_label_->setText(QString(" 📍 Scope: %1 ").arg(display_text));
      status_hierarchy_label_->setToolTip(QString("Hierarchical Scope: %1\n(Click '⬆ Up' to ascend)").arg(path.isEmpty() ? "Top" : path));
    }
  }

  [[nodiscard]] bool is_remember_settings_accepted(QSettings* custom_settings = nullptr) const {
    if (custom_settings) {
      return custom_settings->value("General/remember_settings_accepted", false).toBool();
    }
    QSettings settings("MADI", "MadiDiagrammer");
    if (settings.contains("General/remember_settings_accepted")) {
      return settings.value("General/remember_settings_accepted", false).toBool();
    }
    QSettings legacy_settings("VECTOR", "VectorDiagrammer");
    return legacy_settings.value("General/remember_settings_accepted", false).toBool();
  }

  [[nodiscard]] bool is_remember_settings_prompted(QSettings* custom_settings = nullptr) const {
    if (custom_settings) {
      return custom_settings->contains("General/remember_settings_accepted");
    }
    QSettings settings("MADI", "MadiDiagrammer");
    if (settings.contains("General/remember_settings_accepted")) {
      return true;
    }
    QSettings legacy_settings("VECTOR", "VectorDiagrammer");
    return legacy_settings.contains("General/remember_settings_accepted");
  }

  void set_remember_settings_accepted(bool accepted, QSettings* custom_settings = nullptr) {
    if (custom_settings) {
      custom_settings->setValue("General/remember_settings_accepted", accepted);
    } else {
      QSettings settings("MADI", "MadiDiagrammer");
      settings.setValue("General/remember_settings_accepted", accepted);
    }
    if (action_remember_settings_) {
      action_remember_settings_->blockSignals(true);
      action_remember_settings_->setChecked(accepted);
      action_remember_settings_->blockSignals(false);
    }
    if (accepted) {
      save_settings(custom_settings);
    }
  }

  void save_settings(QSettings* custom_settings = nullptr) {
    std::unique_ptr<QSettings> local_settings;
    QSettings* s = custom_settings;
    if (!s) {
      local_settings = std::make_unique<QSettings>("MADI", "MadiDiagrammer");
      s = local_settings.get();
    }

    // 1. Window Positions & Geometry & Dock State
    s->setValue("Window/geometry", saveGeometry());
    s->setValue("Window/state", saveState());

    // 2. Colours, Themes & Styles
    const auto& tm = ThemeManager::instance();
    s->setValue("Theme/ui_theme", QString::fromStdString(tm.current_ui_theme().id));
    s->setValue("Theme/diagram_theme", QString::fromStdString(tm.current_diagram_theme().id));

    // 3. Canvas & Visual Aids
    if (canvas_) {
      s->setValue("Canvas/show_rulers", canvas_->show_rulers());
      s->setValue("Canvas/show_line_jumps", canvas_->show_line_jumps());
      s->setValue("Canvas/show_smart_guides", canvas_->show_smart_guides());

      // 4. Page Setup
      s->setValue("Page/preset", static_cast<int>(canvas_->page_preset()));
      s->setValue("Page/orientation", static_cast<int>(canvas_->page_orientation()));
      s->setValue("Page/width", canvas_->page_bounds().width);
      s->setValue("Page/height", canvas_->page_bounds().height);
      s->setValue("Page/margin", canvas_->page_margin());
      s->setValue("Page/show_sheet", canvas_->show_page_sheet());

      // Custom Diagram Style Colors
      const auto& ds = canvas_->diagram_style();
      s->setValue("Theme/canvas_bg", ds.canvas_bg);
      s->setValue("Theme/page_bg", ds.page_bg);
      s->setValue("Theme/page_border", ds.page_border);
      s->setValue("Theme/ruler_bg", ds.ruler_bg);
      s->setValue("Theme/ruler_fg", ds.ruler_fg);
      s->setValue("Theme/ruler_border", ds.ruler_border);
      s->setValue("Theme/jump_bridge_color", ds.jump_bridge_color);
      s->setValue("Theme/guide_color", ds.guide_color);
      s->setValue("Theme/node_fill", ds.render_style.fill_color);
      s->setValue("Theme/node_stroke", ds.render_style.stroke_color);
      s->setValue("Theme/node_text", ds.render_style.text_color);
      s->setValue("Theme/wire_color", ds.render_style.wire_color);
      s->setValue("Theme/selection_border", ds.render_style.selection_border);
    }

    // 5. Grid & Snapping Settings
    if (grid_manager_) {
      s->setValue("SnapGrid/snap_enabled", grid_manager_->is_snap_enabled());
      s->setValue("SnapGrid/snap_strength", static_cast<int>(grid_manager_->snap_strength()));
      s->setValue("SnapGrid/snap_tolerance", grid_manager_->snap_tolerance());
      s->setValue("SnapGrid/snap_flags", static_cast<uint32_t>(grid_manager_->snap_flags()));

      const auto& cfg = grid_manager_->config();
      s->setValue("SnapGrid/snap_type", static_cast<int>(cfg.snap_type));
      s->setValue("SnapGrid/spacing_mode", static_cast<int>(cfg.spacing_mode));
      s->setValue("SnapGrid/snap_spacing_x", cfg.snap_spacing_x);
      s->setValue("SnapGrid/snap_spacing_y", cfg.snap_spacing_y);
      s->setValue("SnapGrid/major_spacing_x", cfg.major_spacing_x);
      s->setValue("SnapGrid/major_spacing_y", cfg.major_spacing_y);
      s->setValue("SnapGrid/subdivisions_x", cfg.subdivisions_x);
      s->setValue("SnapGrid/match_grid_to_snap", cfg.match_grid_to_snap);
      s->setValue("SnapGrid/polar_angle_deg", cfg.polar_angle_deg);
      s->setValue("SnapGrid/visible", cfg.visible);
      s->setValue("SnapGrid/show_subdivisions", cfg.show_subdivisions);
    }

    s->sync();
  }

  void restore_settings(QSettings* custom_settings = nullptr) {
    std::unique_ptr<QSettings> local_settings;
    QSettings* s = custom_settings;
    if (!s) {
      local_settings = std::make_unique<QSettings>("MADI", "MadiDiagrammer");
      if (!local_settings->contains("Window/geometry")) {
        QSettings legacy("VECTOR", "VectorDiagrammer");
        if (legacy.contains("Window/geometry")) {
          local_settings = std::make_unique<QSettings>("VECTOR", "VectorDiagrammer");
        }
      }
      s = local_settings.get();
    }

    // 1. Window Positions & Geometry & Dock State
    if (s->contains("Window/geometry")) {
      restoreGeometry(s->value("Window/geometry").toByteArray());
    }
    if (s->contains("Window/state")) {
      restoreState(s->value("Window/state").toByteArray());
    }

    // 2. Colours, Themes & Styles
    auto& tm = ThemeManager::instance();
    const QString saved_ui = s->value("Theme/ui_theme").toString();
    if (!saved_ui.isEmpty() && tm.find_ui_theme(saved_ui.toStdString())) {
      tm.set_active_ui_theme(saved_ui.toStdString());
    }
    const QString saved_diag = s->value("Theme/diagram_theme").toString();
    if (!saved_diag.isEmpty() && tm.find_diagram_theme(saved_diag.toStdString())) {
      tm.set_active_diagram_theme(saved_diag.toStdString());
      if (canvas_) {
        canvas_->set_diagram_style(to_canvas_style(tm.current_diagram_theme()));
      }
    }

    // 3. Canvas & Visual Aids & Style Overrides
    if (canvas_) {
      if (s->contains("Canvas/show_rulers")) {
        const bool v = s->value("Canvas/show_rulers").toBool();
        canvas_->set_show_rulers(v);
        if (action_toggle_rulers_) action_toggle_rulers_->setChecked(v);
      }
      if (s->contains("Canvas/show_line_jumps")) {
        const bool v = s->value("Canvas/show_line_jumps").toBool();
        canvas_->set_show_line_jumps(v);
        if (action_toggle_line_jumps_) action_toggle_line_jumps_->setChecked(v);
      }
      if (s->contains("Canvas/show_smart_guides")) {
        const bool v = s->value("Canvas/show_smart_guides").toBool();
        canvas_->set_show_smart_guides(v);
        if (action_toggle_smart_guides_) action_toggle_smart_guides_->setChecked(v);
      }

      // Page Setup
      if (s->contains("Page/preset")) {
        const auto preset = static_cast<madi::renderer::PagePreset>(s->value("Page/preset").toInt());
        const auto orient = static_cast<madi::renderer::PageOrientation>(s->value("Page/orientation").toInt());
        if (preset == madi::renderer::PagePreset::Custom) {
          const double w = s->value("Page/width", 1100.0).toDouble();
          const double h = s->value("Page/height", 850.0).toDouble();
          canvas_->set_page_size(w, h);
        } else {
          canvas_->set_page_preset(preset, orient);
        }
      }
      if (s->contains("Page/margin")) {
        canvas_->set_page_margin(s->value("Page/margin").toDouble());
      }
      if (s->contains("Page/show_sheet")) {
        canvas_->set_show_page_sheet(s->value("Page/show_sheet").toBool());
      }

      // Restore custom diagram colors if saved
      auto ds = canvas_->diagram_style();
      if (s->contains("Theme/canvas_bg")) ds.canvas_bg = s->value("Theme/canvas_bg").toUInt();
      if (s->contains("Theme/page_bg")) ds.page_bg = s->value("Theme/page_bg").toUInt();
      if (s->contains("Theme/page_border")) ds.page_border = s->value("Theme/page_border").toUInt();
      if (s->contains("Theme/ruler_bg")) ds.ruler_bg = s->value("Theme/ruler_bg").toUInt();
      if (s->contains("Theme/ruler_fg")) ds.ruler_fg = s->value("Theme/ruler_fg").toUInt();
      if (s->contains("Theme/ruler_border")) ds.ruler_border = s->value("Theme/ruler_border").toUInt();
      if (s->contains("Theme/jump_bridge_color")) ds.jump_bridge_color = s->value("Theme/jump_bridge_color").toUInt();
      if (s->contains("Theme/guide_color")) ds.guide_color = s->value("Theme/guide_color").toUInt();
      if (s->contains("Theme/node_fill")) ds.render_style.fill_color = s->value("Theme/node_fill").toUInt();
      if (s->contains("Theme/node_stroke")) ds.render_style.stroke_color = s->value("Theme/node_stroke").toUInt();
      if (s->contains("Theme/node_text")) ds.render_style.text_color = s->value("Theme/node_text").toUInt();
      if (s->contains("Theme/wire_color")) ds.render_style.wire_color = s->value("Theme/wire_color").toUInt();
      if (s->contains("Theme/selection_border")) ds.render_style.selection_border = s->value("Theme/selection_border").toUInt();
      canvas_->set_diagram_style(ds);
    }

    // 4. Grid & Snapping Settings
    if (grid_manager_) {
      auto cfg = grid_manager_->config();
      if (s->contains("SnapGrid/snap_type")) {
        cfg.snap_type = static_cast<madi::grid::SnapType>(s->value("SnapGrid/snap_type").toInt());
      }
      if (s->contains("SnapGrid/spacing_mode")) {
        cfg.spacing_mode = static_cast<madi::grid::GridSpacingMode>(s->value("SnapGrid/spacing_mode").toInt());
      }
      if (s->contains("SnapGrid/snap_spacing_x")) {
        cfg.snap_spacing_x = s->value("SnapGrid/snap_spacing_x").toDouble();
      }
      if (s->contains("SnapGrid/snap_spacing_y")) {
        cfg.snap_spacing_y = s->value("SnapGrid/snap_spacing_y").toDouble();
      }
      if (s->contains("SnapGrid/major_spacing_x")) {
        cfg.major_spacing_x = s->value("SnapGrid/major_spacing_x").toDouble();
      }
      if (s->contains("SnapGrid/major_spacing_y")) {
        cfg.major_spacing_y = s->value("SnapGrid/major_spacing_y").toDouble();
      }
      if (s->contains("SnapGrid/subdivisions_x")) {
        cfg.subdivisions_x = s->value("SnapGrid/subdivisions_x").toUInt();
      }
      if (s->contains("SnapGrid/match_grid_to_snap")) {
        cfg.match_grid_to_snap = s->value("SnapGrid/match_grid_to_snap").toBool();
      }
      if (s->contains("SnapGrid/polar_angle_deg")) {
        cfg.polar_angle_deg = s->value("SnapGrid/polar_angle_deg").toDouble();
      }
      if (s->contains("SnapGrid/visible")) {
        cfg.visible = s->value("SnapGrid/visible").toBool();
      }
      if (s->contains("SnapGrid/show_subdivisions")) {
        cfg.show_subdivisions = s->value("SnapGrid/show_subdivisions").toBool();
      }
      grid_manager_->set_config(cfg);

      if (s->contains("SnapGrid/snap_flags")) {
        grid_manager_->set_snap_flags(static_cast<madi::grid::SnapFlags>(s->value("SnapGrid/snap_flags").toUInt()));
      }
      if (s->contains("SnapGrid/snap_strength")) {
        grid_manager_->set_snap_strength(static_cast<madi::grid::SnapStrength>(s->value("SnapGrid/snap_strength").toInt()));
      }
      if (s->contains("SnapGrid/snap_tolerance")) {
        grid_manager_->set_snap_tolerance(s->value("SnapGrid/snap_tolerance").toDouble());
      }
      if (s->contains("SnapGrid/snap_enabled")) {
        grid_manager_->set_snap_enabled(s->value("SnapGrid/snap_enabled").toBool());
      }
    }

    update_snap_ui();
    rebuild_theme_menus();
    update_tool_icons();
    if (action_remember_settings_) {
      action_remember_settings_->blockSignals(true);
      action_remember_settings_->setChecked(true);
      action_remember_settings_->blockSignals(false);
    }
    if (canvas_) canvas_->update();
  }

  void reset_layout() {
    resize(1360, 860);
    if (dock_palette_) {
      dock_palette_->setFloating(false);
      addDockWidget(Qt::LeftDockWidgetArea, dock_palette_);
      dock_palette_->show();
    }
    if (stencil_dock_) {
      stencil_dock_->setFloating(false);
      addDockWidget(Qt::LeftDockWidgetArea, stencil_dock_);
      stencil_dock_->show();
    }
    if (hierarchy_dock_) {
      hierarchy_dock_->setFloating(false);
      addDockWidget(Qt::LeftDockWidgetArea, hierarchy_dock_);
      hierarchy_dock_->show();
    }
    if (dock_inspector_) {
      dock_inspector_->setFloating(false);
      addDockWidget(Qt::RightDockWidgetArea, dock_inspector_);
      dock_inspector_->show();
    }
    if (console_dock_) {
      console_dock_->setFloating(false);
      addDockWidget(Qt::BottomDockWidgetArea, console_dock_);
      console_dock_->show();
    }
    for (auto* tb : {toolbar_file_, toolbar_draw_, toolbar_operations_, toolbar_format_, toolbar_view_, toolbar_aids_, toolbar_hierarchy_}) {
      if (tb) {
        addToolBar(Qt::TopToolBarArea, tb);
        tb->setVisible(true);
      }
    }
    if (canvas_) canvas_->reset_view();
    statusBar()->showMessage("Window layout reset to default.", 3000);
  }

protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::BackButton ||
        event->button() == Qt::ExtraButton1 ||
        event->button() == Qt::ExtraButton8) {
      if (canvas_) canvas_->undo();
      event->accept();
      return;
    }
    if (event->button() == Qt::ForwardButton ||
        event->button() == Qt::ExtraButton2 ||
        event->button() == Qt::ExtraButton9) {
      if (canvas_) canvas_->redo();
      event->accept();
      return;
    }
    QMainWindow::mousePressEvent(event);
  }

  void closeEvent(QCloseEvent* event) override {
    if (!maybe_save()) {
      event->ignore();
      return;
    }

    if (!is_remember_settings_prompted()) {
      const bool is_offscreen = QGuiApplication::platformName().contains("offscreen");
      if (is_offscreen) {
        set_remember_settings_accepted(false);
      } else {
        const QMessageBox::StandardButton res = QMessageBox::question(
            this,
            "Remember Workspace Settings",
            "Would you like Madí to remember your window positions, dock layout, themes, colors, and settings for future sessions?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        const bool accepted = (res == QMessageBox::Yes);
        set_remember_settings_accepted(accepted);
      }
    } else if (is_remember_settings_accepted()) {
      save_settings();
    }
    event->accept();
  }

private:
  static madi::renderer::CanvasWidget::DiagramStyle to_canvas_style(const DiagramTheme& dt) {
    madi::renderer::CanvasWidget::DiagramStyle cs;
    cs.canvas_bg = dt.canvas_bg;
    cs.page_bg = dt.page_bg;
    cs.page_border = dt.page_border;
    cs.ruler_bg = dt.ruler_bg;
    cs.ruler_fg = dt.ruler_fg;
    cs.ruler_border = dt.ruler_border;
    cs.jump_bridge_color = dt.jump_bridge_color;
    cs.guide_color = dt.guide_color;
    cs.render_style = dt.to_render_style();
    return cs;
  }

  void create_actions() {
    // File Actions
    action_new_ = new QAction("&New Diagram", this);
    action_new_->setShortcut(QKeySequence::New);
    connect(action_new_, &QAction::triggered, this, [this]() { new_diagram(); });

    action_open_ = new QAction("&Open Diagram...", this);
    action_open_->setShortcut(QKeySequence::Open);
    connect(action_open_, &QAction::triggered, this, [this]() { open_diagram(); });

    action_save_ = new QAction("&Save Diagram", this);
    action_save_->setShortcut(QKeySequence::Save);
    connect(action_save_, &QAction::triggered, this, [this]() { save_diagram(); });

    action_save_as_ = new QAction("Save Diagram &As...", this);
    action_save_as_->setShortcut(QKeySequence::SaveAs);
    connect(action_save_as_, &QAction::triggered, this, [this]() { save_diagram_as(); });

    action_export_drawio_ = new QAction("Export &Draw.io XML...", this);
    connect(action_export_drawio_, &QAction::triggered, this, &MainWindow::export_drawio);

    action_export_svg_ = new QAction("Export &SVG (Scalable Vector Graphics)...", this);
    action_export_svg_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(action_export_svg_, &QAction::triggered, this, &MainWindow::export_svg);

    action_export_png_ = new QAction("Export &PNG Raster Image...", this);
    action_export_png_->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(action_export_png_, &QAction::triggered, this, &MainWindow::export_png);

    action_export_jpeg_ = new QAction("Export &JPEG Image...", this);
    connect(action_export_jpeg_, &QAction::triggered, this, &MainWindow::export_jpeg);

    action_export_yaml_ = new QAction("Export &YAML...", this);
    connect(action_export_yaml_, &QAction::triggered, this, &MainWindow::save_diagram);

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

    action_cut_ = new QAction("Cu&t", this);
    action_cut_->setShortcut(QKeySequence::Cut);
    action_cut_->setEnabled(false);
    connect(action_cut_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::cut_selected);

    action_copy_ = new QAction("&Copy", this);
    action_copy_->setShortcut(QKeySequence::Copy);
    action_copy_->setEnabled(false);
    connect(action_copy_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::copy_selected);

    action_paste_ = new QAction("&Paste", this);
    action_paste_->setShortcut(QKeySequence::Paste);
    connect(action_paste_, &QAction::triggered, canvas_, [this]() {
      canvas_->paste_clipboard();
    });

    action_duplicate_ = new QAction("&Duplicate", this);
    action_duplicate_->setShortcut(QKeySequence("Ctrl+D"));
    action_duplicate_->setEnabled(false);
    connect(action_duplicate_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::duplicate_selected);

    action_delete_ = new QAction("&Delete", this);
    action_delete_->setShortcut(QKeySequence::Delete);
    connect(action_delete_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::delete_selected);

    action_select_all_ = new QAction("Select &All", this);
    action_select_all_->setShortcut(QKeySequence::SelectAll);
    connect(action_select_all_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::select_all);

    // 2D Boolean Logic Operations & Merging
    action_merge_ = new QAction("&Union / OR (Merge)", this);
    action_merge_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    action_merge_->setToolTip("Union / OR (Ctrl+G)\nCombine selected figures into a unified polygon");
    action_merge_->setStatusTip("Union (OR) selected figures into a unified polygon (Ctrl+G)");
    action_merge_->setEnabled(false);
    connect(action_merge_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::merge_selected_figures);
    action_bool_union_ = action_merge_;

    action_bool_intersect_ = new QAction("&Intersection (AND)", this);
    action_bool_intersect_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    action_bool_intersect_->setToolTip("Intersection / AND (Ctrl+Shift+I)\nKeep overlapping region of selected figures");
    action_bool_intersect_->setStatusTip("Compute intersection (AND) of selected figures (Ctrl+Shift+I)");
    action_bool_intersect_->setEnabled(false);
    connect(action_bool_intersect_, &QAction::triggered, canvas_, [this]() {
      canvas_->boolean_combine_selected(madi::core::BooleanOp::Intersection);
    });

    action_bool_subtract_ = new QAction("&Subtract (Difference)", this);
    action_bool_subtract_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    action_bool_subtract_->setToolTip("Subtract / Difference (Ctrl+Shift+D)\nCut subsequent figures out of the first selected figure");
    action_bool_subtract_->setStatusTip("Subtract subsequent figures from the first figure (Ctrl+Shift+D)");
    action_bool_subtract_->setEnabled(false);
    connect(action_bool_subtract_, &QAction::triggered, canvas_, [this]() {
      canvas_->boolean_combine_selected(madi::core::BooleanOp::Difference);
    });

    action_bool_xor_ = new QAction("&XOR (Symmetric Difference)", this);
    action_bool_xor_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
    action_bool_xor_->setToolTip("XOR / Symmetric Difference (Ctrl+Shift+X)\nKeep regions that belong to either figure but not both");
    action_bool_xor_->setStatusTip("Compute XOR / symmetric difference of selected figures (Ctrl+Shift+X)");
    action_bool_xor_->setEnabled(false);
    connect(action_bool_xor_, &QAction::triggered, canvas_, [this]() {
      canvas_->boolean_combine_selected(madi::core::BooleanOp::Xor);
    });

    action_split_ = new QAction("&Split Figure", this);
    action_split_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    action_split_->setToolTip("Split Figure (Ctrl+Shift+G)\nUngroup composite figure into constituent elements");
    action_split_->setStatusTip("Split composite figure into constituent elements (Ctrl+Shift+G)");
    action_split_->setEnabled(false);
    connect(action_split_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::split_selected_figure);

    // Stacking Order (Z-Order) Actions
    action_bring_to_front_ = new QAction("Bring to &Front", this);
    action_bring_to_front_->setShortcuts({QKeySequence("Ctrl+Shift+]"), QKeySequence("Ctrl+Shift+F")});
    action_bring_to_front_->setToolTip("Bring to Front (Ctrl+Shift+] / Ctrl+Shift+F)\nMove selected figure(s) to the very top of the stacking order");
    action_bring_to_front_->setStatusTip("Bring selected figure(s) to the front of the diagram (Ctrl+Shift+F)");
    action_bring_to_front_->setEnabled(false);
    connect(action_bring_to_front_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::bring_to_front_selected);

    action_bring_forward_ = new QAction("Bring &Forward", this);
    action_bring_forward_->setShortcuts({QKeySequence("Ctrl+]"), QKeySequence("Ctrl+F")});
    action_bring_forward_->setToolTip("Bring Forward (Ctrl+] / Ctrl+F)\nMove selected figure(s) one step up in the stacking order");
    action_bring_forward_->setStatusTip("Bring selected figure(s) one step forward (Ctrl+F)");
    action_bring_forward_->setEnabled(false);
    connect(action_bring_forward_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::bring_forward_selected);

    action_send_backward_ = new QAction("Send &Backward", this);
    action_send_backward_->setShortcut(QKeySequence("Ctrl+["));
    action_send_backward_->setToolTip("Send Backward (Ctrl+[)\nMove selected figure(s) one step down in the stacking order");
    action_send_backward_->setStatusTip("Send selected figure(s) one step backward (Ctrl+[)");
    action_send_backward_->setEnabled(false);
    connect(action_send_backward_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::send_backward_selected);

    action_send_to_back_ = new QAction("Send to &Back", this);
    action_send_to_back_->setShortcuts({QKeySequence("Ctrl+Shift+["), QKeySequence("Ctrl+Shift+B")});
    action_send_to_back_->setToolTip("Send to Back (Ctrl+Shift+[ / Ctrl+Shift+B)\nMove selected figure(s) to the very bottom of the stacking order");
    action_send_to_back_->setStatusTip("Send selected figure(s) to the back of the diagram (Ctrl+Shift+B)");
    action_send_to_back_->setEnabled(false);
    connect(action_send_to_back_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::send_to_back_selected);

    // Tool Actions
    tool_action_group_ = new QActionGroup(this);
    tool_action_group_->setExclusive(true);

    action_tool_select_ = new QAction("&Select / Pointer", tool_action_group_);
    action_tool_select_->setCheckable(true);
    action_tool_select_->setChecked(true);
    action_tool_select_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_1), QKeySequence(Qt::Key_S), QKeySequence(Qt::Key_V)});
    action_tool_select_->setToolTip("Select / Pointer Tool (Ctrl+1 / S / V)\nPick, move, and edit figures on canvas. Esc returns to Select.");
    action_tool_select_->setStatusTip("Select tool: Pick, move, and edit figures on canvas (Ctrl+1 / S / V)");
    connect(action_tool_select_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::Select);
    });

    action_tool_rect_ = new QAction("&Rectangle Block", tool_action_group_);
    action_tool_rect_->setCheckable(true);
    action_tool_rect_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_8), QKeySequence(Qt::Key_R)});
    action_tool_rect_->setToolTip("Rectangle Block (Ctrl+8 / R)\nInsert or drag-draw rectangular blocks");
    action_tool_rect_->setStatusTip("Rectangle Block: Insert or drag-draw rectangular blocks (Ctrl+8 / R)");
    connect(action_tool_rect_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddRectangle);
    });

    action_tool_rounded_ = new QAction("R&ounded Figure", tool_action_group_);
    action_tool_rounded_->setCheckable(true);
    action_tool_rounded_->setShortcuts({QKeySequence(Qt::Key_U)});
    action_tool_rounded_->setToolTip("Rounded Figure (U)\nInsert or drag-draw rounded rectangles");
    action_tool_rounded_->setStatusTip("Rounded Figure: Insert or drag-draw rounded rectangles (U)");
    connect(action_tool_rounded_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddRoundedRect);
    });

    action_tool_ellipse_ = new QAction("&State Ellipse", tool_action_group_);
    action_tool_ellipse_->setCheckable(true);
    action_tool_ellipse_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_9), QKeySequence(Qt::Key_E), QKeySequence(Qt::Key_O)});
    action_tool_ellipse_->setToolTip("State Ellipse (Ctrl+9 / E / O)\nInsert or drag-draw ellipses and circles");
    action_tool_ellipse_->setStatusTip("State Ellipse: Insert or drag-draw ellipses and circles (Ctrl+9 / E / O)");
    connect(action_tool_ellipse_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddEllipse);
    });

    action_tool_polygon_ = new QAction("&Polygon", tool_action_group_);
    action_tool_polygon_->setCheckable(true);
    action_tool_polygon_->setShortcuts({QKeySequence(Qt::Key_P)});
    action_tool_polygon_->setToolTip("Polygon (P)\nClick points on canvas to draw polygon, right-click to finish");
    action_tool_polygon_->setStatusTip("Polygon: Click points on canvas to draw polygon, right-click to finish (P)");
    connect(action_tool_polygon_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddPolygon);
    });

    action_tool_block_ = new QAction("Subsystem &Block", tool_action_group_);
    action_tool_block_->setCheckable(true);
    action_tool_block_->setShortcuts({QKeySequence(Qt::Key_H)});
    action_tool_block_->setToolTip("Hierarchical Subsystem Block (H)\nCreate container with isolated internal diagram");
    action_tool_block_->setStatusTip("Hierarchical Subsystem Block: Container with isolated internal diagram (H)");
    connect(action_tool_block_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddHierarchicalBlock);
    });

    action_tool_wire_ = new QAction("&Wire / Connector", tool_action_group_);
    action_tool_wire_->setCheckable(true);
    action_tool_wire_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_3), QKeySequence(Qt::Key_C), QKeySequence(Qt::Key_W)});
    action_tool_wire_->setToolTip("Wire / Connector Tool (Ctrl+3 / C / W)\nRoute orthogonal obstacle-avoiding connections");
    action_tool_wire_->setStatusTip("Wire / Connector: Route orthogonal obstacle-avoiding connections (Ctrl+3 / C / W)");
    connect(action_tool_wire_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddConnector);
    });

    action_tool_inport_ = new QAction("&Input Port", tool_action_group_);
    action_tool_inport_->setCheckable(true);
    action_tool_inport_->setShortcuts({QKeySequence(Qt::Key_I)});
    action_tool_inport_->setToolTip("Hierarchical Input Port (I)\nAdd input port automatically synced to symbol");
    action_tool_inport_->setStatusTip("Hierarchical Input Port: Add input port automatically synced to symbol (I)");
    connect(action_tool_inport_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddInputPort);
    });

    action_tool_outport_ = new QAction("&Output Port", tool_action_group_);
    action_tool_outport_->setCheckable(true);
    action_tool_outport_->setShortcuts({QKeySequence(Qt::Key_O)});
    action_tool_outport_->setToolTip("Hierarchical Output Port (O)\nAdd output port automatically synced to symbol");
    action_tool_outport_->setStatusTip("Hierarchical Output Port: Add output port automatically synced to symbol (O)");
    connect(action_tool_outport_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddOutputPort);
    });

    action_tool_biport_ = new QAction("&Bidirectional Port", tool_action_group_);
    action_tool_biport_->setCheckable(true);
    action_tool_biport_->setShortcuts({QKeySequence(Qt::Key_B)});
    action_tool_biport_->setToolTip("Hierarchical Bidirectional Port (B)\nAdd inout boundary port automatically synced to symbol");
    action_tool_biport_->setStatusTip("Hierarchical Bidirectional Port: Add inout boundary port automatically synced to symbol (B)");
    connect(action_tool_biport_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddBidirectionalPort);
    });

    action_tool_multipoint_arrow_ = new QAction("&Multi-Point Arrow", tool_action_group_);
    action_tool_multipoint_arrow_->setCheckable(true);
    action_tool_multipoint_arrow_->setShortcuts({QKeySequence(Qt::SHIFT | Qt::Key_W), QKeySequence(Qt::SHIFT | Qt::Key_C)});
    action_tool_multipoint_arrow_->setToolTip("Multi-Point Arrow Connector (Shift+W)\nDraw polyline arrows with multiple waypoints and arrowhead");
    action_tool_multipoint_arrow_->setStatusTip("Multi-Point Arrow: Draw polyline arrows with multiple waypoints (Shift+W)");
    connect(action_tool_multipoint_arrow_, &QAction::triggered, this, [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddMultiPointArrow);
    });

    connect(canvas_, &madi::renderer::CanvasWidget::tool_mode_changed,
            this, [this](madi::renderer::ToolMode mode) {
      if (mode == madi::renderer::ToolMode::Select) {
        action_tool_select_->setChecked(true);
        if (btn_palette_select_) btn_palette_select_->setChecked(true);
        if (btn_palette_wire_) btn_palette_wire_->setChecked(false);
      } else if (mode == madi::renderer::ToolMode::AddConnector) {
        action_tool_wire_->setChecked(true);
        if (btn_palette_select_) btn_palette_select_->setChecked(false);
        if (btn_palette_wire_) btn_palette_wire_->setChecked(true);
      } else {
        if (btn_palette_select_) btn_palette_select_->setChecked(false);
        if (btn_palette_wire_) btn_palette_wire_->setChecked(false);
      }

      if (mode == madi::renderer::ToolMode::AddRectangle) action_tool_rect_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddRoundedRect) action_tool_rounded_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddEllipse) action_tool_ellipse_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddPolygon) action_tool_polygon_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddHierarchicalBlock) action_tool_block_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddInputPort) action_tool_inport_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddOutputPort) action_tool_outport_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddBidirectionalPort) action_tool_biport_->setChecked(true);
      else if (mode == madi::renderer::ToolMode::AddMultiPointArrow) action_tool_multipoint_arrow_->setChecked(true);
    });

    // View Actions
    action_zoom_in_ = new QAction("Zoom &In", this);
    action_zoom_in_->setShortcut(QKeySequence::ZoomIn);
    connect(action_zoom_in_, &QAction::triggered, this, [this]() {
      canvas_->zoom_at(madi::core::Vec2d{canvas_->width() * 0.5, canvas_->height() * 0.5}, 1.25);
      canvas_->update();
    });

    action_zoom_out_ = new QAction("Zoom &Out", this);
    action_zoom_out_->setShortcut(QKeySequence::ZoomOut);
    connect(action_zoom_out_, &QAction::triggered, this, [this]() {
      canvas_->zoom_at(madi::core::Vec2d{canvas_->width() * 0.5, canvas_->height() * 0.5}, 0.8);
      canvas_->update();
    });

    action_fit_view_ = new QAction("&Fit Content to Window", this);
    action_fit_view_->setShortcuts({
        QKeySequence(Qt::Key_F3),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W),
        QKeySequence(Qt::CTRL | Qt::Key_0)
    });
    action_fit_view_->setToolTip("Fit Content to Window (F3 / Ctrl+Shift+W / Ctrl+0)\nZoom viewport so all figures fit on screen");
    connect(action_fit_view_, &QAction::triggered, this, [this]() {
      canvas_->fit_content();
      canvas_->update();
    });

    action_reset_view_ = new QAction("&Reset View (100%)", this);
    action_reset_view_->setShortcuts({
        QKeySequence(Qt::CTRL | Qt::Key_Home),
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0)
    });
    action_reset_view_->setToolTip("Reset View (Ctrl+Home / Ctrl+Alt+0)\nRestore 100% zoom and center origin");
    connect(action_reset_view_, &QAction::triggered, this, [this]() {
      canvas_->reset_view();
      canvas_->update();
    });

    action_toggle_grid_ = new QAction("Show &Grid", this);
    action_toggle_grid_->setCheckable(true);
    action_toggle_grid_->setChecked(true);
    action_toggle_grid_->setShortcuts({
        QKeySequence(Qt::SHIFT | Qt::Key_F9),
        QKeySequence(Qt::SHIFT | Qt::Key_G),
        QKeySequence(Qt::CTRL | Qt::Key_Apostrophe)
    });
    action_toggle_grid_->setToolTip("Show Grid (Shift+F9 / Shift+G / Ctrl+')\nToggle diagram grid visibility");
    action_toggle_grid_->setStatusTip("Toggle diagram grid visibility (Shift+F9 / Shift+G)");
    connect(action_toggle_grid_, &QAction::toggled, this, [this](bool checked) {
      auto cfg = grid_manager_->config();
      cfg.visible = checked;
      grid_manager_->set_config(cfg);
      canvas_->update();
    });

    action_toggle_snap_ = new QAction("Enable &Snapping", this);
    action_toggle_snap_->setCheckable(true);
    action_toggle_snap_->setChecked(true);
    action_toggle_snap_->setShortcuts({
        QKeySequence(Qt::Key_F9),
        QKeySequence(Qt::CTRL | Qt::Key_B),
        QKeySequence(Qt::SHIFT | Qt::Key_S)
    });
    connect(action_toggle_snap_, &QAction::toggled, this, [this](bool checked) {
      grid_manager_->set_snap_enabled(checked);
      update_snap_ui();
    });

    action_toggle_angle_snap_ = new QAction("Enable &Angle Snapping", this);
    action_toggle_angle_snap_->setCheckable(true);
    action_toggle_angle_snap_->setChecked(canvas_ ? canvas_->is_angle_snapping_enabled() : true);
    action_toggle_angle_snap_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_A));
    action_toggle_angle_snap_->setToolTip("Angle Snapping (Alt+A): Snap figure rotation to angular increments (15° default). Hold Alt for free rotation.");
    action_toggle_angle_snap_->setStatusTip("Toggle angle snapping during figure rotation (Alt+A)");
    action_toggle_angle_snap_->setIcon(IconFactory::create_icon(IconFactory::IconType::AngleSnap, QColor(0x38, 0xBD, 0xF8)));
    connect(action_toggle_angle_snap_, &QAction::toggled, this, [this](bool checked) {
      if (canvas_) {
        canvas_->set_angle_snapping_enabled(checked);
        update_angle_snap_ui();
      }
    });

    action_snap_settings_ = new QAction("Snap & Grid Settings...", this);
    action_snap_settings_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F9));
    action_snap_settings_->setToolTip("Configure Snap & Grid Settings (Alt+F9)");
    connect(action_snap_settings_, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(0);
    });

    action_toggle_rulers_ = new QAction("Show &Rulers", this);
    action_toggle_rulers_->setCheckable(true);
    action_toggle_rulers_->setChecked(true);
    action_toggle_rulers_->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_R));
    connect(action_toggle_rulers_, &QAction::toggled, canvas_, &madi::renderer::CanvasWidget::set_show_rulers);

    action_toggle_line_jumps_ = new QAction("Show &Line Jumps (Bridges)", this);
    action_toggle_line_jumps_->setCheckable(true);
    action_toggle_line_jumps_->setChecked(true);
    connect(action_toggle_line_jumps_, &QAction::toggled, canvas_, &madi::renderer::CanvasWidget::set_show_line_jumps);

    action_toggle_segment_moving_ = new QAction("Enable &Edge Segment Moving", this);
    action_toggle_segment_moving_->setCheckable(true);
    action_toggle_segment_moving_->setChecked(false);
    action_toggle_segment_moving_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_S));
    action_toggle_segment_moving_->setToolTip("Enable Edge Segment Moving (Alt+S). When disabled, hold Shift to drag edge segments.");
    action_toggle_segment_moving_->setStatusTip("Toggle edge segment moving on shapes and arrows (Alt+S). Hold Shift to move segments temporarily.");
    connect(action_toggle_segment_moving_, &QAction::toggled, this, [this](bool checked) {
      if (canvas_) {
        canvas_->set_segment_moving_enabled(checked);
        statusBar()->showMessage(checked ? "Edge segment moving enabled" : "Edge segment moving disabled (Hold Shift to move)", 3000);
      }
    });

    action_toggle_smart_guides_ = new QAction("Show &Smart Alignment Guides", this);
    action_toggle_smart_guides_->setCheckable(true);
    action_toggle_smart_guides_->setChecked(true);
    connect(action_toggle_smart_guides_, &QAction::toggled, canvas_, &madi::renderer::CanvasWidget::set_show_smart_guides);

    // Hierarchy Actions
    action_drill_down_ = new QAction("&Drill Down (Push)", this);
    action_drill_down_->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter), QKeySequence(Qt::CTRL | Qt::Key_Down)});
    action_drill_down_->setToolTip("Drill Down (Enter / Ctrl+Down)\nDescend into selected figure / subsystem internal view");
    action_drill_down_->setStatusTip("Drill Down: Descend into selected figure / subsystem internal view (Enter)");
    action_drill_down_->setEnabled(false);
    connect(action_drill_down_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::drill_down_selected);

    action_pop_scope_ = new QAction("&Up to Parent (Pop)", this);
    action_pop_scope_->setShortcuts({QKeySequence("Alt+Up"), QKeySequence(Qt::CTRL | Qt::Key_Up)});
    action_pop_scope_->setToolTip("Up to Parent (Alt+Up / Ctrl+Up)\nReturn to parent diagram hierarchy level");
    action_pop_scope_->setStatusTip("Up to Parent: Return to parent diagram hierarchy level (Alt+Up)");
    action_pop_scope_->setEnabled(false);
    connect(action_pop_scope_, &QAction::triggered, canvas_, &madi::renderer::CanvasWidget::pop_hierarchy_scope);

    // Workspace & Session Persistence Actions
    action_remember_settings_ = new QAction("&Remember Window Positions & Settings", this);
    action_remember_settings_->setCheckable(true);
    action_remember_settings_->setChecked(is_remember_settings_accepted());
    action_remember_settings_->setToolTip("Save window geometry, dock layout, colors, and themes across sessions");
    connect(action_remember_settings_, &QAction::toggled, this, [this](bool checked) {
      set_remember_settings_accepted(checked);
      statusBar()->showMessage(checked
          ? "Window positions, layout, themes, and settings will be remembered."
          : "Session persistence disabled.", 3000);
    });

    action_reset_layout_ = new QAction("Reset Window &Layout to Default", this);
    action_reset_layout_->setToolTip("Reset window size and dock positions to default layout");
    connect(action_reset_layout_, &QAction::triggered, this, [this]() {
      reset_layout();
    });
  }

  void create_menus() {
    // 1. File Menu
    auto* menu_file = menuBar()->addMenu("&File");
    menu_file->addAction(action_new_);
    menu_file->addAction(action_open_);
    menu_file->addAction(action_save_);
    menu_file->addAction(action_save_as_);
    menu_file->addSeparator();

    auto* act_page_setup = menu_file->addAction("Page &Setup...");
    connect(act_page_setup, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(3);
    });
    menu_file->addSeparator();

    auto* menu_export = menu_file->addMenu("&Export Diagram");
    menu_export->addAction(action_export_drawio_);
    menu_export->addAction(action_export_svg_);
    menu_export->addAction(action_export_png_);
    menu_export->addAction(action_export_jpeg_);
    menu_export->addAction(action_export_yaml_);
    menu_file->addSeparator();

    menu_file->addAction(action_exit_);

    // 2. Edit Menu
    auto* menu_edit = menuBar()->addMenu("&Edit");
    menu_edit->addAction(action_undo_);
    menu_edit->addAction(action_redo_);
    menu_edit->addSeparator();
    menu_edit->addAction(action_cut_);
    menu_edit->addAction(action_copy_);
    menu_edit->addAction(action_paste_);
    menu_edit->addAction(action_duplicate_);
    menu_edit->addSeparator();

    auto* menu_bool = menu_edit->addMenu("&Boolean Operations");
    menu_bool->addAction(action_merge_);
    menu_bool->addAction(action_bool_intersect_);
    menu_bool->addAction(action_bool_subtract_);
    menu_bool->addAction(action_bool_xor_);
    menu_edit->addAction(action_split_);
    menu_edit->addSeparator();

    auto* menu_order = menu_edit->addMenu("&Order");
    menu_order->addAction(action_bring_to_front_);
    menu_order->addAction(action_bring_forward_);
    menu_order->addAction(action_send_backward_);
    menu_order->addAction(action_send_to_back_);
    menu_edit->addSeparator();

    menu_edit->addAction(action_select_all_);
    auto* act_deselect = menu_edit->addAction("&Deselect All / Cancel Tool");
    act_deselect->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(act_deselect, &QAction::triggered, this, [this]() {
      if (canvas_) {
        canvas_->clear_selection();
        if (canvas_->tool_mode() != madi::renderer::ToolMode::Select) {
          canvas_->set_tool_mode(madi::renderer::ToolMode::Select);
        }
      }
    });
    menu_edit->addAction(action_delete_);

    // 3. Insert Menu
    auto* menu_insert = menuBar()->addMenu("&Insert");
    auto* menu_ins_tools = menu_insert->addMenu("&Tools");
    menu_ins_tools->addAction(action_tool_select_);
    menu_ins_tools->addAction(action_tool_wire_);
    menu_ins_tools->addAction(action_tool_multipoint_arrow_);
    auto* act_branch = menu_ins_tools->addAction("Add &Multipoint Branch");
    connect(act_branch, &QAction::triggered, this, [this]() {
      if (!canvas_ || !graph_) return;
      const auto& sel_nodes = canvas_->selected_nodes();
      if (!sel_nodes.empty()) {
        canvas_->add_arrow_branch_to_selected(sel_nodes.front());
      } else {
        statusBar()->showMessage("Select an arrow, then select a destination node to branch to", 3000);
      }
    });

    menu_insert->addSeparator();
    auto* menu_ins_shapes = menu_insert->addMenu("&2D Figures");
    menu_ins_shapes->addAction(action_tool_rect_);
    menu_ins_shapes->addAction(action_tool_rounded_);
    menu_ins_shapes->addAction(action_tool_ellipse_);
    menu_ins_shapes->addAction(action_tool_polygon_);

    menu_insert->addSeparator();
    auto* menu_ins_hierarchy = menu_insert->addMenu("&Hierarchy Elements");
    menu_ins_hierarchy->addAction(action_tool_block_);
    menu_ins_hierarchy->addAction(action_tool_inport_);
    menu_ins_hierarchy->addAction(action_tool_outport_);
    menu_ins_hierarchy->addAction(action_tool_biport_);

    // 4. View Menu
    auto* menu_view = menuBar()->addMenu("&View");

    // 4a. Zoom & Navigation
    menu_view->addAction(action_zoom_in_);
    menu_view->addAction(action_zoom_out_);
    menu_view->addAction(action_fit_view_);
    menu_view->addAction(action_reset_view_);
    menu_view->addSeparator();

    // 4b. Toolbars Submenu (Each sub-section can be shown or hidden)
    auto* menu_toolbars = menu_view->addMenu("&Toolbars");
    if (toolbar_file_) menu_toolbars->addAction(toolbar_file_->toggleViewAction());
    if (toolbar_draw_) menu_toolbars->addAction(toolbar_draw_->toggleViewAction());
    if (toolbar_operations_) menu_toolbars->addAction(toolbar_operations_->toggleViewAction());
    if (toolbar_format_) menu_toolbars->addAction(toolbar_format_->toggleViewAction());
    if (toolbar_view_) menu_toolbars->addAction(toolbar_view_->toggleViewAction());
    if (toolbar_aids_) menu_toolbars->addAction(toolbar_aids_->toggleViewAction());
    if (toolbar_hierarchy_) menu_toolbars->addAction(toolbar_hierarchy_->toggleViewAction());

    // 4c. Tool Windows / Docks Submenu
    auto* menu_docks = menu_view->addMenu("Tool &Windows");
    if (dock_palette_) menu_docks->addAction(dock_palette_->toggleViewAction());
    if (stencil_dock_) menu_docks->addAction(stencil_dock_->toggleViewAction());
    if (hierarchy_dock_) menu_docks->addAction(hierarchy_dock_->toggleViewAction());
    if (dock_inspector_) menu_docks->addAction(dock_inspector_->toggleViewAction());
    if (console_dock_) menu_docks->addAction(console_dock_->toggleViewAction());
    menu_docks->addSeparator();
    action_focus_console_ = new QAction("&Command Console", this);
    action_focus_console_->setShortcut(QKeySequence(Qt::Key_F2));
    action_focus_console_->setToolTip("Focus command console (F2 or :)");
    connect(action_focus_console_, &QAction::triggered, this, [this]() {
      if (console_dock_) {
        console_dock_->show();
        console_dock_->focus_input();
      }
    });
    menu_docks->addAction(action_focus_console_);

    menu_view->addSeparator();

    // 4d. Visual Aids & Guides Submenu
    auto* menu_visual_aids = menu_view->addMenu("&Visual Aids && Guides");
    menu_visual_aids->addAction(action_toggle_snap_);
    menu_visual_aids->addAction(action_toggle_angle_snap_);
    menu_visual_aids->addAction(action_toggle_grid_);
    menu_visual_aids->addAction(action_toggle_smart_guides_);
    menu_visual_aids->addAction(action_toggle_rulers_);
    menu_visual_aids->addAction(action_toggle_line_jumps_);
    menu_visual_aids->addAction(action_toggle_segment_moving_);
    menu_visual_aids->addSeparator();

    auto* menu_wire_style = menu_visual_aids->addMenu("&Wire Crossing Style");
    auto* grp_wire_style = new QActionGroup(this);
    grp_wire_style->setExclusive(true);
    auto* act_dot_priority = menu_wire_style->addAction("Style 1: Modern Dot Priority (Crossings pass, ball connects)");
    act_dot_priority->setCheckable(true);
    act_dot_priority->setChecked(true);
    grp_wire_style->addAction(act_dot_priority);
    auto* act_loop_priority = menu_wire_style->addAction("Style 2: Legacy Bridge/Loop Priority (Crossings connect, loop jumps)");
    act_loop_priority->setCheckable(true);
    grp_wire_style->addAction(act_loop_priority);

    connect(act_dot_priority, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->set_wire_crossing_style(madi::renderer::WireCrossingStyle::DotPriority);
    });
    connect(act_loop_priority, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->set_wire_crossing_style(madi::renderer::WireCrossingStyle::BridgeLoopPriority);
    });

    menu_visual_aids->addSeparator();
    menu_visual_aids->addAction(action_snap_settings_);

    // 4e. Snapping Modes Submenu
    auto* menu_snap = menu_view->addMenu("&Snapping Modes");
    menu_snap->addAction(action_toggle_snap_);
    menu_snap->addAction(action_toggle_angle_snap_);
    menu_snap->addAction(action_snap_settings_);
    menu_snap->addSeparator();

    auto* snap_grp = new QActionGroup(this);
    auto make_snap_strength_act = [this, snap_grp, menu_snap](const QString& title, madi::grid::SnapStrength str) {
      auto* act = menu_snap->addAction(title);
      act->setCheckable(true);
      snap_grp->addAction(act);
      if (grid_manager_->snap_strength() == str) act->setChecked(true);
      connect(act, &QAction::triggered, this, [this, str]() {
        grid_manager_->set_snap_strength(str);
        update_snap_ui();
      });
      return act;
    };

    make_snap_strength_act("Off (Disabled)", madi::grid::SnapStrength::Off);
    make_snap_strength_act("Weak (3 px)", madi::grid::SnapStrength::Weak);
    make_snap_strength_act("Normal (6 px)", madi::grid::SnapStrength::Normal);
    make_snap_strength_act("Strong (12 px)", madi::grid::SnapStrength::Strong);

    menu_snap->addSeparator();
    auto* act_custom_snap = menu_snap->addAction("Custom Sensitivity...");
    connect(act_custom_snap, &QAction::triggered, this, [this]() {
      bool ok = false;
      double val = QInputDialog::getDouble(this, "Snap Sensitivity", "Enter snap tolerance distance (pixels):",
                                           grid_manager_->snap_tolerance(), 0.0, 50.0, 1, &ok);
      if (ok) {
        grid_manager_->set_snap_tolerance(val);
        update_snap_ui();
      }
    });

    menu_snap->addSeparator();
    auto make_snap_flag_act = [this, menu_snap](const QString& title, madi::grid::SnapFlags flag) {
      auto* act = menu_snap->addAction(title);
      act->setCheckable(true);
      act->setChecked(madi::grid::has_flag(grid_manager_->snap_flags(), flag));
      connect(act, &QAction::toggled, this, [this, flag](bool checked) {
        if (checked) grid_manager_->enable_flag(flag);
        else grid_manager_->disable_flag(flag);
        update_snap_ui();
      });
      return act;
    };

    make_snap_flag_act("Snap to Grid", madi::grid::SnapFlags::Grid);
    make_snap_flag_act("Snap to Shape Bounds", madi::grid::SnapFlags::ShapeBounds);
    make_snap_flag_act("Snap to Shape Center", madi::grid::SnapFlags::ShapeCenter);
    make_snap_flag_act("Snap to Interface Ports", madi::grid::SnapFlags::InterfacePort);

    // 4f. Page Setup & Sheet Submenu
    auto* menu_page_size = menu_view->addMenu("&Page Setup && Sheet");
    auto* grp_preset = new QActionGroup(menu_page_size);
    grp_preset->setExclusive(true);

    auto add_preset_act = [this, menu_page_size, grp_preset](const QString& title, madi::renderer::PagePreset p) {
      auto* act = menu_page_size->addAction(title);
      act->setCheckable(true);
      grp_preset->addAction(act);
      if (canvas_->page_preset() == p) act->setChecked(true);
      connect(act, &QAction::triggered, this, [this, p]() {
        canvas_->set_page_preset(p, canvas_->page_orientation());
      });
      return act;
    };

    add_preset_act("Letter (8.5 × 11 in)", madi::renderer::PagePreset::Letter);
    add_preset_act("Legal (8.5 × 14 in)", madi::renderer::PagePreset::Legal);
    add_preset_act("Tabloid / Ledger (11 × 17 in)", madi::renderer::PagePreset::Tabloid);
    add_preset_act("A4 (210 × 297 mm)", madi::renderer::PagePreset::A4);
    add_preset_act("A3 (297 × 420 mm)", madi::renderer::PagePreset::A3);
    add_preset_act("A2 (420 × 594 mm)", madi::renderer::PagePreset::A2);
    add_preset_act("A1 (594 × 841 mm)", madi::renderer::PagePreset::A1);
    add_preset_act("A0 (841 × 1189 mm)", madi::renderer::PagePreset::A0);

    menu_page_size->addSeparator();
    auto* grp_orient = new QActionGroup(menu_page_size);
    grp_orient->setExclusive(true);
    auto* act_landscape = menu_page_size->addAction("Landscape");
    act_landscape->setCheckable(true);
    grp_orient->addAction(act_landscape);
    auto* act_portrait = menu_page_size->addAction("Portrait");
    act_portrait->setCheckable(true);
    grp_orient->addAction(act_portrait);
    if (canvas_->page_orientation() == madi::renderer::PageOrientation::Landscape) {
      act_landscape->setChecked(true);
    } else {
      act_portrait->setChecked(true);
    }
    connect(act_landscape, &QAction::triggered, this, [this]() {
      canvas_->set_page_orientation(madi::renderer::PageOrientation::Landscape);
    });
    connect(act_portrait, &QAction::triggered, this, [this]() {
      canvas_->set_page_orientation(madi::renderer::PageOrientation::Portrait);
    });

    menu_page_size->addSeparator();
    auto* act_show_sheet = menu_page_size->addAction("Show Page Sheet");
    act_show_sheet->setCheckable(true);
    act_show_sheet->setChecked(canvas_->show_page_sheet());
    connect(act_show_sheet, &QAction::toggled, this, [this](bool checked) {
      canvas_->set_show_page_sheet(checked);
    });

    menu_page_size->addSeparator();
    auto* act_custom_page = menu_page_size->addAction("Page Setup Dialog...");
    connect(act_custom_page, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(3);
    });

    menu_view->addSeparator();

    // 4g. Workspace & Layout Submenu
    auto* menu_workspace = menu_view->addMenu("&Workspace && Layout");
    menu_workspace->addAction(action_remember_settings_);
    menu_workspace->addAction(action_reset_layout_);

    // 5. Arrange Menu
    auto* menu_arrange = menuBar()->addMenu("Arran&ge");
    auto* menu_arr_order = menu_arrange->addMenu("&Order");
    menu_arr_order->addAction(action_bring_to_front_);
    menu_arr_order->addAction(action_bring_forward_);
    menu_arr_order->addAction(action_send_backward_);
    menu_arr_order->addAction(action_send_to_back_);
    menu_arrange->addAction(action_bring_to_front_);
    menu_arrange->addAction(action_bring_forward_);
    menu_arrange->addAction(action_send_backward_);
    menu_arrange->addAction(action_send_to_back_);
    menu_arrange->addSeparator();
    auto* menu_arr_bool = menu_arrange->addMenu("&Boolean Operations");
    menu_arr_bool->addAction(action_merge_);
    menu_arr_bool->addAction(action_bool_intersect_);
    menu_arr_bool->addAction(action_bool_subtract_);
    menu_arr_bool->addAction(action_bool_xor_);
    menu_arrange->addAction(action_split_);
    menu_arrange->addSeparator();

    auto* menu_arr_rot = menu_arrange->addMenu("&Rotate");
    auto* act_arr_rot_cw = menu_arr_rot->addAction("Rotate 90° &Clockwise");
    act_arr_rot_cw->setShortcut(QKeySequence("Ctrl+R"));
    connect(act_arr_rot_cw, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->rotate_selected(90.0);
    });

    auto* act_arr_rot_ccw = menu_arr_rot->addAction("Rotate 90° Counter-&Clockwise");
    act_arr_rot_ccw->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(act_arr_rot_ccw, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->rotate_selected(-90.0);
    });

    auto* act_arr_rot_180 = menu_arr_rot->addAction("Rotate &180°");
    connect(act_arr_rot_180, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->rotate_selected(180.0);
    });

    auto* act_arr_rot_reset = menu_arr_rot->addAction("Reset &Rotation (0°)");
    connect(act_arr_rot_reset, &QAction::triggered, this, [this]() {
      if (canvas_) canvas_->set_selected_nodes_rotation(0.0);
    });
    menu_arr_rot->addSeparator();
    menu_arr_rot->addAction(action_toggle_angle_snap_);

    menu_arrange->addAction(act_arr_rot_cw);
    menu_arrange->addAction(act_arr_rot_ccw);

    // 6. Hierarchy Menu
    auto* menu_hierarchy = menuBar()->addMenu("&Hierarchy");
    menu_hierarchy->addAction(action_drill_down_);
    menu_hierarchy->addAction(action_pop_scope_);
    menu_hierarchy->addSeparator();
    action_edit_symbol_ = menu_hierarchy->addAction("✏️ &Edit Symbol Definition...");
    action_edit_symbol_->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(action_edit_symbol_, &QAction::triggered, this, [this]() {
      if (active_inspected_node_ != madi::core::InvalidNodeId) {
        open_symbol_editor(active_inspected_node_);
      }
    });

    action_sync_primary_ = menu_hierarchy->addAction("↺ &Sync with Primary Definition");
    connect(action_sync_primary_, &QAction::triggered, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      auto* child_g = canvas_->ensure_subsystem_graph(active_inspected_node_);
      if (child_g) {
        madi::core::SymbolGenerator::sync_symbol_from_diagram(*n, *cur_g, *child_g);
        cur_g->update_all_edge_routes();
        set_document_modified(true);
        update_inspector();
        canvas_->update();
      }
    });

    auto* act_toggle_drift = menu_hierarchy->addAction("&Toggle Definition Drift Mode");
    connect(act_toggle_drift, &QAction::triggered, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      const bool current_drift = (n->properties["definition_sync_mode"] == "allow_drift");
      n->properties["definition_sync_mode"] = current_drift ? "follow_primary" : "allow_drift";
      set_document_modified(true);
      update_inspector();
      canvas_->update();
    });

    menu_hierarchy->addSeparator();
    menu_hierarchy->addAction(action_tool_block_);
    menu_hierarchy->addAction(action_tool_inport_);
    menu_hierarchy->addAction(action_tool_outport_);
    menu_hierarchy->addAction(action_tool_biport_);

    // 6. Themes Menu
    auto* menu_themes = menuBar()->addMenu("&Themes");
    menu_ui_themes_ = menu_themes->addMenu("UI &Theme");
    menu_diag_themes_ = menu_themes->addMenu("&Diagram Theme (Entire Diagram)");
    menu_diag_sel_themes_ = menu_themes->addMenu("Apply Diagram Theme to &Selected Items");
    menu_themes->addSeparator();
    auto* act_reset_sel = menu_themes->addAction("Reset Selected Items to Theme Defaults");
    connect(act_reset_sel, &QAction::triggered, this, [this]() {
      canvas_->reset_selection_to_defaults();
      update_inspector();
    });
    auto* act_reset_all = menu_themes->addAction("Reset Entire Diagram to Theme Defaults");
    connect(act_reset_all, &QAction::triggered, this, [this]() {
      canvas_->reset_all_to_defaults();
      update_inspector();
    });
    menu_themes->addSeparator();
    auto* act_load_theme = menu_themes->addAction("&Load Theme File...");
    connect(act_load_theme, &QAction::triggered, this, &MainWindow::load_custom_theme_file);

    rebuild_theme_menus();

    // 7. Help Menu
    auto* menu_help = menuBar()->addMenu("&Help");

    action_keyboard_shortcuts_ = menu_help->addAction("⌨️ &Keyboard Shortcuts Reference...");
    action_keyboard_shortcuts_->setShortcuts({
        QKeySequence(Qt::Key_F1),
        QKeySequence::HelpContents,
        QKeySequence(Qt::SHIFT | Qt::Key_Slash)
    });
    action_keyboard_shortcuts_->setToolTip("Open Keyboard Shortcuts Reference Dialog (F1)");
    connect(action_keyboard_shortcuts_, &QAction::triggered, this, &MainWindow::show_keyboard_shortcuts_dialog);

    auto* menu_cheat = menu_help->addMenu("⚡ &Quick Reference Cheat Sheet");
    auto add_cheat_item = [menu_cheat](const QString& title, const QString& sc) {
      auto* act = menu_cheat->addAction(QString("%1\t%2").arg(title, sc));
      act->setEnabled(false);
    };
    add_cheat_item("Pointer / Select Tool", "Ctrl+1");
    add_cheat_item("Connector / Wire Tool", "Ctrl+3");
    add_cheat_item("Rectangle Tool", "Ctrl+8");
    add_cheat_item("Ellipse Tool", "Ctrl+9");
    menu_cheat->addSeparator();
    add_cheat_item("Toggle Snapping", "F9");
    add_cheat_item("Toggle Grid", "Shift+F9");
    add_cheat_item("Snap & Grid Settings", "Alt+F9");
    add_cheat_item("Fit to Window", "F3 / Ctrl+Shift+W");
    menu_cheat->addSeparator();
    add_cheat_item("Bring to Front", "Ctrl+Shift+F");
    add_cheat_item("Send to Back", "Ctrl+Shift+B");
    add_cheat_item("Bring Forward", "Ctrl+F");
    add_cheat_item("Duplicate in Place", "Ctrl+D");
    add_cheat_item("Group / Combine", "Ctrl+G");
    add_cheat_item("Ungroup / Split", "Ctrl+Shift+U");
    add_cheat_item("Rotate 90° Clockwise", "Ctrl+R");
    add_cheat_item("Nudge (1px / 10px)", "Arrows / Shift+Arrows");
    menu_cheat->addSeparator();
    auto* act_open_full = menu_cheat->addAction("Open Complete Shortcuts Table (F1)...");
    connect(act_open_full, &QAction::triggered, this, &MainWindow::show_keyboard_shortcuts_dialog);

    menu_help->addSeparator();
    auto* act_console_help = menu_help->addAction("Command &Console Reference");
    connect(act_console_help, &QAction::triggered, this, [this]() {
      if (console_dock_) {
        console_dock_->show();
        console_dock_->focus_input();
      }
    });
    menu_help->addSeparator();
    menu_help->addAction("About Madí...", this, &MainWindow::show_about_dialog);
  }

  void rebuild_theme_menus() {
    if (!menu_ui_themes_ || !menu_diag_themes_ || !menu_diag_sel_themes_) return;

    menu_ui_themes_->clear();
    auto& tm = ThemeManager::instance();
    auto* grp_ui = new QActionGroup(menu_ui_themes_);
    grp_ui->setExclusive(true);
    for (const auto& t : tm.ui_themes()) {
      auto* act = menu_ui_themes_->addAction(QString::fromStdString(t.name));
      act->setCheckable(true);
      if (t.id == tm.current_ui_theme().id) act->setChecked(true);
      grp_ui->addAction(act);
      std::string tid = t.id;
      connect(act, &QAction::triggered, this, [tid]() {
        ThemeManager::instance().set_active_ui_theme(tid);
      });
    }

    menu_diag_themes_->clear();
    auto* grp_diag = new QActionGroup(menu_diag_themes_);
    grp_diag->setExclusive(true);
    for (const auto& t : tm.diagram_themes()) {
      auto* act = menu_diag_themes_->addAction(QString::fromStdString(t.name));
      act->setCheckable(true);
      if (t.id == tm.current_diagram_theme().id) act->setChecked(true);
      grp_diag->addAction(act);
      std::string tid = t.id;
      connect(act, &QAction::triggered, this, [this, tid]() {
        ThemeManager::instance().set_active_diagram_theme(tid);
        if (const auto* dt = ThemeManager::instance().find_diagram_theme(tid)) {
          canvas_->apply_theme_to_entire_diagram(to_canvas_style(*dt));
        }
      });
    }

    menu_diag_sel_themes_->clear();
    for (const auto& t : tm.diagram_themes()) {
      auto* act = menu_diag_sel_themes_->addAction(QString("Apply '%1' to Selection").arg(QString::fromStdString(t.name)));
      std::string tid = t.id;
      connect(act, &QAction::triggered, this, [this, tid]() {
        if (const auto* dt = ThemeManager::instance().find_diagram_theme(tid)) {
          canvas_->apply_theme_to_selection(to_canvas_style(*dt));
          update_inspector();
        }
      });
    }
  }

  void load_custom_theme_file() {
    const QString fpath = QFileDialog::getOpenFileName(
        this, "Load Theme Configuration File", "", "Theme JSON (*.json);;All Files (*)");
    if (!fpath.isEmpty()) {
      if (ThemeManager::instance().load_theme_file(fpath)) {
        rebuild_theme_menus();
        statusBar()->showMessage("Loaded theme file: " + fpath, 4000);
      } else {
        QMessageBox::warning(this, "Theme Error", "Failed to parse theme file:\n" + fpath);
      }
    }
  }

  void cycle_snap_strength() {
    if (!grid_manager_) return;
    auto s = grid_manager_->snap_strength();
    switch (s) {
      case madi::grid::SnapStrength::Off:
        grid_manager_->set_snap_strength(madi::grid::SnapStrength::Weak);
        break;
      case madi::grid::SnapStrength::Weak:
        grid_manager_->set_snap_strength(madi::grid::SnapStrength::Normal);
        break;
      case madi::grid::SnapStrength::Normal:
        grid_manager_->set_snap_strength(madi::grid::SnapStrength::Strong);
        break;
      case madi::grid::SnapStrength::Strong:
      case madi::grid::SnapStrength::Custom:
        grid_manager_->set_snap_strength(madi::grid::SnapStrength::Off);
        break;
    }
    update_snap_ui();
  }

  void update_snap_ui() {
    if (!grid_manager_ || !btn_snap_status_) return;
    const auto s = grid_manager_->snap_strength();
    const double tol = grid_manager_->snap_tolerance();
    QString text;
    if (s == madi::grid::SnapStrength::Off || tol <= 1e-4) {
      text = "SNAP: OFF";
      btn_snap_status_->setIcon(IconFactory::create_icon(IconFactory::IconType::Snapping, QColor(148, 163, 184)));
    } else {
      text = QString("SNAP: %1").arg(madi::grid::to_string(s));
      btn_snap_status_->setIcon(IconFactory::create_icon(IconFactory::IconType::Snapping, QColor(74, 222, 128)));
    }
    btn_snap_status_->setText(text);
    if (action_toggle_snap_) {
      action_toggle_snap_->setChecked(grid_manager_->is_snap_enabled());
    }
    canvas_->update();
  }

  void update_angle_snap_ui() {
    if (!canvas_) return;
    const bool enabled = canvas_->is_angle_snapping_enabled();
    const double step = canvas_->angle_snap_step();

    if (btn_angle_snap_status_) {
      QString text;
      if (enabled) {
        text = QString("∠ SNAP: %1°").arg(step, 0, 'f', (step == std::floor(step)) ? 0 : 1);
        btn_angle_snap_status_->setIcon(IconFactory::create_icon(IconFactory::IconType::AngleSnap, QColor(74, 222, 128)));
      } else {
        text = "∠ SNAP: OFF";
        btn_angle_snap_status_->setIcon(IconFactory::create_icon(IconFactory::IconType::AngleSnap, QColor(148, 163, 184)));
      }
      btn_angle_snap_status_->setText(text);
    }

    if (action_toggle_angle_snap_) {
      action_toggle_angle_snap_->blockSignals(true);
      action_toggle_angle_snap_->setChecked(enabled);
      action_toggle_angle_snap_->blockSignals(false);
    }

    if (btn_tb_angle_snap_) {
      btn_tb_angle_snap_->blockSignals(true);
      btn_tb_angle_snap_->setChecked(enabled);
      btn_tb_angle_snap_->blockSignals(false);
    }

    if (btn_dock_angle_snap_) {
      btn_dock_angle_snap_->blockSignals(true);
      btn_dock_angle_snap_->setChecked(enabled);
      btn_dock_angle_snap_->blockSignals(false);
    }

    if (btn_insp_angle_snap_) {
      btn_insp_angle_snap_->blockSignals(true);
      btn_insp_angle_snap_->setChecked(enabled);
      btn_insp_angle_snap_->blockSignals(false);
    }

    canvas_->update();
  }

  void show_angle_snap_context_menu(const QPoint& global_pos) {
    if (!canvas_) return;
    QMenu menu(this);
    auto* act_toggle = menu.addAction("Enable &Angle Snapping (Alt+A)");
    act_toggle->setCheckable(true);
    act_toggle->setChecked(canvas_->is_angle_snapping_enabled());
    connect(act_toggle, &QAction::toggled, this, [this](bool checked) {
      if (canvas_) {
        canvas_->set_angle_snapping_enabled(checked);
        update_angle_snap_ui();
      }
    });

    menu.addSeparator();
    auto* grp = new QActionGroup(&menu);
    auto add_angle_step = [this, &menu, grp](const QString& title, double step) {
      auto* a = menu.addAction(title);
      a->setCheckable(true);
      grp->addAction(a);
      if (std::abs(canvas_->angle_snap_step() - step) < 0.1) {
        a->setChecked(true);
      }
      connect(a, &QAction::triggered, this, [this, step]() {
        if (canvas_) {
          canvas_->set_angle_snapping_enabled(true);
          canvas_->set_angle_snap_step(step);
          update_angle_snap_ui();
        }
      });
    };

    add_angle_step("5° (Fine Step)", 5.0);
    add_angle_step("15° (Standard)", 15.0);
    add_angle_step("30° (Engineering / Iso)", 30.0);
    add_angle_step("45° (Diagonal / Octagonal)", 45.0);
    add_angle_step("90° (Orthogonal Only)", 90.0);

    menu.addSeparator();
    auto* act_cardinal = menu.addAction("Prioritize Cardinal Angles (0°, 90°, 180°, 270°)");
    act_cardinal->setCheckable(true);
    act_cardinal->setChecked(canvas_->cardinal_snap_priority());
    connect(act_cardinal, &QAction::toggled, this, [this](bool checked) {
      if (canvas_) {
        canvas_->set_cardinal_snap_priority(checked);
      }
    });

    menu.addSeparator();
    auto* act_settings = menu.addAction("Snap & Grid Settings... (Alt+F9)");
    connect(act_settings, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(2);
    });

    menu.exec(global_pos);
  }

  void open_snap_grid_settings(int initial_tab = 0) {
    if (!grid_manager_ || !canvas_) return;
    SnapGridSettingsDialog dlg(this);
    dlg.load_from(*grid_manager_, canvas_->show_smart_guides(), canvas_);
    dlg.set_active_tab(initial_tab);

    auto apply_settings = [this, &dlg]() {
      bool smart_guides = canvas_->show_smart_guides();
      dlg.apply_to(*grid_manager_, smart_guides, canvas_);
      canvas_->set_show_smart_guides(smart_guides);
      if (action_toggle_smart_guides_) {
        action_toggle_smart_guides_->setChecked(smart_guides);
      }
      update_snap_ui();
      update_angle_snap_ui();
      canvas_->update();
    };

    connect(&dlg, &SnapGridSettingsDialog::apply_requested, this, apply_settings);
    if (dlg.exec() == QDialog::Accepted) {
      apply_settings();
    }
  }

  void show_keyboard_shortcuts_dialog() {
    KeyboardShortcutsDialog dlg(this);
    dlg.exec();
  }

  void show_ruler_context_menu(const QPoint& global_pos) {
    QMenu menu(this);
    auto* act_settings = menu.addAction("Snap & Grid Settings... (Alt+F9)");
    connect(act_settings, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(2);
    });
    auto* act_page = menu.addAction("Page Setup...");
    connect(act_page, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(3);
    });
    menu.addSeparator();

    auto* grp_spacing = new QActionGroup(&menu);
    auto* act_var = menu.addAction("Variable Grid Spacing (Zoom-adaptive)");
    act_var->setCheckable(true);
    grp_spacing->addAction(act_var);

    auto* act_fixed = menu.addAction("Fixed Grid Spacing (Custom dimensions)");
    act_fixed->setCheckable(true);
    grp_spacing->addAction(act_fixed);

    if (grid_manager_->grid_spacing_mode() == madi::grid::GridSpacingMode::Fixed) {
      act_fixed->setChecked(true);
    } else {
      act_var->setChecked(true);
    }

    connect(act_var, &QAction::triggered, this, [this]() {
      grid_manager_->set_grid_spacing_mode(madi::grid::GridSpacingMode::Variable);
      canvas_->update();
    });
    connect(act_fixed, &QAction::triggered, this, [this]() {
      grid_manager_->set_grid_spacing_mode(madi::grid::GridSpacingMode::Fixed);
      canvas_->update();
    });

    menu.addSeparator();
    menu.addAction(action_toggle_rulers_);
    menu.addAction(action_toggle_grid_);
    menu.addAction(action_toggle_snap_);

    menu.exec(global_pos);
  }

  void show_snap_status_context_menu(const QPoint& global_pos) {
    QMenu menu(this);
    auto* act_settings = menu.addAction("Snap Settings... (Alt+F9)");
    connect(act_settings, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(2);
    });
    menu.addSeparator();

    auto* grp = new QActionGroup(&menu);
    auto add_strength = [this, &menu, grp](const QString& title, madi::grid::SnapStrength str) {
      auto* a = menu.addAction(title);
      a->setCheckable(true);
      grp->addAction(a);
      if (grid_manager_->snap_strength() == str) a->setChecked(true);
      connect(a, &QAction::triggered, this, [this, str]() {
        grid_manager_->set_snap_strength(str);
        update_snap_ui();
      });
    };
    add_strength("Off (Disabled)", madi::grid::SnapStrength::Off);
    add_strength("Weak (3 px)", madi::grid::SnapStrength::Weak);
    add_strength("Normal (6 px)", madi::grid::SnapStrength::Normal);
    add_strength("Strong (12 px)", madi::grid::SnapStrength::Strong);

    menu.addSeparator();
    auto add_flag = [this, &menu](const QString& title, madi::grid::SnapFlags flag) {
      auto* a = menu.addAction(title);
      a->setCheckable(true);
      a->setChecked(madi::grid::has_flag(grid_manager_->snap_flags(), flag));
      connect(a, &QAction::toggled, this, [this, flag](bool checked) {
        if (checked) grid_manager_->enable_flag(flag);
        else grid_manager_->disable_flag(flag);
        update_snap_ui();
      });
    };
    add_flag("Snap to Grid", madi::grid::SnapFlags::Grid);
    add_flag("Snap to Shape Geometry", madi::grid::SnapFlags::ShapeBounds | madi::grid::SnapFlags::ShapeCenter);
    add_flag("Snap to Connection Points", madi::grid::SnapFlags::InterfacePort);

    menu.exec(global_pos);
  }

  void update_tool_icons() {
    auto& tm = ThemeManager::instance();
    const auto& theme = tm.current_ui_theme();
    const QColor fg = argb_to_qcolor(hex_to_argb(theme.get_color("toolbar_fg", "#CDD6F4")));

    if (action_new_) action_new_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileNew, fg));
    if (action_open_) action_open_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileOpen, fg));
    if (action_save_) action_save_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileSave, fg));
    if (action_save_as_) action_save_as_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileSave, fg));
    if (action_export_drawio_) action_export_drawio_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileExport, fg));
    if (action_export_svg_) action_export_svg_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileExport, fg));
    if (action_export_png_) action_export_png_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileExport, fg));
    if (action_export_jpeg_) action_export_jpeg_->setIcon(IconFactory::create_icon(IconFactory::IconType::FileExport, fg));

    if (btn_bold_) btn_bold_->setIcon(IconFactory::create_icon(IconFactory::IconType::FontBold, fg));
    if (btn_italic_) btn_italic_->setIcon(IconFactory::create_icon(IconFactory::IconType::FontItalic, fg));
    if (btn_underline_) btn_underline_->setIcon(IconFactory::create_icon(IconFactory::IconType::FontUnderline, fg));
    if (btn_fill_color_) btn_fill_color_->setIcon(IconFactory::create_icon(IconFactory::IconType::FillColor, fg));
    if (btn_stroke_color_) btn_stroke_color_->setIcon(IconFactory::create_icon(IconFactory::IconType::StrokeColor, fg));
    if (btn_text_color_) btn_text_color_->setIcon(IconFactory::create_icon(IconFactory::IconType::TextColor, fg));

    if (action_tool_select_) action_tool_select_->setIcon(IconFactory::create_icon(IconFactory::IconType::Select, fg));
    if (action_tool_rect_) action_tool_rect_->setIcon(IconFactory::create_icon(IconFactory::IconType::Rectangle, fg));
    if (action_tool_rounded_) action_tool_rounded_->setIcon(IconFactory::create_icon(IconFactory::IconType::RoundedRect, fg));
    if (action_tool_ellipse_) action_tool_ellipse_->setIcon(IconFactory::create_icon(IconFactory::IconType::Ellipse, fg));
    if (action_tool_polygon_) action_tool_polygon_->setIcon(IconFactory::create_icon(IconFactory::IconType::PolygonHex, fg));
    if (action_tool_block_) action_tool_block_->setIcon(IconFactory::create_icon(IconFactory::IconType::SubsystemBlock, fg));
    if (action_tool_inport_) action_tool_inport_->setIcon(IconFactory::create_icon(IconFactory::IconType::InputPort, fg));
    if (action_tool_outport_) action_tool_outport_->setIcon(IconFactory::create_icon(IconFactory::IconType::OutputPort, fg));
    if (action_tool_biport_) action_tool_biport_->setIcon(IconFactory::create_icon(IconFactory::IconType::BidirectionalPort, fg));
    if (action_tool_wire_) action_tool_wire_->setIcon(IconFactory::create_icon(IconFactory::IconType::Wire, fg));
    if (action_tool_multipoint_arrow_) action_tool_multipoint_arrow_->setIcon(IconFactory::create_icon(IconFactory::IconType::MultiPointArrow, fg));

    if (action_merge_) action_merge_->setIcon(IconFactory::create_icon(IconFactory::IconType::BooleanUnion, fg));
    if (action_bool_intersect_) action_bool_intersect_->setIcon(IconFactory::create_icon(IconFactory::IconType::BooleanIntersect, fg));
    if (action_bool_subtract_) action_bool_subtract_->setIcon(IconFactory::create_icon(IconFactory::IconType::BooleanSubtract, fg));
    if (action_bool_xor_) action_bool_xor_->setIcon(IconFactory::create_icon(IconFactory::IconType::BooleanXor, fg));
    if (action_split_) action_split_->setIcon(IconFactory::create_icon(IconFactory::IconType::SplitFigure, fg));
    if (action_bring_to_front_) action_bring_to_front_->setIcon(IconFactory::create_icon(IconFactory::IconType::BringToFront, fg));
    if (action_bring_forward_) action_bring_forward_->setIcon(IconFactory::create_icon(IconFactory::IconType::BringForward, fg));
    if (action_send_backward_) action_send_backward_->setIcon(IconFactory::create_icon(IconFactory::IconType::SendBackward, fg));
    if (action_send_to_back_) action_send_to_back_->setIcon(IconFactory::create_icon(IconFactory::IconType::SendToBack, fg));

    if (action_undo_) action_undo_->setIcon(IconFactory::create_icon(IconFactory::IconType::Undo, fg));
    if (action_redo_) action_redo_->setIcon(IconFactory::create_icon(IconFactory::IconType::Redo, fg));
    if (action_delete_) action_delete_->setIcon(IconFactory::create_icon(IconFactory::IconType::Delete, fg));

    if (action_zoom_in_) action_zoom_in_->setIcon(IconFactory::create_icon(IconFactory::IconType::ZoomIn, fg));
    if (action_zoom_out_) action_zoom_out_->setIcon(IconFactory::create_icon(IconFactory::IconType::ZoomOut, fg));
    if (action_fit_view_) action_fit_view_->setIcon(IconFactory::create_icon(IconFactory::IconType::FitContent, fg));

    if (action_toggle_grid_) action_toggle_grid_->setIcon(IconFactory::create_icon(IconFactory::IconType::ShowGrid, fg));
    if (action_toggle_snap_) action_toggle_snap_->setIcon(IconFactory::create_icon(IconFactory::IconType::Snapping, fg));
    if (action_toggle_angle_snap_) action_toggle_angle_snap_->setIcon(IconFactory::create_icon(IconFactory::IconType::AngleSnap, fg));
    if (btn_dock_rot_cw_) btn_dock_rot_cw_->setIcon(IconFactory::create_icon(IconFactory::IconType::RotateCW, fg));
    if (btn_dock_rot_ccw_) btn_dock_rot_ccw_->setIcon(IconFactory::create_icon(IconFactory::IconType::RotateCCW, fg));
    if (btn_dock_angle_snap_) btn_dock_angle_snap_->setIcon(IconFactory::create_icon(IconFactory::IconType::AngleSnap, fg));
    if (btn_insp_rot_cw_) btn_insp_rot_cw_->setIcon(IconFactory::create_icon(IconFactory::IconType::RotateCW, fg));
    if (btn_insp_rot_ccw_) btn_insp_rot_ccw_->setIcon(IconFactory::create_icon(IconFactory::IconType::RotateCCW, fg));

    if (action_drill_down_) action_drill_down_->setIcon(IconFactory::create_icon(IconFactory::IconType::DrillDown, fg));
    if (action_pop_scope_) action_pop_scope_->setIcon(IconFactory::create_icon(IconFactory::IconType::PopScope, fg));

    for (auto& [btn, itype] : palette_tool_buttons_) {
      if (btn) {
        btn->setIcon(IconFactory::create_icon(itype, fg));
      }
    }

    if (stencil_dock_) {
      stencil_dock_->update_icons(fg);
    }
  }

  void create_toolbars() {
    // Sub-Section 1: File & History Toolbar
    toolbar_file_ = addToolBar("File & History");
    toolbar_file_->setObjectName("FileHistoryToolBar");
    toolbar_file_->setMovable(true);
    toolbar_file_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_file_->setIconSize(QSize(20, 20));
    toolbar_file_->addAction(action_new_);
    toolbar_file_->addAction(action_open_);
    toolbar_file_->addAction(action_save_);
    toolbar_file_->addSeparator();
    toolbar_file_->addAction(action_undo_);
    toolbar_file_->addAction(action_redo_);

    // Sub-Section 2: Drawing & Shape Tools
    toolbar_draw_ = addToolBar("Drawing Tools");
    toolbar_draw_->setObjectName("DrawingToolsToolBar");
    toolbar_draw_->setMovable(true);
    toolbar_draw_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_draw_->setIconSize(QSize(20, 20));
    toolbar_draw_->addAction(action_tool_select_);
    toolbar_draw_->addAction(action_tool_wire_);
    toolbar_draw_->addSeparator();
    toolbar_draw_->addAction(action_tool_rect_);
    toolbar_draw_->addAction(action_tool_rounded_);
    toolbar_draw_->addAction(action_tool_ellipse_);
    toolbar_draw_->addAction(action_tool_polygon_);
    toolbar_draw_->addSeparator();
    toolbar_draw_->addAction(action_tool_block_);
    toolbar_draw_->addAction(action_tool_inport_);
    toolbar_draw_->addAction(action_tool_outport_);

    main_toolbar_ = toolbar_draw_; // Backward-compatibility alias

    // Sub-Section 3: Figure & Boolean Operations
    toolbar_operations_ = addToolBar("Figure Operations");
    toolbar_operations_->setObjectName("FigureOperationsToolBar");
    toolbar_operations_->setMovable(true);
    toolbar_operations_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_operations_->setIconSize(QSize(20, 20));
    toolbar_operations_->addAction(action_merge_);
    toolbar_operations_->addAction(action_bool_intersect_);
    toolbar_operations_->addAction(action_bool_subtract_);
    toolbar_operations_->addAction(action_bool_xor_);
    toolbar_operations_->addSeparator();
    toolbar_operations_->addAction(action_split_);
    toolbar_operations_->addSeparator();
    toolbar_operations_->addAction(action_bring_to_front_);
    toolbar_operations_->addAction(action_bring_forward_);
    toolbar_operations_->addAction(action_send_backward_);
    toolbar_operations_->addAction(action_send_to_back_);
    toolbar_operations_->addSeparator();
    toolbar_operations_->addAction(action_delete_);

    // Sub-Section 4: View & Navigation
    toolbar_view_ = addToolBar("View & Navigation");
    toolbar_view_->setObjectName("ViewNavToolBar");
    toolbar_view_->setMovable(true);
    toolbar_view_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_view_->setIconSize(QSize(20, 20));
    toolbar_view_->addAction(action_zoom_in_);
    toolbar_view_->addAction(action_zoom_out_);
    toolbar_view_->addAction(action_fit_view_);

    // Sub-Section 5: Visual Aids & Snapping
    toolbar_aids_ = addToolBar("Visual Aids & Snapping");
    toolbar_aids_->setObjectName("VisualAidsToolBar");
    toolbar_aids_->setMovable(true);
    toolbar_aids_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_aids_->setIconSize(QSize(20, 20));
    toolbar_aids_->addAction(action_toggle_grid_);
    toolbar_aids_->addAction(action_toggle_snap_);

    btn_tb_angle_snap_ = new QToolButton(toolbar_aids_);
    btn_tb_angle_snap_->setDefaultAction(action_toggle_angle_snap_);
    btn_tb_angle_snap_->setPopupMode(QToolButton::MenuButtonPopup);
    btn_tb_angle_snap_->setToolTip("Angle Snapping (Alt+A): Toggle or select angle preset (5°, 15°, 30°, 45°, 90°)");

    auto* menu_tb_angle = new QMenu(btn_tb_angle_snap_);
    auto* grp_tb_angle = new QActionGroup(menu_tb_angle);
    auto add_tb_step = [this, menu_tb_angle, grp_tb_angle](const QString& title, double step) {
      auto* act = menu_tb_angle->addAction(title);
      act->setCheckable(true);
      grp_tb_angle->addAction(act);
      if (canvas_ && std::abs(canvas_->angle_snap_step() - step) < 0.1) {
        act->setChecked(true);
      }
      connect(act, &QAction::triggered, this, [this, step]() {
        if (canvas_) {
          canvas_->set_angle_snapping_enabled(true);
          canvas_->set_angle_snap_step(step);
          update_angle_snap_ui();
        }
      });
    };
    add_tb_step("5° (Fine Step)", 5.0);
    add_tb_step("15° (Standard)", 15.0);
    add_tb_step("30° (Engineering / Iso)", 30.0);
    add_tb_step("45° (Diagonal / Octagonal)", 45.0);
    add_tb_step("90° (Orthogonal Only)", 90.0);
    menu_tb_angle->addSeparator();
    auto* act_tb_cardinal = menu_tb_angle->addAction("Prioritize Cardinal Angles (0°, 90°, 180°, 270°)");
    act_tb_cardinal->setCheckable(true);
    if (canvas_) act_tb_cardinal->setChecked(canvas_->cardinal_snap_priority());
    connect(act_tb_cardinal, &QAction::toggled, this, [this](bool checked) {
      if (canvas_) canvas_->set_cardinal_snap_priority(checked);
    });
    menu_tb_angle->addSeparator();
    auto* act_tb_settings = menu_tb_angle->addAction("Snap & Grid Settings... (Alt+F9)");
    connect(act_tb_settings, &QAction::triggered, this, [this]() {
      open_snap_grid_settings(2);
    });
    btn_tb_angle_snap_->setMenu(menu_tb_angle);
    toolbar_aids_->addWidget(btn_tb_angle_snap_);

    toolbar_aids_->addAction(action_snap_settings_);
    toolbar_aids_->addSeparator();
    toolbar_aids_->addAction(action_toggle_rulers_);
    toolbar_aids_->addAction(action_toggle_smart_guides_);

    // Sub-Section 6: Hierarchy Navigation (Hierarchy path is displayed exclusively on bottom status bar)
    toolbar_hierarchy_ = addToolBar("Hierarchy Navigation");
    toolbar_hierarchy_->setObjectName("HierarchyToolBar");
    toolbar_hierarchy_->setMovable(true);
    toolbar_hierarchy_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_hierarchy_->setIconSize(QSize(20, 20));
    toolbar_hierarchy_->addAction(action_drill_down_);
    toolbar_hierarchy_->addAction(action_pop_scope_);
    if (action_edit_symbol_) toolbar_hierarchy_->addAction(action_edit_symbol_);

    // Sub-Section 7: Text & Quick Styling Toolbar
    toolbar_format_ = addToolBar("Text & Formatting");
    toolbar_format_->setObjectName("TextFormattingToolBar");
    toolbar_format_->setMovable(true);
    toolbar_format_->setIconSize(QSize(18, 18));

    combo_font_family_ = new QComboBox(toolbar_format_);
    combo_font_family_->setToolTip("Font Family");
    combo_font_family_->setFixedWidth(120);
    combo_font_family_->addItems({"Sans Serif", "Arial", "Helvetica", "Segoe UI", "Roboto", "Liberation Sans", "Courier New", "Monospace", "Georgia", "Times New Roman"});
    connect(combo_font_family_, &QComboBox::currentTextChanged, this, [this](const QString& family) {
      if (suppress_inspector_updates_ || !canvas_) return;
      canvas_->set_selected_nodes_font_family(family);
    });
    toolbar_format_->addWidget(combo_font_family_);

    combo_font_size_ = new QComboBox(toolbar_format_);
    combo_font_size_->setToolTip("Font Size (pt)");
    combo_font_size_->setFixedWidth(55);
    combo_font_size_->setEditable(true);
    combo_font_size_->addItems({"8", "9", "10", "11", "12", "14", "16", "18", "20", "24", "28", "32", "36", "48", "72"});
    combo_font_size_->setCurrentText("10");
    connect(combo_font_size_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
      if (suppress_inspector_updates_ || !canvas_) return;
      bool ok = false;
      int sz = text.toInt(&ok);
      if (ok && sz > 0) {
        canvas_->set_selected_nodes_font_size(sz);
      }
    });
    toolbar_format_->addWidget(combo_font_size_);

    btn_bold_ = new QToolButton(toolbar_format_);
    btn_bold_->setToolTip("Bold");
    btn_bold_->setCheckable(true);
    btn_bold_->setIcon(IconFactory::create_icon(IconFactory::IconType::FontBold, QColor("#CDD6F4")));
    connect(btn_bold_, &QToolButton::toggled, this, [this](bool bold) {
      if (suppress_inspector_updates_ || !canvas_) return;
      canvas_->set_selected_nodes_font_bold(bold);
    });
    toolbar_format_->addWidget(btn_bold_);

    btn_italic_ = new QToolButton(toolbar_format_);
    btn_italic_->setToolTip("Italic");
    btn_italic_->setCheckable(true);
    btn_italic_->setIcon(IconFactory::create_icon(IconFactory::IconType::FontItalic, QColor("#CDD6F4")));
    connect(btn_italic_, &QToolButton::toggled, this, [this](bool italic) {
      if (suppress_inspector_updates_ || !canvas_) return;
      canvas_->set_selected_nodes_font_italic(italic);
    });
    toolbar_format_->addWidget(btn_italic_);

    btn_underline_ = new QToolButton(toolbar_format_);
    btn_underline_->setToolTip("Underline");
    btn_underline_->setCheckable(true);
    btn_underline_->setIcon(IconFactory::create_icon(IconFactory::IconType::FontUnderline, QColor("#CDD6F4")));
    connect(btn_underline_, &QToolButton::toggled, this, [this](bool underline) {
      if (suppress_inspector_updates_ || !canvas_) return;
      canvas_->set_selected_nodes_font_underline(underline);
    });
    toolbar_format_->addWidget(btn_underline_);

    toolbar_format_->addSeparator();

    btn_fill_color_ = new QToolButton(toolbar_format_);
    btn_fill_color_->setToolTip("Fill Color (Choose from palette)");
    btn_fill_color_->setIcon(IconFactory::create_icon(IconFactory::IconType::FillColor, QColor("#3B82F6")));
    connect(btn_fill_color_, &QToolButton::clicked, this, [this]() {
      if (!canvas_) return;
      QColor c = QColorDialog::getColor(QColor("#3B82F6"), this, "Choose Shape Fill Color");
      if (c.isValid()) {
        canvas_->set_selected_nodes_fill_color(c.name());
        update_inspector();
      }
    });
    toolbar_format_->addWidget(btn_fill_color_);

    btn_stroke_color_ = new QToolButton(toolbar_format_);
    btn_stroke_color_->setToolTip("Stroke Color (Choose from palette)");
    btn_stroke_color_->setIcon(IconFactory::create_icon(IconFactory::IconType::StrokeColor, QColor("#EF4444")));
    connect(btn_stroke_color_, &QToolButton::clicked, this, [this]() {
      if (!canvas_) return;
      QColor c = QColorDialog::getColor(QColor("#EF4444"), this, "Choose Stroke Color");
      if (c.isValid()) {
        canvas_->set_selected_nodes_stroke_color(c.name());
        update_inspector();
      }
    });
    toolbar_format_->addWidget(btn_stroke_color_);

    btn_text_color_ = new QToolButton(toolbar_format_);
    btn_text_color_->setToolTip("Text Color (Choose from palette)");
    btn_text_color_->setIcon(IconFactory::create_icon(IconFactory::IconType::TextColor, QColor("#F8FAFC")));
    connect(btn_text_color_, &QToolButton::clicked, this, [this]() {
      if (!canvas_) return;
      QColor c = QColorDialog::getColor(QColor("#F8FAFC"), this, "Choose Text Color");
      if (c.isValid()) {
        canvas_->set_selected_nodes_text_color(c.name());
        update_inspector();
      }
    });
    toolbar_format_->addWidget(btn_text_color_);

    combo_stroke_width_ = new QComboBox(toolbar_format_);
    combo_stroke_width_->setToolTip("Stroke Width");
    combo_stroke_width_->setFixedWidth(70);
    combo_stroke_width_->addItems({"0.5 px", "1.0 px", "1.5 px", "2.0 px", "3.0 px", "4.0 px", "6.0 px", "8.0 px"});
    combo_stroke_width_->setCurrentIndex(2);
    connect(combo_stroke_width_, &QComboBox::currentIndexChanged, this, [this](int idx) {
      if (suppress_inspector_updates_ || !canvas_) return;
      static const double widths[] = {0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0};
      if (idx >= 0 && idx < 8) {
        canvas_->set_selected_nodes_stroke_width(widths[idx]);
      }
    });
    toolbar_format_->addWidget(combo_stroke_width_);

    toolbar_format_->addSeparator();

    auto* swatches_widget = new QWidget(toolbar_format_);
    auto* swatches_layout = new QHBoxLayout(swatches_widget);
    swatches_layout->setContentsMargins(2, 0, 2, 0);
    swatches_layout->setSpacing(2);

    const struct SwatchDef { const char* hex; const char* name; } swatches[] = {
      {"#334155", "Slate"},
      {"#EF4444", "Red"},
      {"#F97316", "Orange"},
      {"#F59E0B", "Amber"},
      {"#10B981", "Emerald"},
      {"#14B8A6", "Teal"},
      {"#0EA5E9", "Sky Blue"},
      {"#3B82F6", "Blue"},
      {"#6366F1", "Indigo"},
      {"#EC4899", "Pink"}
    };

    for (const auto& sw : swatches) {
      auto* btn_sw = new QToolButton(swatches_widget);
      btn_sw->setFixedSize(16, 16);
      btn_sw->setToolTip(QString("%1 (%2)\nClick: Fill | Shift+Click: Stroke | Ctrl+Click: Text").arg(sw.name, sw.hex));
      btn_sw->setStyleSheet(QString(
        "QToolButton { background-color: %1; border: 1px solid rgba(255,255,255,0.35); border-radius: 3px; }"
        "QToolButton:hover { border: 2px solid #FFFFFF; }"
      ).arg(sw.hex));
      QString hex_str = sw.hex;
      connect(btn_sw, &QToolButton::clicked, this, [this, hex_str]() {
        if (!canvas_) return;
        auto mods = QGuiApplication::keyboardModifiers();
        if (mods & Qt::ShiftModifier) {
          canvas_->set_selected_nodes_stroke_color(hex_str);
        } else if (mods & Qt::ControlModifier) {
          canvas_->set_selected_nodes_text_color(hex_str);
        } else {
          canvas_->set_selected_nodes_fill_color(hex_str);
        }
        update_inspector();
      });
      swatches_layout->addWidget(btn_sw);
    }
    toolbar_format_->addWidget(swatches_widget);
  }

  void create_docks() {
    // 1. Tools Dock (Converted from Figures & Blocks Palette)
    dock_palette_ = new QDockWidget("Tools", this);
    dock_palette_->setObjectName("DockTools");
    dock_palette_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* palette_widget = new QWidget(dock_palette_);
    auto* palette_layout = new QVBoxLayout(palette_widget);
    palette_layout->setSpacing(4);
    palette_layout->setContentsMargins(4, 4, 4, 4);

    auto make_palette_btn = [this](IconFactory::IconType itype, const QString& tooltip, auto click_fn) {
      auto* btn = new QToolButton(this);
      btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
      btn->setIconSize(QSize(20, 20));
      btn->setFixedSize(28, 28);
      btn->setToolTip(tooltip);
      palette_tool_buttons_.emplace_back(btn, itype);
      connect(btn, &QToolButton::clicked, this, click_fn);
      return btn;
    };

    // Category 1: Interaction & Selection Tools (Compact Selector)
    auto* lbl_modes = new QLabel("<b>Interaction Tools:</b>", this);
    lbl_modes->setStyleSheet("font-size: 11px;");
    palette_layout->addWidget(lbl_modes);

    auto* grid_modes = new QGridLayout();
    grid_modes->setSpacing(3);

    btn_palette_select_ = make_palette_btn(IconFactory::IconType::Select, "Select & Transform (S)\nPick, move, resize, and edit items on canvas", [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::Select);
    });
    btn_palette_select_->setCheckable(true);
    btn_palette_select_->setChecked(true);

    btn_palette_wire_ = make_palette_btn(IconFactory::IconType::Wire, "Orthogonal Net Connector (C)\nRoute orthogonal obstacle-avoiding wires", [this]() {
      canvas_->set_tool_mode(madi::renderer::ToolMode::AddConnector);
    });
    btn_palette_wire_->setCheckable(true);

    btn_palette_pan_ = make_palette_btn(IconFactory::IconType::Pan, "Pan Canvas (Space+Drag)\nHold Space and drag with mouse to pan", [this]() {
      statusBar()->showMessage("Pan Tool: Click and drag canvas or hold Spacebar to pan viewport.", 3000);
    });

    btn_palette_del_ = make_palette_btn(IconFactory::IconType::Delete, "Delete Selected (Del)\nRemove selected figures and connections", [this]() {
      canvas_->delete_selected();
    });
    btn_palette_del_->setEnabled(false);

    grid_modes->addWidget(btn_palette_select_, 0, 0);
    grid_modes->addWidget(btn_palette_wire_, 0, 1);
    grid_modes->addWidget(btn_palette_pan_, 0, 2);
    grid_modes->addWidget(btn_palette_del_, 0, 3);
    palette_layout->addLayout(grid_modes);

    palette_layout->addSpacing(3);

    // Category 2: Figure Operations (Boolean & Grouping)
    auto* lbl_ops = new QLabel("<b>Boolean & Combine:</b>", this);
    lbl_ops->setStyleSheet("font-size: 11px;");
    palette_layout->addWidget(lbl_ops);

    auto* grid_ops = new QGridLayout();
    grid_ops->setSpacing(3);

    btn_dock_merge_ = make_palette_btn(IconFactory::IconType::BooleanUnion, "Union / OR (Merge) (Ctrl+G)\nCombine multiple figures into a unified polygon", [this]() {
      canvas_->merge_selected_figures();
    });
    btn_dock_merge_->setEnabled(false);

    btn_dock_intersect_ = make_palette_btn(IconFactory::IconType::BooleanIntersect, "Intersection (AND) (Ctrl+Shift+I)\nKeep overlapping region of figures", [this]() {
      canvas_->boolean_combine_selected(madi::core::BooleanOp::Intersection);
    });
    btn_dock_intersect_->setEnabled(false);

    btn_dock_subtract_ = make_palette_btn(IconFactory::IconType::BooleanSubtract, "Subtract / Difference (Ctrl+Shift+D)\nCut subsequent figures out of first figure", [this]() {
      canvas_->boolean_combine_selected(madi::core::BooleanOp::Difference);
    });
    btn_dock_subtract_->setEnabled(false);

    btn_dock_xor_ = make_palette_btn(IconFactory::IconType::BooleanXor, "XOR / Symmetric Difference (Ctrl+Shift+X)\nKeep regions that do not overlap", [this]() {
      canvas_->boolean_combine_selected(madi::core::BooleanOp::Xor);
    });
    btn_dock_xor_->setEnabled(false);

    btn_dock_split_ = make_palette_btn(IconFactory::IconType::SplitFigure, "Split Selected Figure (Ctrl+Shift+G)\nUngroup composite figure into elements", [this]() {
      canvas_->split_selected_figure();
    });
    btn_dock_split_->setEnabled(false);

    grid_ops->addWidget(btn_dock_merge_, 0, 0);
    grid_ops->addWidget(btn_dock_intersect_, 0, 1);
    grid_ops->addWidget(btn_dock_subtract_, 0, 2);
    grid_ops->addWidget(btn_dock_xor_, 0, 3);
    grid_ops->addWidget(btn_dock_split_, 1, 0);
    palette_layout->addLayout(grid_ops);

    palette_layout->addSpacing(3);

    // Category 3: Hierarchy & Scope Tools
    auto* lbl_hier = new QLabel("<b>Hierarchy & Scope:</b>", this);
    lbl_hier->setStyleSheet("font-size: 11px;");
    palette_layout->addWidget(lbl_hier);

    auto* grid_hier = new QGridLayout();
    grid_hier->setSpacing(3);

    btn_dock_down_ = make_palette_btn(IconFactory::IconType::DrillDown, "Drill Down: Descend into internal view (Enter)", [this]() {
      canvas_->drill_down_selected();
    });
    btn_dock_down_->setEnabled(false);

    btn_dock_up_ = make_palette_btn(IconFactory::IconType::PopScope, "Up to Parent: Ascend to parent diagram level (Esc)", [this]() {
      canvas_->pop_hierarchy_scope();
    });
    btn_dock_up_->setEnabled(false);

    grid_hier->addWidget(btn_dock_down_, 0, 0);
    grid_hier->addWidget(btn_dock_up_, 0, 1);
    palette_layout->addLayout(grid_hier);

    palette_layout->addSpacing(3);

    // Category 4: Stacking & Arrangement
    auto* lbl_stack = new QLabel("<b>Stacking & Order:</b>", this);
    lbl_stack->setStyleSheet("font-size: 11px;");
    palette_layout->addWidget(lbl_stack);

    auto* grid_stack = new QGridLayout();
    grid_stack->setSpacing(3);

    btn_dock_front_ = make_palette_btn(IconFactory::IconType::BringToFront, "Bring to Front (Ctrl+Shift+])", [this]() {
      canvas_->bring_to_front_selected();
    });
    btn_dock_front_->setEnabled(false);

    btn_dock_forward_ = make_palette_btn(IconFactory::IconType::BringForward, "Bring Forward (Ctrl+])", [this]() {
      canvas_->bring_forward_selected();
    });
    btn_dock_forward_->setEnabled(false);

    btn_dock_backward_ = make_palette_btn(IconFactory::IconType::SendBackward, "Send Backward (Ctrl+[)", [this]() {
      canvas_->send_backward_selected();
    });
    btn_dock_backward_->setEnabled(false);

    btn_dock_back_ = make_palette_btn(IconFactory::IconType::SendToBack, "Send to Back (Ctrl+Shift+[)", [this]() {
      canvas_->send_to_back_selected();
    });
    btn_dock_back_->setEnabled(false);

    grid_stack->addWidget(btn_dock_front_, 0, 0);
    grid_stack->addWidget(btn_dock_forward_, 0, 1);
    grid_stack->addWidget(btn_dock_backward_, 0, 2);
    grid_stack->addWidget(btn_dock_back_, 0, 3);
    palette_layout->addLayout(grid_stack);

    palette_layout->addSpacing(3);

    // Category 5: Rotate & Snapping
    auto* lbl_rot = new QLabel("<b>Rotate & Snap:</b>", this);
    lbl_rot->setStyleSheet("font-size: 11px;");
    palette_layout->addWidget(lbl_rot);

    auto* grid_rot = new QGridLayout();
    grid_rot->setSpacing(3);

    btn_dock_rot_ccw_ = make_palette_btn(IconFactory::IconType::RotateCCW, "Rotate 90° Counter-Clockwise (Ctrl+Shift+R)", [this]() {
      if (canvas_) canvas_->rotate_selected(-90.0);
    });
    btn_dock_rot_ccw_->setEnabled(false);

    btn_dock_rot_cw_ = make_palette_btn(IconFactory::IconType::RotateCW, "Rotate 90° Clockwise (Ctrl+R)", [this]() {
      if (canvas_) canvas_->rotate_selected(90.0);
    });
    btn_dock_rot_cw_->setEnabled(false);

    btn_dock_angle_snap_ = make_palette_btn(IconFactory::IconType::AngleSnap, "Toggle Angle Snapping (Alt+A)\nSnaps figure rotation to angular increments", [this]() {
      if (canvas_) {
        canvas_->set_angle_snapping_enabled(!canvas_->is_angle_snapping_enabled());
        update_angle_snap_ui();
      }
    });
    btn_dock_angle_snap_->setCheckable(true);
    btn_dock_angle_snap_->setChecked(canvas_ ? canvas_->is_angle_snapping_enabled() : true);

    grid_rot->addWidget(btn_dock_rot_ccw_, 0, 0);
    grid_rot->addWidget(btn_dock_rot_cw_, 0, 1);
    grid_rot->addWidget(btn_dock_angle_snap_, 0, 2);
    palette_layout->addLayout(grid_rot);

    palette_layout->addSpacing(3);

    // Category 6: History / Revert
    auto* lbl_hist = new QLabel("<b>History:</b>", this);
    lbl_hist->setStyleSheet("font-size: 11px;");
    palette_layout->addWidget(lbl_hist);

    auto* grid_hist = new QGridLayout();
    grid_hist->setSpacing(3);

    btn_dock_undo_ = make_palette_btn(IconFactory::IconType::Undo, "Undo (Ctrl+Z)", [this]() {
      command_history_->undo();
    });
    btn_dock_undo_->setEnabled(false);

    btn_dock_redo_ = make_palette_btn(IconFactory::IconType::Redo, "Redo (Ctrl+Y)", [this]() {
      command_history_->redo();
    });
    btn_dock_redo_->setEnabled(false);

    grid_hist->addWidget(btn_dock_undo_, 0, 0);
    grid_hist->addWidget(btn_dock_redo_, 0, 1);
    palette_layout->addLayout(grid_hist);

    palette_layout->addStretch();
    dock_palette_->setWidget(palette_widget);
    dock_palette_->setMinimumWidth(130);
    addDockWidget(Qt::LeftDockWidgetArea, dock_palette_);

    // Stencil Library Dock (Left) - Shapes
    stencil_dock_ = new StencilLibraryDock(this);
    stencil_dock_->setObjectName("ShapesDock");
    stencil_dock_->set_canvas(canvas_);
    addDockWidget(Qt::LeftDockWidgetArea, stencil_dock_);

    // Hierarchy Tree Dock (Left)
    hierarchy_dock_ = new HierarchyTreeDock(this);
    hierarchy_dock_->setObjectName("HierarchyTreeDock");
    hierarchy_dock_->set_canvas(canvas_);
    hierarchy_dock_->set_graph_model(graph_.get());
    addDockWidget(Qt::LeftDockWidgetArea, hierarchy_dock_);

    tabifyDockWidget(stencil_dock_, dock_palette_);
    tabifyDockWidget(dock_palette_, hierarchy_dock_);
    stencil_dock_->raise();

    // 2. Element Properties Inspector Dock
    dock_inspector_ = new QDockWidget("Properties Inspector", this);
    dock_inspector_->setObjectName("PropertiesInspectorDock");
    dock_inspector_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    auto* inspector_widget = new QWidget(dock_inspector_);
    auto* form_layout = new QFormLayout(inspector_widget);

    inspector_name_ = new QLineEdit(this);
    connect(inspector_name_, &QLineEdit::editingFinished, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ != madi::core::InvalidNodeId && cur_g) {
        if (auto* n = cur_g->find_node(active_inspected_node_)) {
          if (n->name != inspector_name_->text().toStdString()) {
            n->name = inspector_name_->text().toStdString();
            set_document_modified(true);
            canvas_->update();
          }
        }
      }
    });

    inspector_shape_type_ = new QLabel("None", this);
    inspector_shape_type_->setStyleSheet("QLabel { font-weight: bold; color: #2B6CB0; }");

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
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ != madi::core::InvalidNodeId && cur_g) {
        if (auto* n = cur_g->find_node(active_inspected_node_)) {
          n->bounds.x = inspector_x_->value();
          n->bounds.y = inspector_y_->value();
          n->bounds.width = inspector_w_->value();
          n->bounds.height = inspector_h_->value();
          n->rotation_deg = inspector_rotation_->value();
          set_document_modified(true);
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
    form_layout->addRow("Shape:", inspector_shape_type_);
    form_layout->addRow("X:", inspector_x_);
    form_layout->addRow("Y:", inspector_y_);
    form_layout->addRow("Width:", inspector_w_);
    form_layout->addRow("Height:", inspector_h_);
    auto* rot_row = new QWidget(this);
    auto* rot_layout = new QHBoxLayout(rot_row);
    rot_layout->setContentsMargins(0, 0, 0, 0);
    rot_layout->setSpacing(3);
    rot_layout->addWidget(inspector_rotation_, 1);

    btn_insp_rot_ccw_ = new QToolButton(rot_row);
    btn_insp_rot_ccw_->setFixedSize(22, 22);
    btn_insp_rot_ccw_->setToolTip("Rotate 90° CCW");
    btn_insp_rot_ccw_->setIcon(IconFactory::create_icon(IconFactory::IconType::RotateCCW, QColor(0x38, 0xBD, 0xF8)));
    connect(btn_insp_rot_ccw_, &QToolButton::clicked, this, [this]() {
      if (canvas_) canvas_->rotate_selected(-90.0);
    });
    rot_layout->addWidget(btn_insp_rot_ccw_);

    btn_insp_rot_cw_ = new QToolButton(rot_row);
    btn_insp_rot_cw_->setFixedSize(22, 22);
    btn_insp_rot_cw_->setToolTip("Rotate 90° CW");
    btn_insp_rot_cw_->setIcon(IconFactory::create_icon(IconFactory::IconType::RotateCW, QColor(0x38, 0xBD, 0xF8)));
    connect(btn_insp_rot_cw_, &QToolButton::clicked, this, [this]() {
      if (canvas_) canvas_->rotate_selected(90.0);
    });
    rot_layout->addWidget(btn_insp_rot_cw_);

    btn_insp_angle_snap_ = new QToolButton(rot_row);
    btn_insp_angle_snap_->setFixedSize(22, 22);
    btn_insp_angle_snap_->setText("∠");
    btn_insp_angle_snap_->setCheckable(true);
    btn_insp_angle_snap_->setChecked(canvas_ ? canvas_->is_angle_snapping_enabled() : true);
    btn_insp_angle_snap_->setToolTip("Toggle Angle Snapping (15° default, hold Alt for free rotation)");
    btn_insp_angle_snap_->setStyleSheet("QToolButton { font-weight: bold; } QToolButton:checked { background: #0078D4; color: white; border-radius: 3px; }");
    connect(btn_insp_angle_snap_, &QToolButton::toggled, this, [this](bool checked) {
      if (canvas_) {
        canvas_->set_angle_snapping_enabled(checked);
        update_angle_snap_ui();
      }
    });
    rot_layout->addWidget(btn_insp_angle_snap_);

    form_layout->addRow("Rotation:", rot_row);

    // Subsystem Management Group
    inspector_subsystem_group_ = new QGroupBox("Subsystem Configuration", inspector_widget);
    auto* sub_layout = new QFormLayout(inspector_subsystem_group_);

    inspector_subsystem_type_ = new QComboBox(inspector_subsystem_group_);
    inspector_subsystem_type_->addItem("⚙ Built-in (Embedded in Diagram)");
    inspector_subsystem_type_->addItem("📁 Separate File (Linked)");
    sub_layout->addRow("Mode:", inspector_subsystem_type_);

    auto* file_row = new QWidget(inspector_subsystem_group_);
    auto* file_row_layout = new QHBoxLayout(file_row);
    file_row_layout->setContentsMargins(0, 0, 0, 0);
    inspector_subsystem_file_ = new QLineEdit(file_row);
    inspector_subsystem_file_->setPlaceholderText("subsystem.yaml");
    btn_browse_subsystem_file_ = new QPushButton("Browse...", file_row);
    file_row_layout->addWidget(inspector_subsystem_file_);
    file_row_layout->addWidget(btn_browse_subsystem_file_);
    sub_layout->addRow("File:", file_row);

    auto* btn_row1 = new QWidget(inspector_subsystem_group_);
    auto* btn_row1_layout = new QHBoxLayout(btn_row1);
    btn_row1_layout->setContentsMargins(0, 0, 0, 0);
    btn_subsystem_drill_ = new QPushButton("⬇ Drill Down", btn_row1);
    btn_subsystem_drill_->setStyleSheet("font-weight: bold; background: #EBF8FF; padding: 4px;");
    btn_subsystem_convert_ = new QPushButton("📤 Export / Embed", btn_row1);
    btn_row1_layout->addWidget(btn_subsystem_drill_);
    btn_row1_layout->addWidget(btn_subsystem_convert_);
    sub_layout->addRow(btn_row1);

    auto* btn_row2 = new QWidget(inspector_subsystem_group_);
    auto* btn_row2_layout = new QHBoxLayout(btn_row2);
    btn_row2_layout->setContentsMargins(0, 0, 0, 0);
    btn_subsystem_save_ = new QPushButton("💾 Save File", btn_row2);
    btn_subsystem_reload_ = new QPushButton("🔄 Reload File", btn_row2);
    btn_row2_layout->addWidget(btn_subsystem_save_);
    btn_row2_layout->addWidget(btn_subsystem_reload_);
    sub_layout->addRow(btn_row2);

    // Definition Synchronization & Drift Mode (Inclusive Language)
    inspector_drift_mode_ = new QComboBox(inspector_subsystem_group_);
    inspector_drift_mode_->addItem("Follow Primary Definition (Synchronized)", "follow_primary");
    inspector_drift_mode_->addItem("Allow Drift (Custom Instance Override)", "allow_drift");
    sub_layout->addRow("Definition Sync:", inspector_drift_mode_);

    auto* btn_row3 = new QWidget(inspector_subsystem_group_);
    auto* btn_row3_layout = new QHBoxLayout(btn_row3);
    btn_row3_layout->setContentsMargins(0, 0, 0, 0);
    btn_edit_symbol_ = new QPushButton("✏️ Edit Symbol...", btn_row3);
    btn_edit_symbol_->setStyleSheet("font-weight: bold; background: #EEF2FF; padding: 4px;");
    btn_sync_primary_ = new QPushButton("↺ Sync with Primary", btn_row3);
    btn_row3_layout->addWidget(btn_edit_symbol_);
    btn_row3_layout->addWidget(btn_sync_primary_);
    sub_layout->addRow(btn_row3);

    form_layout->addRow(inspector_subsystem_group_);
    inspector_subsystem_group_->setVisible(false);

    connect(inspector_subsystem_type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      if (index == 1) {
        QString fpath = inspector_subsystem_file_->text().trimmed();
        if (fpath.isEmpty()) {
          if (auto* n = cur_g->find_node(active_inspected_node_)) {
            fpath = QString::fromStdString(n->name + ".yaml");
            inspector_subsystem_file_->setText(fpath);
          }
        }
        canvas_->convert_subsystem_to_file(active_inspected_node_, fpath.toStdString());
      } else {
        canvas_->convert_subsystem_to_builtin(active_inspected_node_);
      }
      set_document_modified(true);
      update_inspector();
    });

    connect(inspector_subsystem_file_, &QLineEdit::editingFinished, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ != madi::core::InvalidNodeId && cur_g) {
        const QString fpath = inspector_subsystem_file_->text().trimmed();
        canvas_->convert_subsystem_to_file(active_inspected_node_, fpath.toStdString());
        set_document_modified(true);
      }
    });

    connect(btn_browse_subsystem_file_, &QPushButton::clicked, this, [this]() {
      if (active_inspected_node_ == madi::core::InvalidNodeId) return;
      const QString fpath = QFileDialog::getOpenFileName(this, "Select Subsystem File", "", "Supported Formats (*.yaml *.drawio *.xml);;All Files (*)");
      if (!fpath.isEmpty()) {
        inspector_subsystem_file_->setText(fpath);
        canvas_->convert_subsystem_to_file(active_inspected_node_, fpath.toStdString());
        set_document_modified(true);
        update_inspector();
      }
    });

    connect(btn_subsystem_drill_, &QPushButton::clicked, this, [this]() {
      if (active_inspected_node_ != madi::core::InvalidNodeId) {
        canvas_->drill_down_node(active_inspected_node_);
      }
    });

    connect(btn_subsystem_save_, &QPushButton::clicked, this, [this]() {
      if (active_inspected_node_ != madi::core::InvalidNodeId) {
        canvas_->save_subsystem_file(active_inspected_node_);
      }
    });

    connect(btn_subsystem_reload_, &QPushButton::clicked, this, [this]() {
      if (active_inspected_node_ != madi::core::InvalidNodeId) {
        canvas_->reload_subsystem_file(active_inspected_node_);
      }
    });

    connect(btn_subsystem_convert_, &QPushButton::clicked, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      if (n->is_file_subsystem()) {
        canvas_->convert_subsystem_to_builtin(active_inspected_node_);
      } else {
        const QString fpath = QFileDialog::getSaveFileName(this, "Export Subsystem to File", QString::fromStdString(n->name + ".madi"), "Madí Diagram (*.madi);;Madí YAML (*.yaml);;All Files (*)");
        if (!fpath.isEmpty()) {
          canvas_->convert_subsystem_to_file(active_inspected_node_, fpath.toStdString());
        }
      }
      update_inspector();
    });

    connect(inspector_drift_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      if (auto* n = cur_g->find_node(active_inspected_node_)) {
        n->properties["definition_sync_mode"] = (index == 1) ? "allow_drift" : "follow_primary";
        set_document_modified(true);
      }
    });

    connect(btn_edit_symbol_, &QPushButton::clicked, this, [this]() {
      if (active_inspected_node_ != madi::core::InvalidNodeId) {
        open_symbol_editor(active_inspected_node_);
      }
    });

    connect(btn_sync_primary_, &QPushButton::clicked, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      auto* child_g = canvas_->ensure_subsystem_graph(active_inspected_node_);
      if (child_g) {
        madi::core::SymbolGenerator::sync_symbol_from_diagram(*n, *cur_g, *child_g);
        cur_g->update_all_edge_routes();
        set_document_modified(true);
        update_inspector();
        canvas_->update();
      }
    });

    // Appearance & Styling Group
    inspector_style_group_ = new QGroupBox("Appearance & Styling", inspector_widget);
    auto* style_layout = new QFormLayout(inspector_style_group_);

    btn_pick_fill_ = new QPushButton("Pick Fill...", inspector_style_group_);
    connect(btn_pick_fill_, &QPushButton::clicked, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      QColor initial = argb_to_qcolor(canvas_->diagram_style().render_style.fill_color);
      auto it = n->properties.find("fill_color");
      if (it != n->properties.end()) {
        QColor parsed(QString::fromStdString(it->second));
        if (parsed.isValid()) initial = parsed;
      }
      QColor chosen = QColorDialog::getColor(initial, this, "Choose Shape Fill Color");
      if (chosen.isValid()) {
        n->properties["fill_color"] = chosen.name().toStdString();
        update_inspector();
        canvas_->update();
      }
    });

    btn_pick_stroke_ = new QPushButton("Pick Stroke...", inspector_style_group_);
    connect(btn_pick_stroke_, &QPushButton::clicked, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      QColor initial = argb_to_qcolor(canvas_->diagram_style().render_style.stroke_color);
      auto it = n->properties.find("stroke_color");
      if (it != n->properties.end()) {
        QColor parsed(QString::fromStdString(it->second));
        if (parsed.isValid()) initial = parsed;
      }
      QColor chosen = QColorDialog::getColor(initial, this, "Choose Shape Stroke Color");
      if (chosen.isValid()) {
        n->properties["stroke_color"] = chosen.name().toStdString();
        update_inspector();
        canvas_->update();
      }
    });

    btn_pick_text_ = new QPushButton("Pick Text...", inspector_style_group_);
    connect(btn_pick_text_, &QPushButton::clicked, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      QColor initial = argb_to_qcolor(canvas_->diagram_style().render_style.text_color);
      auto it = n->properties.find("text_color");
      if (it != n->properties.end()) {
        QColor parsed(QString::fromStdString(it->second));
        if (parsed.isValid()) initial = parsed;
      }
      QColor chosen = QColorDialog::getColor(initial, this, "Choose Shape Text Color");
      if (chosen.isValid()) {
        n->properties["text_color"] = chosen.name().toStdString();
        update_inspector();
        canvas_->update();
      }
    });

    btn_reset_style_ = new QPushButton("↺ Reset to Theme Defaults", inspector_style_group_);
    connect(btn_reset_style_, &QPushButton::clicked, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
      auto* n = cur_g->find_node(active_inspected_node_);
      if (!n) return;
      n->properties.erase("fill_color");
      n->properties.erase("stroke_color");
      n->properties.erase("text_color");
      n->properties.erase("stroke_width");
      n->properties.erase("font_family");
      n->properties.erase("font_size");
      n->properties.erase("font_bold");
      n->properties.erase("font_italic");
      n->properties.erase("font_underline");
      update_inspector();
      canvas_->update();
    });

    auto* insp_swatches_layout = new QHBoxLayout();
    insp_swatches_layout->setSpacing(2);
    const struct SwatchDef { const char* hex; const char* name; } swatches[] = {
      {"#334155", "Slate"},
      {"#EF4444", "Red"},
      {"#F97316", "Orange"},
      {"#F59E0B", "Amber"},
      {"#10B981", "Emerald"},
      {"#14B8A6", "Teal"},
      {"#0EA5E9", "Sky Blue"},
      {"#3B82F6", "Blue"},
      {"#6366F1", "Indigo"},
      {"#EC4899", "Pink"}
    };
    for (const auto& sw : swatches) {
      auto* btn = new QPushButton(inspector_style_group_);
      btn->setFixedSize(18, 18);
      btn->setToolTip(QString("%1 (%2)").arg(sw.name, sw.hex));
      btn->setStyleSheet(QString("background-color: %1; border: 1px solid #475569; border-radius: 3px;").arg(sw.hex));
      QString hex_str = sw.hex;
      connect(btn, &QPushButton::clicked, this, [this, hex_str]() {
        auto* cur_g = current_diagram_graph();
        if (active_inspected_node_ == madi::core::InvalidNodeId || !cur_g) return;
        auto* n = cur_g->find_node(active_inspected_node_);
        if (!n) return;
        n->properties["fill_color"] = hex_str.toStdString();
        update_inspector();
        canvas_->update();
      });
      insp_swatches_layout->addWidget(btn);
    }
    style_layout->addRow("Palette:", insp_swatches_layout);
    style_layout->addRow("Fill:", btn_pick_fill_);
    style_layout->addRow("Stroke:", btn_pick_stroke_);
    style_layout->addRow("Text:", btn_pick_text_);
    style_layout->addRow(btn_reset_style_);
    form_layout->addRow(inspector_style_group_);
    inspector_style_group_->setVisible(false);

    // Arrow / Connection Net Group
    inspector_edge_group_ = new QGroupBox("Arrow / Net Configuration", inspector_widget);
    auto* edge_layout = new QFormLayout(inspector_edge_group_);

    inspector_edge_name_ = new QLineEdit(inspector_edge_group_);
    connect(inspector_edge_name_, &QLineEdit::editingFinished, this, [this]() {
      auto* cur_g = current_diagram_graph();
      if (canvas_ && canvas_->selected_edge() != madi::core::InvalidEdgeId && cur_g) {
        if (auto* e = cur_g->find_edge(canvas_->selected_edge())) {
          if (e->name != inspector_edge_name_->text().toStdString()) {
            e->name = inspector_edge_name_->text().toStdString();
            set_document_modified(true);
            canvas_->update();
          }
        }
      }
    });

    inspector_edge_type_ = new QLabel("Arrow Net", inspector_edge_group_);
    inspector_edge_type_->setStyleSheet("QLabel { font-weight: bold; color: #2B6CB0; }");

    inspector_edge_aligned_cb_ = new QCheckBox("Align Multipoint Lines", inspector_edge_group_);
    inspector_edge_aligned_cb_->setToolTip("When enabled, multipoint fan-out branches share an aligned bus spine and enter destinations in parallel.");
    connect(inspector_edge_aligned_cb_, &QCheckBox::toggled, this, [this](bool checked) {
      if (canvas_ && canvas_->selected_edge() != madi::core::InvalidEdgeId) {
        canvas_->set_edge_branches_aligned(canvas_->selected_edge(), checked);
        set_document_modified(true);
      }
    });

    edge_layout->addRow("Name:", inspector_edge_name_);
    edge_layout->addRow("Type:", inspector_edge_type_);
    edge_layout->addRow("Alignment:", inspector_edge_aligned_cb_);
    form_layout->addRow(inspector_edge_group_);
    inspector_edge_group_->setVisible(false);

    dock_inspector_->setWidget(inspector_widget);
    addDockWidget(Qt::RightDockWidgetArea, dock_inspector_);

    // 3. Command Console Dock
    console_dock_ = new CommandConsoleDock(this);
    console_dock_->setObjectName("CommandConsoleDock");
    console_dock_->set_canvas(canvas_);
    console_dock_->set_grid_manager(grid_manager_.get());
    console_dock_->set_new_callback([this]() {
      new_diagram();
    });
    console_dock_->set_save_callback([this](const QString& p) {
      if (p.isEmpty()) save_diagram();
      else save_diagram_path(p);
    });
    console_dock_->set_open_callback([this](const QString& p) {
      if (p.isEmpty()) open_diagram();
      else open_diagram_path(p);
    });
    console_dock_->set_export_callback([this](const QString& fmt, const QString& p) {
      const QString f = fmt.toLower();
      if (f == "svg") {
        if (p.isEmpty()) export_svg();
        else export_svg_path(p);
      } else if (f == "png") {
        if (p.isEmpty()) export_png();
        else export_image_path(p, "PNG");
      } else if (f == "jpg" || f == "jpeg") {
        if (p.isEmpty()) export_jpeg();
        else export_image_path(p, "JPEG");
      } else if (f == "xml" || f == "drawio") {
        if (p.isEmpty()) export_drawio();
        else {
          madi::io::DrawioParser parser;
          std::string xml = parser.export_xml(*current_diagram_graph());
          std::ofstream out(p.toStdString());
          if (out.is_open()) {
            out << xml;
            statusBar()->showMessage(QString("Exported Draw.io XML to %1").arg(p), 4000);
            if (console_dock_) console_dock_->log_info("Exported Draw.io XML to " + p);
          }
        }
      } else {
        if (console_dock_) console_dock_->log_error("Unknown export format: " + fmt + ". Supported: svg, png, jpg, xml, drawio");
      }
    });
    console_dock_->set_settings_callback([this](int tab) {
      open_snap_grid_settings(tab);
    });
    console_dock_->set_symbol_edit_callback([this](const QString& name) {
      auto* cur_g = current_diagram_graph();
      if (!cur_g) return;
      madi::core::NodeId target_id = active_inspected_node_;
      if (!name.isEmpty()) {
        for (const auto& [nid, n] : cur_g->nodes()) {
          if (QString::fromStdString(n.name).compare(name, Qt::CaseInsensitive) == 0) {
            target_id = nid;
            break;
          }
        }
      }
      if (target_id != madi::core::InvalidNodeId) {
        open_symbol_editor(target_id);
      }
    });
    addDockWidget(Qt::BottomDockWidgetArea, console_dock_);
  }

public:
  void update_inspector() {
    const bool can_merge = canvas_->can_merge_selected();
    const bool can_split = canvas_->can_split_selected();
    const bool can_down = canvas_->can_drill_down();
    const bool can_up = canvas_->can_pop_scope();

    action_merge_->setEnabled(can_merge);
    if (action_bool_intersect_) action_bool_intersect_->setEnabled(can_merge);
    if (action_bool_subtract_) action_bool_subtract_->setEnabled(can_merge);
    if (action_bool_xor_) action_bool_xor_->setEnabled(can_merge);
    action_split_->setEnabled(can_split);

    const bool has_selection = !canvas_->selected_nodes().empty();
    const bool has_any_selection = has_selection || (canvas_->selected_edge() != madi::core::InvalidEdgeId);
    if (action_delete_) action_delete_->setEnabled(has_any_selection);
    if (action_cut_) action_cut_->setEnabled(has_selection);
    if (action_copy_) action_copy_->setEnabled(has_selection);
    if (action_duplicate_) action_duplicate_->setEnabled(has_selection);
    if (action_paste_) action_paste_->setEnabled(canvas_->has_clipboard());

    if (action_bring_to_front_) action_bring_to_front_->setEnabled(has_selection);
    if (action_bring_forward_) action_bring_forward_->setEnabled(has_selection);
    if (action_send_backward_) action_send_backward_->setEnabled(has_selection);
    if (action_send_to_back_) action_send_to_back_->setEnabled(has_selection);

    action_drill_down_->setEnabled(can_down);
    action_pop_scope_->setEnabled(can_up);
    if (action_edit_symbol_) action_edit_symbol_->setEnabled(can_down);
    if (action_sync_primary_) action_sync_primary_->setEnabled(can_down);

    if (btn_dock_merge_) btn_dock_merge_->setEnabled(can_merge);
    if (btn_dock_intersect_) btn_dock_intersect_->setEnabled(can_merge);
    if (btn_dock_subtract_) btn_dock_subtract_->setEnabled(can_merge);
    if (btn_dock_xor_) btn_dock_xor_->setEnabled(can_merge);
    if (btn_dock_split_) btn_dock_split_->setEnabled(can_split);
    if (btn_dock_down_) btn_dock_down_->setEnabled(can_down);
    if (btn_dock_up_) btn_dock_up_->setEnabled(can_up);
    if (btn_palette_del_) btn_palette_del_->setEnabled(has_any_selection);
    if (btn_dock_front_) btn_dock_front_->setEnabled(has_selection);
    if (btn_dock_forward_) btn_dock_forward_->setEnabled(has_selection);
    if (btn_dock_backward_) btn_dock_backward_->setEnabled(has_selection);
    if (btn_dock_back_) btn_dock_back_->setEnabled(has_selection);
    if (btn_dock_rot_cw_) btn_dock_rot_cw_->setEnabled(has_selection);
    if (btn_dock_rot_ccw_) btn_dock_rot_ccw_->setEnabled(has_selection);
    if (btn_insp_rot_cw_) btn_insp_rot_cw_->setEnabled(has_selection);
    if (btn_insp_rot_ccw_) btn_insp_rot_ccw_->setEnabled(has_selection);

    auto* cur_g = current_diagram_graph();
    if (!cur_g) return;

    // Inspect selected wire / edge if no nodes are selected
    if (canvas_->selected_edge() != madi::core::InvalidEdgeId && canvas_->selected_nodes().empty()) {
      if (const auto* edge = cur_g->find_edge(canvas_->selected_edge())) {
        if (inspector_edge_group_) {
          inspector_edge_group_->setVisible(true);
          inspector_edge_name_->setText(QString::fromStdString(edge->name));
          if (edge->is_multipoint()) {
            inspector_edge_type_->setText(QString("Multipoint Arrow (%1 targets)").arg(edge->extra_destinations.size() + 1));
            inspector_edge_aligned_cb_->setEnabled(true);
            inspector_edge_aligned_cb_->blockSignals(true);
            inspector_edge_aligned_cb_->setChecked(edge->is_branches_aligned());
            inspector_edge_aligned_cb_->blockSignals(false);
          } else {
            inspector_edge_type_->setText("Single Arrow / Net");
            inspector_edge_aligned_cb_->setEnabled(false);
            inspector_edge_aligned_cb_->blockSignals(true);
            inspector_edge_aligned_cb_->setChecked(edge->is_branches_aligned());
            inspector_edge_aligned_cb_->blockSignals(false);
          }
        }
        active_inspected_node_ = madi::core::InvalidNodeId;
        inspector_name_->clear();
        inspector_shape_type_->setText("None");
        inspector_x_->setValue(0.0);
        inspector_y_->setValue(0.0);
        inspector_w_->setValue(0.0);
        inspector_h_->setValue(0.0);
        inspector_rotation_->setValue(0.0);
        if (inspector_subsystem_group_) inspector_subsystem_group_->setVisible(false);
        if (inspector_style_group_) inspector_style_group_->setVisible(false);
        return;
      }
    } else {
      if (inspector_edge_group_) inspector_edge_group_->setVisible(false);
    }

    for (const auto& [nid, n] : cur_g->nodes()) {
      if (n.selected) {
        active_inspected_node_ = nid;
        inspector_name_->setText(QString::fromStdString(n.name));
        inspector_x_->setValue(n.bounds.x);
        inspector_y_->setValue(n.bounds.y);
        inspector_w_->setValue(n.bounds.width);
        inspector_h_->setValue(n.bounds.height);
        inspector_rotation_->setValue(n.rotation_deg);

        QString shape_str = "Rectangle";
        if (n.shape == madi::core::ShapeType::RoundedRect) shape_str = "Rounded Rectangle";
        else if (n.shape == madi::core::ShapeType::Ellipse) shape_str = "Ellipse";
        else if (n.shape == madi::core::ShapeType::HierarchicalBlock) shape_str = "Subsystem Block";
        else if (n.shape == madi::core::ShapeType::CustomPolygon) {
          shape_str = QString("Polygon (%1 vertices)").arg(n.get_world_polygon_vertices().size());
        }

        auto it_m = n.properties.find("is_merged_figure");
        if (it_m != n.properties.end() && it_m->second == "true") {
          shape_str += " [Merged Composite]";
        }

        inspector_shape_type_->setText(shape_str);

        if (n.is_subsystem()) {
          inspector_subsystem_group_->setVisible(true);
          const bool is_file = n.is_file_subsystem();
          inspector_subsystem_type_->blockSignals(true);
          inspector_subsystem_type_->setCurrentIndex(is_file ? 1 : 0);
          inspector_subsystem_type_->blockSignals(false);

          inspector_subsystem_file_->setEnabled(is_file);
          btn_browse_subsystem_file_->setEnabled(is_file);
          btn_subsystem_save_->setEnabled(is_file);
          btn_subsystem_reload_->setEnabled(is_file);

          if (is_file) {
            inspector_subsystem_file_->setText(QString::fromStdString(n.subsystem_file()));
            btn_subsystem_convert_->setText("📥 Embed in Diagram");
            btn_subsystem_convert_->setToolTip("Embed external file content into this diagram");
          } else {
            inspector_subsystem_file_->clear();
            btn_subsystem_convert_->setText("📤 Export to File...");
            btn_subsystem_convert_->setToolTip("Save this subsystem diagram into a separate external file");
          }

          if (inspector_drift_mode_) {
            inspector_drift_mode_->blockSignals(true);
            const auto it_drift = n.properties.find("definition_sync_mode");
            const bool is_drift = (it_drift != n.properties.end() && it_drift->second == "allow_drift");
            inspector_drift_mode_->setCurrentIndex(is_drift ? 1 : 0);
            inspector_drift_mode_->blockSignals(false);
          }
        } else {
          inspector_subsystem_group_->setVisible(false);
        }

        if (inspector_style_group_) {
          inspector_style_group_->setVisible(true);

          auto it_f = n.properties.find("fill_color");
          if (it_f != n.properties.end()) {
            btn_pick_fill_->setText(QString("Fill: %1 (Custom)").arg(QString::fromStdString(it_f->second)));
          } else {
            btn_pick_fill_->setText(QString("Fill: %1 (Default)").arg(argb_to_hex(canvas_->diagram_style().render_style.fill_color)));
          }

          auto it_s = n.properties.find("stroke_color");
          if (it_s != n.properties.end()) {
            btn_pick_stroke_->setText(QString("Stroke: %1 (Custom)").arg(QString::fromStdString(it_s->second)));
          } else {
            btn_pick_stroke_->setText(QString("Stroke: %1 (Default)").arg(argb_to_hex(canvas_->diagram_style().render_style.stroke_color)));
          }

          auto it_t = n.properties.find("text_color");
          if (it_t != n.properties.end()) {
            btn_pick_text_->setText(QString("Text: %1 (Custom)").arg(QString::fromStdString(it_t->second)));
          } else {
            btn_pick_text_->setText(QString("Text: %1 (Default)").arg(argb_to_hex(canvas_->diagram_style().render_style.text_color)));
          }
        }

        suppress_inspector_updates_ = true;
        if (combo_font_family_) {
          auto it_ff = n.properties.find("font_family");
          if (it_ff != n.properties.end() && !it_ff->second.empty()) {
            combo_font_family_->setCurrentText(QString::fromStdString(it_ff->second));
          } else {
            combo_font_family_->setCurrentText("Sans Serif");
          }
        }
        if (combo_font_size_) {
          auto it_fs = n.properties.find("font_size");
          if (it_fs != n.properties.end()) {
            combo_font_size_->setCurrentText(QString::fromStdString(it_fs->second));
          } else {
            combo_font_size_->setCurrentText("10");
          }
        }
        if (btn_bold_) {
          auto it_fb = n.properties.find("font_bold");
          btn_bold_->setChecked(it_fb != n.properties.end() && (it_fb->second == "true" || it_fb->second == "1"));
        }
        if (btn_italic_) {
          auto it_fi = n.properties.find("font_italic");
          btn_italic_->setChecked(it_fi != n.properties.end() && (it_fi->second == "true" || it_fi->second == "1"));
        }
        if (btn_underline_) {
          auto it_fu = n.properties.find("font_underline");
          btn_underline_->setChecked(it_fu != n.properties.end() && (it_fu->second == "true" || it_fu->second == "1"));
        }
        if (combo_stroke_width_) {
          auto it_sw = n.properties.find("stroke_width");
          if (it_sw != n.properties.end()) {
            combo_stroke_width_->setCurrentText(QString::fromStdString(it_sw->second) + " px");
          } else {
            combo_stroke_width_->setCurrentIndex(2);
          }
        }
        suppress_inspector_updates_ = false;

        return;
      }
    }

    suppress_inspector_updates_ = true;
    if (btn_bold_) btn_bold_->setChecked(false);
    if (btn_italic_) btn_italic_->setChecked(false);
    if (btn_underline_) btn_underline_->setChecked(false);
    suppress_inspector_updates_ = false;

    active_inspected_node_ = madi::core::InvalidNodeId;
    inspector_name_->clear();
    inspector_shape_type_->setText("None");
    inspector_x_->setValue(0.0);
    inspector_y_->setValue(0.0);
    inspector_w_->setValue(0.0);
    inspector_h_->setValue(0.0);
    inspector_rotation_->setValue(0.0);
    if (inspector_subsystem_group_) {
      inspector_subsystem_group_->setVisible(false);
    }
    if (inspector_style_group_) {
      inspector_style_group_->setVisible(false);
    }
    if (inspector_edge_group_) {
      inspector_edge_group_->setVisible(false);
    }
  }

public:
  [[nodiscard]] bool is_document_modified() const noexcept {
    return is_modified_;
  }

  void set_document_modified(bool mod) {
    if (is_modified_ == mod) return;
    is_modified_ = mod;
    setWindowModified(mod);
    update_window_title();
  }

  void on_model_changed() {
    if (!suppress_model_modified_) {
      set_document_modified(true);
    }
  }

  void update_window_title() {
    QString title = "Madí";
    if (!current_file_path_.isEmpty()) {
      QFileInfo fi(current_file_path_);
      title += " - " + fi.fileName();
    } else {
      title += " - Untitled";
    }
    title += "[*]";
    setWindowTitle(title);
    setWindowModified(is_modified_);
  }

  bool maybe_save() {
    if (!is_document_modified()) {
      return true;
    }

    const bool is_offscreen = QGuiApplication::platformName().contains("offscreen");
    if (is_offscreen) {
      return true;
    }

    const QMessageBox::StandardButton res = QMessageBox::warning(
        this,
        "Save Diagram",
        "The current diagram has unsaved changes.\nDo you want to save before proceeding?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (res == QMessageBox::Save) {
      return save_diagram();
    } else if (res == QMessageBox::Cancel) {
      return false;
    }
    // Discard
    return true;
  }

  bool new_diagram() {
    if (!maybe_save()) {
      return false;
    }

    if (graph_) {
      suppress_model_modified_ = true;
      if (canvas_) {
        canvas_->reset_hierarchy(graph_.get());
      }
      graph_->clear();
      if (spatial_index_) spatial_index_->clear();
      if (command_history_) command_history_->clear();
      current_file_path_.clear();
      suppress_model_modified_ = false;
      set_document_modified(false);
      update_window_title();

      if (canvas_) {
        canvas_->refresh_spatial_index();
        canvas_->reset_view();
        canvas_->update();
      }
      if (hierarchy_dock_) {
        hierarchy_dock_->set_graph_model(graph_.get());
        hierarchy_dock_->set_scope_info("Top", false);
      }
      update_status_hierarchy("Top");
      if (btn_pop_scope_status_) {
        btn_pop_scope_status_->setEnabled(false);
      }
      if (action_pop_scope_) {
        action_pop_scope_->setEnabled(false);
      }
      update_inspector();
      statusBar()->showMessage("New diagram created.", 3000);
    }
    return true;
  }

  bool save_diagram_path(const QString& filename) {
    if (filename.isEmpty()) return false;

    // Sync any open subsystem modifications back to root graph
    if (canvas_) {
      canvas_->sync_all_hierarchy_symbols();
    }

    std::string content_out;
    if (filename.endsWith(".madi", Qt::CaseInsensitive) ||
        filename.endsWith(".yaml", Qt::CaseInsensitive) ||
        filename.endsWith(".yml", Qt::CaseInsensitive)) {
      madi::io::StorageEngine storage;
      content_out = storage.serialize_yaml(*graph_);
    } else {
      // Default file format: Draw.io XML
      madi::io::DrawioParser parser;
      content_out = parser.export_xml(*graph_);
    }

    std::ofstream out(filename.toStdString());
    if (!out.is_open()) {
      statusBar()->showMessage("Cannot write to selected file: " + filename, 4000);
      return false;
    }
    out << content_out;
    current_file_path_ = filename;
    set_document_modified(false);
    update_window_title();
    const QString msg = QString("Saved diagram to %1").arg(filename);
    statusBar()->showMessage(msg, 4000);
    if (console_dock_) console_dock_->log_info(msg);
    return true;
  }

  bool save_diagram() {
    if (current_file_path_.isEmpty()) {
      return save_diagram_as();
    }
    return save_diagram_path(current_file_path_);
  }

  bool save_diagram_as() {
    const QString default_name = current_file_path_.isEmpty() ? "diagram.madi" : current_file_path_;
    const QString filename = QFileDialog::getSaveFileName(
        this, "Save Diagram", default_name,
        "Madí Diagram (*.madi);;Draw.io XML Diagram (*.xml *.drawio);;Madí YAML (*.yaml *.yml);;All Files (*)");
    if (filename.isEmpty()) {
      return false;
    }
    return save_diagram_path(filename);
  }

  void open_diagram() {
    if (!maybe_save()) {
      return;
    }
    const QString filename = QFileDialog::getOpenFileName(
        this, "Open Diagram", "",
        "Supported Formats (*.madi *.xml *.drawio *.yaml *.yml *.diag);;Madí Diagram (*.madi);;Draw.io XML Diagram (*.xml *.drawio);;Madí YAML (*.yaml *.yml);;All Files (*)");
    if (!filename.isEmpty()) {
      open_diagram_path(filename);
    }
  }

  bool open_diagram_path(const QString& filename) {
    if (filename.isEmpty()) return false;

    std::ifstream in(filename.toStdString());
    if (!in.is_open()) {
      statusBar()->showMessage("Cannot open selected file: " + filename, 4000);
      return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    suppress_model_modified_ = true;
    if (canvas_) {
      canvas_->reset_hierarchy(graph_.get());
    }
    graph_->clear();

    const bool is_yaml = filename.endsWith(".madi", Qt::CaseInsensitive) ||
                         filename.endsWith(".yaml", Qt::CaseInsensitive) ||
                         filename.endsWith(".yml", Qt::CaseInsensitive);
    bool imported = false;
    std::string err_msg;

    if (!is_yaml) {
      madi::io::DrawioParser parser;
      auto res = parser.import_xml(content, *graph_);
      if (res.success) {
        imported = true;
      } else {
        err_msg = res.error_message;
      }
    }

    if (!imported) {
      madi::io::StorageEngine storage;
      auto res = storage.deserialize_yaml(content, *graph_);
      if (res.success) {
        imported = true;
      } else if (err_msg.empty()) {
        err_msg = res.error_message;
      }
    }

    if (!imported) {
      suppress_model_modified_ = false;
      statusBar()->showMessage("Import Error: " + QString::fromStdString(err_msg), 4000);
      return false;
    }

    if (command_history_) command_history_->clear();
    current_file_path_ = filename;
    suppress_model_modified_ = false;
    set_document_modified(false);
    update_window_title();

    if (canvas_) {
      canvas_->refresh_spatial_index();
      canvas_->fit_content();
      canvas_->update();
    }
    if (hierarchy_dock_) {
      hierarchy_dock_->set_graph_model(graph_.get());
      hierarchy_dock_->set_scope_info("Top", false);
    }
    update_status_hierarchy("Top");
    if (btn_pop_scope_status_) {
      btn_pop_scope_status_->setEnabled(false);
    }
    if (action_pop_scope_) {
      action_pop_scope_->setEnabled(false);
    }
    const QString msg = QString("Opened %1 (%2 nodes, %3 edges)")
        .arg(filename)
        .arg(graph_->node_count())
        .arg(graph_->edge_count());
    statusBar()->showMessage(msg, 4000);
    if (console_dock_) console_dock_->log_info(msg);
    return true;
  }

  void export_drawio() {
    const QString filename = QFileDialog::getSaveFileName(
        this, "Export Draw.io XML", "diagram.drawio", "Draw.io Diagram (*.drawio *.xml);;All Files (*)");
    if (filename.isEmpty()) return;

    auto* cur_g = current_diagram_graph();
    if (!cur_g) return;

    madi::io::DrawioParser parser;
    std::string xml = parser.export_xml(*cur_g);

    std::ofstream out(filename.toStdString());
    if (!out.is_open()) {
      QMessageBox::warning(this, "Error", "Cannot write to export file.");
      return;
    }
    out << xml;
    statusBar()->showMessage(QString("Exported Draw.io XML to %1").arg(filename), 4000);
    if (console_dock_) console_dock_->log_info("Exported Draw.io XML to " + filename);
  }

  bool export_svg_path(const QString& filename) {
    auto* cur_g = current_diagram_graph();
    if (filename.isEmpty() || !cur_g) return false;

    // Calculate bounding box of diagram content
    double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
    bool has_content = false;

    for (const auto& [nid, n] : cur_g->nodes()) {
      has_content = true;
      min_x = std::min(min_x, n.bounds.x);
      min_y = std::min(min_y, n.bounds.y);
      max_x = std::max(max_x, n.bounds.x + n.bounds.width);
      max_y = std::max(max_y, n.bounds.y + n.bounds.height);
    }
    for (const auto& [eid, e] : cur_g->edges()) {
      for (const auto& pt : e.waypoints) {
        has_content = true;
        min_x = std::min(min_x, pt.x);
        min_y = std::min(min_y, pt.y);
        max_x = std::max(max_x, pt.x);
        max_y = std::max(max_y, pt.y);
      }
    }

    if (!has_content) {
      min_x = 0; min_y = 0; max_x = 800; max_y = 600;
    }

    const double margin = 40.0;
    min_x -= margin;
    min_y -= margin;
    max_x += margin;
    max_y += margin;
    const double width = std::max(100.0, max_x - min_x);
    const double height = std::max(100.0, max_y - min_y);

    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" ";
    ss << "width=\"" << width << "\" height=\"" << height << "\" ";
    ss << "viewBox=\"" << min_x << " " << min_y << " " << width << " " << height << "\">\n";

    const auto& ds = canvas_->diagram_style();
    ss << "  <rect x=\"" << min_x << "\" y=\"" << min_y << "\" width=\"" << width << "\" height=\"" << height << "\" ";
    ss << "fill=\"" << argb_to_hex(ds.page_bg).toStdString() << "\"/>\n";

    // Wires / Edges
    for (const auto& [eid, edge] : cur_g->edges()) {
      if (!edge.visible || edge.waypoints.empty()) continue;
      std::string stroke_c = argb_to_hex(ds.render_style.wire_color).toStdString();
      auto it_sc = edge.properties.find("stroke_color");
      if (it_sc != edge.properties.end() && !it_sc->second.empty()) stroke_c = it_sc->second;
      double stroke_w = 1.5;
      auto it_sw = edge.properties.find("stroke_width");
      if (it_sw != edge.properties.end()) {
        try { stroke_w = std::stod(it_sw->second); } catch (...) {}
      }

      ss << "  <polyline points=\"";
      for (size_t i = 0; i < edge.waypoints.size(); ++i) {
        if (i > 0) ss << " ";
        ss << edge.waypoints[i].x << "," << edge.waypoints[i].y;
      }
      ss << "\" fill=\"none\" stroke=\"" << stroke_c << "\" stroke-width=\"" << stroke_w << "\" stroke-linejoin=\"round\" stroke-linecap=\"round\"/>\n";
      if (!edge.waypoints.empty()) {
        ss << "  <circle cx=\"" << edge.waypoints.front().x << "\" cy=\"" << edge.waypoints.front().y << "\" r=\"2.5\" fill=\"" << stroke_c << "\"/>\n";
      }
    }

    // Nodes (sorted by z_order)
    std::vector<const madi::core::Node*> sorted_nodes;
    sorted_nodes.reserve(cur_g->node_count());
    for (const auto& [nid, n] : cur_g->nodes()) {
      sorted_nodes.push_back(&n);
    }
    std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const auto* a, const auto* b) {
      if (a->z_order != b->z_order) return a->z_order < b->z_order;
      return a->id < b->id;
    });

    auto xml_esc = [](const std::string& str) {
      std::string out;
      for (char c : str) {
        switch (c) {
          case '&': out += "&amp;"; break;
          case '<': out += "&lt;"; break;
          case '>': out += "&gt;"; break;
          case '\"': out += "&quot;"; break;
          case '\'': out += "&apos;"; break;
          default: out += c; break;
        }
      }
      return out;
    };

    for (const auto* n_ptr : sorted_nodes) {
      const auto& n = *n_ptr;
      std::string fill_c = argb_to_hex(ds.render_style.fill_color).toStdString();
      auto it_f = n.properties.find("fill_color");
      if (it_f != n.properties.end() && !it_f->second.empty()) fill_c = it_f->second;

      std::string stroke_c = argb_to_hex(ds.render_style.stroke_color).toStdString();
      auto it_s = n.properties.find("stroke_color");
      if (it_s != n.properties.end() && !it_s->second.empty()) stroke_c = it_s->second;

      double stroke_w = 1.5;
      auto it_sw = n.properties.find("stroke_width");
      if (it_sw != n.properties.end()) {
        try { stroke_w = std::stod(it_sw->second); } catch (...) {}
      }

      std::string font_family = "sans-serif";
      auto it_ff = n.properties.find("font_family");
      if (it_ff != n.properties.end() && !it_ff->second.empty()) font_family = it_ff->second;

      int font_size = 12;
      auto it_fs = n.properties.find("font_size");
      if (it_fs != n.properties.end()) {
        try { font_size = std::stoi(it_fs->second); } catch (...) {}
      }

      bool is_bold = false;
      auto it_fb = n.properties.find("font_bold");
      if (it_fb != n.properties.end() && (it_fb->second == "true" || it_fb->second == "1")) is_bold = true;

      bool is_italic = false;
      auto it_fi = n.properties.find("font_italic");
      if (it_fi != n.properties.end() && (it_fi->second == "true" || it_fi->second == "1")) is_italic = true;

      std::string text_c = argb_to_hex(ds.render_style.text_color).toStdString();
      auto it_tc = n.properties.find("text_color");
      if (it_tc != n.properties.end() && !it_tc->second.empty()) text_c = it_tc->second;

      if (n.shape == madi::core::ShapeType::Rectangle || n.shape == madi::core::ShapeType::HierarchicalBlock) {
        ss << "  <rect x=\"" << n.bounds.x << "\" y=\"" << n.bounds.y << "\" width=\"" << n.bounds.width << "\" height=\"" << n.bounds.height
           << "\" fill=\"" << fill_c << "\" stroke=\"" << stroke_c << "\" stroke-width=\"" << stroke_w << "\"/>\n";
      } else if (n.shape == madi::core::ShapeType::RoundedRect) {
        ss << "  <rect x=\"" << n.bounds.x << "\" y=\"" << n.bounds.y << "\" width=\"" << n.bounds.width << "\" height=\"" << n.bounds.height
           << "\" rx=\"8\" ry=\"8\" fill=\"" << fill_c << "\" stroke=\"" << stroke_c << "\" stroke-width=\"" << stroke_w << "\"/>\n";
      } else if (n.shape == madi::core::ShapeType::Ellipse) {
        ss << "  <ellipse cx=\"" << (n.bounds.x + n.bounds.width * 0.5) << "\" cy=\"" << (n.bounds.y + n.bounds.height * 0.5)
           << "\" rx=\"" << (n.bounds.width * 0.5) << "\" ry=\"" << (n.bounds.height * 0.5)
           << "\" fill=\"" << fill_c << "\" stroke=\"" << stroke_c << "\" stroke-width=\"" << stroke_w << "\"/>\n";
      } else if (n.shape == madi::core::ShapeType::CustomPolygon && !n.polygon_vertices.empty()) {
        ss << "  <polygon points=\"";
        for (size_t i = 0; i < n.polygon_vertices.size(); ++i) {
          if (i > 0) ss << " ";
          ss << n.polygon_vertices[i].x << "," << n.polygon_vertices[i].y;
        }
        ss << "\" fill=\"" << fill_c << "\" stroke=\"" << stroke_c << "\" stroke-width=\"" << stroke_w << "\"/>\n";
      }

      // Text label
      if (!n.name.empty()) {
        const double cx = n.bounds.x + n.bounds.width * 0.5;
        const double cy = n.bounds.y + n.bounds.height * 0.5;
        ss << "  <text x=\"" << cx << "\" y=\"" << cy << "\" "
           << "font-family=\"" << font_family << "\" font-size=\"" << font_size << "\" "
           << (is_bold ? "font-weight=\"bold\" " : "")
           << (is_italic ? "font-style=\"italic\" " : "")
           << "fill=\"" << text_c << "\" text-anchor=\"middle\" dominant-baseline=\"central\">"
           << xml_esc(n.name) << "</text>\n";
      }

      // Ports
      for (madi::core::PortId pid : n.port_ids) {
        if (const auto* p = cur_g->find_port(pid)) {
          auto pt = n.resolve_port_position(*p);
          ss << "  <circle cx=\"" << pt.x << "\" cy=\"" << pt.y << "\" r=\"3.5\" fill=\"#38BDF8\" stroke=\"#0F172A\" stroke-width=\"1.0\"/>\n";
        }
      }
    }

    ss << "</svg>\n";

    std::ofstream out(filename.toStdString());
    if (!out.is_open()) {
      statusBar()->showMessage("Cannot open file for SVG export: " + filename, 4000);
      return false;
    }
    out << ss.str();
    const QString msg = QString("Exported SVG diagram to %1").arg(filename);
    statusBar()->showMessage(msg, 4000);
    if (console_dock_) console_dock_->log_info(msg);
    return true;
  }

  void export_svg() {
    const QString filename = QFileDialog::getSaveFileName(
        this, "Export Scalable Vector Graphics (SVG)", "diagram.svg", "SVG Diagram (*.svg);;All Files (*)");
    if (!filename.isEmpty()) export_svg_path(filename);
  }

  bool export_image_path(const QString& filename, const char* format = "PNG", double scale = 2.0) {
    auto* cur_g = current_diagram_graph();
    if (filename.isEmpty() || !cur_g) return false;

    double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
    bool has_content = false;

    for (const auto& [nid, n] : cur_g->nodes()) {
      has_content = true;
      min_x = std::min(min_x, n.bounds.x);
      min_y = std::min(min_y, n.bounds.y);
      max_x = std::max(max_x, n.bounds.x + n.bounds.width);
      max_y = std::max(max_y, n.bounds.y + n.bounds.height);
    }
    for (const auto& [eid, e] : cur_g->edges()) {
      for (const auto& pt : e.waypoints) {
        has_content = true;
        min_x = std::min(min_x, pt.x);
        min_y = std::min(min_y, pt.y);
        max_x = std::max(max_x, pt.x);
        max_y = std::max(max_y, pt.y);
      }
    }

    if (!has_content) {
      min_x = 0; min_y = 0; max_x = 800; max_y = 600;
    }

    const double margin = 40.0;
    min_x -= margin;
    min_y -= margin;
    max_x += margin;
    max_y += margin;
    const double w = std::max(100.0, max_x - min_x);
    const double h = std::max(100.0, max_y - min_y);

    const int img_w = static_cast<int>(std::ceil(w * scale));
    const int img_h = static_cast<int>(std::ceil(h * scale));

    QImage img(img_w, img_h, QImage::Format_ARGB32_Premultiplied);
    const auto& ds = canvas_->diagram_style();
    img.fill(argb_to_qcolor(ds.page_bg));

    {
      QPainter painter(&img);
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setRenderHint(QPainter::TextAntialiasing, true);
      painter.scale(scale, scale);
      painter.translate(-min_x, -min_y);

      // Render edges
      for (const auto& [eid, edge] : cur_g->edges()) {
        if (!edge.visible || edge.waypoints.size() < 2) continue;
        QColor wire_c = argb_to_qcolor(ds.render_style.wire_color);
        auto it_sc = edge.properties.find("stroke_color");
        if (it_sc != edge.properties.end()) {
          QColor parsed(QString::fromStdString(it_sc->second));
          if (parsed.isValid()) wire_c = parsed;
        }
        double stroke_w = 1.5;
        auto it_sw = edge.properties.find("stroke_width");
        if (it_sw != edge.properties.end()) {
          bool ok = false;
          double psw = QString::fromStdString(it_sw->second).toDouble(&ok);
          if (ok && psw > 0.0) stroke_w = psw;
        }
        QPainterPath path;
        path.moveTo(edge.waypoints[0].x, edge.waypoints[0].y);
        for (size_t i = 1; i < edge.waypoints.size(); ++i) {
          path.lineTo(edge.waypoints[i].x, edge.waypoints[i].y);
        }
        painter.setPen(QPen(wire_c, stroke_w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        painter.setBrush(wire_c);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(edge.waypoints[0].x, edge.waypoints[0].y), 2.5, 2.5);
      }

      // Sort nodes by z_order
      std::vector<const madi::core::Node*> sorted_nodes;
      sorted_nodes.reserve(cur_g->node_count());
      for (const auto& [nid, n] : cur_g->nodes()) {
        sorted_nodes.push_back(&n);
      }
      std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(), [](const auto* a, const auto* b) {
        if (a->z_order != b->z_order) return a->z_order < b->z_order;
        return a->id < b->id;
      });

      for (const auto* n_ptr : sorted_nodes) {
        const auto& node = *n_ptr;
        const QRectF qr_f(node.bounds.x, node.bounds.y, node.bounds.width, node.bounds.height);

        QColor fill_c = argb_to_qcolor(ds.render_style.fill_color);
        auto it_f = node.properties.find("fill_color");
        if (it_f != node.properties.end()) {
          QColor parsed(QString::fromStdString(it_f->second));
          if (parsed.isValid()) fill_c = parsed;
        }

        QColor stroke_c = argb_to_qcolor(ds.render_style.stroke_color);
        auto it_s = node.properties.find("stroke_color");
        if (it_s != node.properties.end()) {
          QColor parsed(QString::fromStdString(it_s->second));
          if (parsed.isValid()) stroke_c = parsed;
        }

        double stroke_w = 1.5;
        auto it_sw = node.properties.find("stroke_width");
        if (it_sw != node.properties.end()) {
          bool ok = false;
          double psw = QString::fromStdString(it_sw->second).toDouble(&ok);
          if (ok && psw > 0.0) stroke_w = psw;
        }

        QPen strokePen(stroke_c, stroke_w, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin);

        QColor node_text_c = argb_to_qcolor(ds.render_style.text_color);
        auto it_tc = node.properties.find("text_color");
        if (it_tc != node.properties.end()) {
          QColor parsed(QString::fromStdString(it_tc->second));
          if (parsed.isValid()) node_text_c = parsed;
        }

        QFont node_font = painter.font();
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
        node_font.setPointSize(pt_size);

        auto it_fb = node.properties.find("font_bold");
        node_font.setBold(it_fb != node.properties.end() && (it_fb->second == "true" || it_fb->second == "1"));
        auto it_fi = node.properties.find("font_italic");
        node_font.setItalic(it_fi != node.properties.end() && (it_fi->second == "true" || it_fi->second == "1"));
        auto it_fu = node.properties.find("font_underline");
        node_font.setUnderline(it_fu != node.properties.end() && (it_fu->second == "true" || it_fu->second == "1"));

        painter.setFont(node_font);

        if (node.shape == madi::core::ShapeType::Rectangle || node.shape == madi::core::ShapeType::HierarchicalBlock) {
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawRect(qr_f);
          painter.setPen(node_text_c);
          painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
        } else if (node.shape == madi::core::ShapeType::RoundedRect) {
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawRoundedRect(qr_f, 8.0, 8.0);
          painter.setPen(node_text_c);
          painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
        } else if (node.shape == madi::core::ShapeType::Ellipse) {
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawEllipse(qr_f);
          painter.setPen(node_text_c);
          painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
        } else if (node.shape == madi::core::ShapeType::CustomPolygon && !node.polygon_vertices.empty()) {
          QPolygonF poly;
          for (const auto& v : node.polygon_vertices) {
            poly << QPointF(v.x, v.y);
          }
          painter.setBrush(fill_c);
          painter.setPen(strokePen);
          painter.drawPolygon(poly);
          painter.setPen(node_text_c);
          painter.drawText(qr_f, Qt::AlignCenter, QString::fromStdString(node.name));
        }

        // Ports
        for (madi::core::PortId pid : node.port_ids) {
          if (const auto* p = graph_->find_port(pid)) {
            const auto pt = node.resolve_port_position(*p);
            painter.setBrush(QColor(0x38, 0xBD, 0xF8));
            painter.setPen(QPen(QColor(0x0F, 0x17, 0x2A), 1.0));
            painter.drawEllipse(QPointF(pt.x, pt.y), 3.5, 3.5);
          }
        }
      }
    }

    if (!img.save(filename, format)) {
      statusBar()->showMessage("Failed to save image file: " + filename, 4000);
      return false;
    }
    const QString msg = QString("Exported %1 image to %2 (%3x%4 px)").arg(format).arg(filename).arg(img_w).arg(img_h);
    statusBar()->showMessage(msg, 4000);
    if (console_dock_) console_dock_->log_info(msg);
    return true;
  }

  void export_png() {
    const QString filename = QFileDialog::getSaveFileName(
        this, "Export PNG Raster Image", "diagram.png", "PNG Image (*.png);;All Files (*)");
    if (!filename.isEmpty()) export_image_path(filename, "PNG");
  }

  void export_jpeg() {
    const QString filename = QFileDialog::getSaveFileName(
        this, "Export JPEG Raster Image", "diagram.jpg", "JPEG Image (*.jpg *.jpeg);;All Files (*)");
    if (!filename.isEmpty()) export_image_path(filename, "JPEG");
  }

  void setup_sample_diagram() {
    madi::core::Node n1;
    n1.name = "Input Sensor Array";
    n1.bounds = {100.0, 160.0, 180.0, 80.0};
    n1.shape = madi::core::ShapeType::Rectangle;
    const madi::core::NodeId n1_id = graph_->add_node(std::move(n1));

    madi::core::Port p1;
    p1.parent_node = n1_id;
    p1.name = "E";
    p1.local_anchor = {1.0, 0.5};
    p1.direction = madi::core::PortDirection::East;
    const madi::core::PortId p1_id = graph_->add_port(std::move(p1));

    madi::core::Node n2;
    n2.name = "DSP Filter Block";
    n2.bounds = {420.0, 160.0, 200.0, 90.0};
    n2.shape = madi::core::ShapeType::HierarchicalBlock;
    n2.set_subsystem_builtin();
    const madi::core::NodeId n2_id = graph_->add_node(std::move(n2));

    madi::core::Port p2;
    p2.parent_node = n2_id;
    p2.name = "W";
    p2.local_anchor = {0.0, 0.5};
    p2.direction = madi::core::PortDirection::West;
    const madi::core::PortId p2_id = graph_->add_port(std::move(p2));

    madi::core::Edge edge;
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

    // Seed a sample Hexagon polygon figure as well
    madi::core::Node n3;
    n3.name = "Hexagon Sensor Logic";
    n3.bounds = {140.0, 320.0, 140.0, 90.0};
    n3.set_regular_polygon(6);
    graph_->add_node(std::move(n3));

    // Seed a sample File-Linked Subsystem
    madi::core::Node n4;
    n4.name = "ALU Unit";
    n4.bounds = {420.0, 320.0, 200.0, 90.0};
    if (QFile::exists("examples/alu_subsystem.yaml")) {
      n4.set_subsystem_file("examples/alu_subsystem.yaml");
    } else {
      n4.set_subsystem_file("alu_subsystem.yaml");
    }
    graph_->add_node(std::move(n4));

    canvas_->refresh_spatial_index();
  }

  void open_symbol_editor(madi::core::NodeId nid) {
    auto* cur_g = current_diagram_graph();
    if (nid == madi::core::InvalidNodeId || !cur_g) return;
    auto* n = cur_g->find_node(nid);
    if (!n) return;
    auto* child_g = canvas_->ensure_subsystem_graph(nid);
    SymbolEditorDialog dlg(*n, *cur_g, child_g, this);
    if (dlg.exec() == QDialog::Accepted) {
      set_document_modified(true);
      update_inspector();
      canvas_->update();
    }
  }

  // Core Members
  std::unique_ptr<madi::core::GraphModel> graph_;
  std::unique_ptr<madi::core::FlatBvhSpatialIndex> spatial_index_;
  std::unique_ptr<madi::core::CommandHistory> command_history_;
  std::unique_ptr<madi::grid::GridManager> grid_manager_;
  std::unique_ptr<madi::core::HierarchyContext> hierarchy_ctx_;

  madi::renderer::CanvasWidget* canvas_{nullptr};
  QToolBar* toolbar_file_{nullptr};
  QToolBar* toolbar_draw_{nullptr};
  QToolBar* toolbar_operations_{nullptr};
  QToolBar* toolbar_format_{nullptr};
  QComboBox* combo_font_family_{nullptr};
  QComboBox* combo_font_size_{nullptr};
  QToolButton* btn_bold_{nullptr};
  QToolButton* btn_italic_{nullptr};
  QToolButton* btn_underline_{nullptr};
  QToolButton* btn_fill_color_{nullptr};
  QToolButton* btn_stroke_color_{nullptr};
  QToolButton* btn_text_color_{nullptr};
  QComboBox* combo_stroke_width_{nullptr};
  QToolBar* toolbar_view_{nullptr};
  QToolBar* toolbar_aids_{nullptr};
  QToolBar* toolbar_hierarchy_{nullptr};
  QToolBar* main_toolbar_{nullptr};
  QToolButton* btn_dock_merge_{nullptr};
  QToolButton* btn_dock_intersect_{nullptr};
  QToolButton* btn_dock_subtract_{nullptr};
  QToolButton* btn_dock_xor_{nullptr};
  QToolButton* btn_dock_split_{nullptr};
  QToolButton* btn_dock_down_{nullptr};
  QToolButton* btn_dock_up_{nullptr};
  QToolButton* btn_palette_select_{nullptr};
  QToolButton* btn_palette_wire_{nullptr};
  QToolButton* btn_palette_pan_{nullptr};
  QToolButton* btn_palette_del_{nullptr};
  QToolButton* btn_dock_front_{nullptr};
  QToolButton* btn_dock_forward_{nullptr};
  QToolButton* btn_dock_backward_{nullptr};
  QToolButton* btn_dock_back_{nullptr};
  QToolButton* btn_dock_rot_cw_{nullptr};
  QToolButton* btn_dock_rot_ccw_{nullptr};
  QToolButton* btn_dock_angle_snap_{nullptr};
  QToolButton* btn_tb_angle_snap_{nullptr};
  QToolButton* btn_dock_undo_{nullptr};
  QToolButton* btn_dock_redo_{nullptr};
  std::vector<std::pair<QToolButton*, IconFactory::IconType>> palette_tool_buttons_;

  // Inspector Members
  madi::core::NodeId active_inspected_node_{madi::core::InvalidNodeId};
  QLineEdit* inspector_name_{nullptr};
  QLabel* inspector_shape_type_{nullptr};
  QDoubleSpinBox* inspector_x_{nullptr};
  QDoubleSpinBox* inspector_y_{nullptr};
  QDoubleSpinBox* inspector_w_{nullptr};
  QDoubleSpinBox* inspector_h_{nullptr};
  QDoubleSpinBox* inspector_rotation_{nullptr};
  QToolButton* btn_insp_rot_cw_{nullptr};
  QToolButton* btn_insp_rot_ccw_{nullptr};
  QToolButton* btn_insp_angle_snap_{nullptr};

  // Subsystem Inspector Members
  QGroupBox* inspector_subsystem_group_{nullptr};
  QComboBox* inspector_subsystem_type_{nullptr};
  QLineEdit* inspector_subsystem_file_{nullptr};
  QPushButton* btn_browse_subsystem_file_{nullptr};
  QPushButton* btn_subsystem_drill_{nullptr};
  QPushButton* btn_subsystem_save_{nullptr};
  QPushButton* btn_subsystem_reload_{nullptr};
  QPushButton* btn_subsystem_convert_{nullptr};
  QComboBox* inspector_drift_mode_{nullptr};
  QPushButton* btn_edit_symbol_{nullptr};
  QPushButton* btn_sync_primary_{nullptr};
  QAction* action_edit_symbol_{nullptr};
  QAction* action_sync_primary_{nullptr};

  // Inspector Style Controls
  QGroupBox* inspector_style_group_{nullptr};
  QPushButton* btn_pick_fill_{nullptr};
  QPushButton* btn_pick_stroke_{nullptr};
  QPushButton* btn_pick_text_{nullptr};
  QPushButton* btn_reset_style_{nullptr};

  // Inspector Edge / Arrow Controls
  QGroupBox* inspector_edge_group_{nullptr};
  QLineEdit* inspector_edge_name_{nullptr};
  QLabel* inspector_edge_type_{nullptr};
  QCheckBox* inspector_edge_aligned_cb_{nullptr};

  // Theme Menus
  QMenu* menu_ui_themes_{nullptr};
  QMenu* menu_diag_themes_{nullptr};
  QMenu* menu_diag_sel_themes_{nullptr};

  // Command Console Dock
  CommandConsoleDock* console_dock_{nullptr};
  QAction* action_focus_console_{nullptr};

  // System Tray
  QSystemTrayIcon* tray_icon_{nullptr};

  // Docks
  QDockWidget* dock_palette_{nullptr};
  StencilLibraryDock* stencil_dock_{nullptr};
  HierarchyTreeDock* hierarchy_dock_{nullptr};
  QDockWidget* dock_inspector_{nullptr};

  // Status Bar Controls
  QLabel* status_hierarchy_label_{nullptr};
  QPushButton* btn_pop_scope_status_{nullptr};
  QLabel* status_coord_label_{nullptr};
  QLabel* status_sel_label_{nullptr};
  QPushButton* btn_snap_status_{nullptr};
  QPushButton* btn_angle_snap_status_{nullptr};
  QPushButton* btn_zoom_out_status_{nullptr};
  QSlider* zoom_slider_{nullptr};
  QPushButton* btn_zoom_in_status_{nullptr};
  QLabel* zoom_percent_label_{nullptr};

  // Display Toggle Actions
  QAction* action_toggle_rulers_{nullptr};
  QAction* action_toggle_line_jumps_{nullptr};
  QAction* action_toggle_smart_guides_{nullptr};
  QAction* action_remember_settings_{nullptr};
  QAction* action_reset_layout_{nullptr};

  // Actions
  QAction* action_new_{nullptr};
  QAction* action_open_{nullptr};
  QAction* action_save_{nullptr};
  QAction* action_save_as_{nullptr};
  QAction* action_export_drawio_{nullptr};
  QAction* action_export_svg_{nullptr};
  QAction* action_export_png_{nullptr};
  QAction* action_export_jpeg_{nullptr};
  QAction* action_export_yaml_{nullptr};
  QAction* action_exit_{nullptr};

  // Document state
  bool is_modified_{false};
  bool suppress_model_modified_{false};
  bool suppress_inspector_updates_{false};
  QString current_file_path_;
  std::unique_ptr<DiagramModelListener> model_listener_;

  QAction* action_undo_{nullptr};
  QAction* action_redo_{nullptr};
  QAction* action_cut_{nullptr};
  QAction* action_copy_{nullptr};
  QAction* action_paste_{nullptr};
  QAction* action_duplicate_{nullptr};
  QAction* action_delete_{nullptr};
  QAction* action_select_all_{nullptr};
  QAction* action_merge_{nullptr};
  QAction* action_bool_union_{nullptr};
  QAction* action_bool_intersect_{nullptr};
  QAction* action_bool_subtract_{nullptr};
  QAction* action_bool_xor_{nullptr};
  QAction* action_split_{nullptr};
  QAction* action_bring_to_front_{nullptr};
  QAction* action_send_to_back_{nullptr};
  QAction* action_bring_forward_{nullptr};
  QAction* action_send_backward_{nullptr};

  QActionGroup* tool_action_group_{nullptr};
  QAction* action_tool_select_{nullptr};
  QAction* action_tool_rect_{nullptr};
  QAction* action_tool_rounded_{nullptr};
  QAction* action_tool_ellipse_{nullptr};
  QAction* action_tool_polygon_{nullptr};
  QAction* action_tool_block_{nullptr};
  QAction* action_tool_inport_{nullptr};
  QAction* action_tool_outport_{nullptr};
  QAction* action_tool_biport_{nullptr};
  QAction* action_tool_wire_{nullptr};
  QAction* action_tool_multipoint_arrow_{nullptr};

  QAction* action_zoom_in_{nullptr};
  QAction* action_zoom_out_{nullptr};
  QAction* action_fit_view_{nullptr};
  QAction* action_reset_view_{nullptr};
  QAction* action_toggle_grid_{nullptr};
  QAction* action_toggle_snap_{nullptr};
  QAction* action_toggle_angle_snap_{nullptr};
  QAction* action_toggle_segment_moving_{nullptr};
  QAction* action_snap_settings_{nullptr};

  QAction* action_drill_down_{nullptr};
  QAction* action_pop_scope_{nullptr};
  QAction* action_keyboard_shortcuts_{nullptr};
};

} // namespace madi::gui

#endif // MADI_GUI_MAIN_WINDOW_HPP
