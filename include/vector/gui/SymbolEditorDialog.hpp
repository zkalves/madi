// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_GUI_SYMBOL_EDITOR_DIALOG_HPP
#define VECTOR_GUI_SYMBOL_EDITOR_DIALOG_HPP

#include "vector/core/Geometry.hpp"
#include "vector/core/GraphModel.hpp"
#include "vector/core/ParametricShape.hpp"
#include "vector/core/SymbolGenerator.hpp"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace vector::gui {

class SymbolPreviewWidget : public QWidget {
  Q_OBJECT

public:
  explicit SymbolPreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumSize(260, 220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
  }

  void set_symbol_info(const QString& name, double w, double h,
                       const QColor& fill, const QColor& stroke, const QColor& banner,
                       const std::vector<std::tuple<QString, QString, QString, double>>& pins,
                       core::Vec2d anchor_pt = {0.0, 0.0},
                       core::Vec2d resize_pt = {1.0, 1.0},
                       const std::vector<core::Vec2d>& control_points = {}) {
    name_ = name;
    symbol_w_ = w;
    symbol_h_ = h;
    fill_ = fill;
    stroke_ = stroke;
    banner_ = banner;
    pins_ = pins;
    anchor_pt_ = anchor_pt;
    resize_pt_ = resize_pt;
    control_points_ = control_points;
    update();
  }

  void set_control_points(const std::vector<core::Vec2d>& points) {
    control_points_ = points;
    update();
  }

  [[nodiscard]] const std::vector<core::Vec2d>& control_points() const noexcept {
    return control_points_;
  }

signals:
  void control_point_moved(int index, double nx, double ny);
  void control_point_double_clicked(double nx, double ny);

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0x1E, 0x1E, 0x2E));

    if (symbol_w_ <= 0.0 || symbol_h_ <= 0.0) return;

    // Compute preview scale to fit with margin
    const double margin = 40.0;
    const double avail_w = width() - margin * 2.0;
    const double avail_h = height() - margin * 2.0;
    if (avail_w <= 0.0 || avail_h <= 0.0) return;

    const double scale = std::min(avail_w / symbol_w_, avail_h / symbol_h_);
    const double sw = symbol_w_ * scale;
    const double sh = symbol_h_ * scale;
    const double sx = (width() - sw) * 0.5;
    const double sy = (height() - sh) * 0.5;

    // Save bounds for hit-testing
    cached_sx_ = sx;
    cached_sy_ = sy;
    cached_sw_ = sw;
    cached_sh_ = sh;

    const QRectF box(sx, sy, sw, sh);

    if (control_points_.size() >= 3) {
      // Draw custom polygon geometry defined by control points
      QPolygonF poly;
      for (const auto& cp : control_points_) {
        poly << QPointF(sx + cp.x * sw, sy + cp.y * sh);
      }
      p.setBrush(fill_);
      QPen sp(stroke_, 2.0);
      p.setPen(sp);
      p.drawPolygon(poly);

      // Connect lines with subtle stroke
      p.setPen(QPen(stroke_.lighter(120), 1.5, Qt::DashLine));
      p.drawPolyline(poly);
      if (!poly.isEmpty()) {
        p.drawLine(poly.last(), poly.first());
      }
    } else {
      // Standard Box fill & stroke
      p.setBrush(fill_);
      QPen sp(stroke_, 2.0);
      p.setPen(sp);
      p.drawRoundedRect(box, 4.0, 4.0);

      // Header banner
      const double banner_h = std::min(24.0, sh * 0.25);
      const QRectF banner_rect(sx, sy, sw, banner_h);
      p.setBrush(banner_);
      p.setPen(Qt::NoPen);
      p.drawRoundedRect(banner_rect, 4.0, 4.0);
      p.drawRect(QRectF(sx, sy + banner_h - 4.0, sw, 4.0));

      // Label
      p.setPen(Qt::white);
      QFont f = p.font();
      f.setBold(true);
      f.setPointSize(std::max(7, static_cast<int>(banner_h * 0.5)));
      p.setFont(f);
      p.drawText(banner_rect, Qt::AlignCenter, name_);
    }

    // Pins
    QFont f_pins = p.font();
    f_pins.setBold(false);
    f_pins.setPointSize(7);
    p.setFont(f_pins);

    for (const auto& [pname, pdir, pside, ppos] : pins_) {
      double px = sx;
      double py = sy;
      double lx = 0.0;
      double ly = 0.0;
      Qt::Alignment align = Qt::AlignVCenter;

      if (pside.startsWith("West", Qt::CaseInsensitive)) {
        px = sx;
        py = sy + ppos * sh;
        lx = px + 6.0;
        ly = py - 8.0;
        align = Qt::AlignLeft | Qt::AlignVCenter;
      } else if (pside.startsWith("East", Qt::CaseInsensitive)) {
        px = sx + sw;
        py = sy + ppos * sh;
        lx = px - 60.0;
        ly = py - 8.0;
        align = Qt::AlignRight | Qt::AlignVCenter;
      } else if (pside.startsWith("North", Qt::CaseInsensitive)) {
        px = sx + ppos * sw;
        py = sy;
        lx = px - 25.0;
        ly = py + 6.0;
        align = Qt::AlignHCenter | Qt::AlignTop;
      } else { // South
        px = sx + ppos * sw;
        py = sy + sh;
        lx = px - 25.0;
        ly = py - 18.0;
        align = Qt::AlignHCenter | Qt::AlignBottom;
      }

      p.setBrush(stroke_);
      p.setPen(Qt::NoPen);
      p.drawRect(QRectF(px - 3.0, py - 3.0, 6.0, 6.0));

      p.setPen(QColor(0xD1, 0xD5, 0xDB));
      p.drawText(QRectF(lx, ly, 54.0, 16.0), align, pname);
    }

    // Draw Custom Control Points (Amber Diamonds)
    for (size_t i = 0; i < control_points_.size(); ++i) {
      const double px = sx + control_points_[i].x * sw;
      const double py = sy + control_points_[i].y * sh;
      const bool is_drag = (dragged_point_idx_ == static_cast<int>(i));
      const double sz = is_drag ? 7.0 : 5.5;

      QPolygonF diamond;
      diamond << QPointF(px, py - sz) << QPointF(px + sz, py)
              << QPointF(px, py + sz) << QPointF(px - sz, py);

      p.setPen(QPen(QColor(0x78, 0x35, 0x0F), is_drag ? 2.0 : 1.5));
      p.setBrush(is_drag ? QColor(0xFB, 0xBF, 0x24) : QColor(0xF5, 0x9E, 0x0B));
      p.drawPolygon(diamond);

      p.setPen(Qt::white);
      p.drawPoint(QPointF(px, py));

      p.setFont(QFont("sans-serif", 7, QFont::Bold));
      p.setPen(QColor(0xF5, 0x9E, 0x0B));
      p.drawText(QRectF(px + 6.0, py - 8.0, 40.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter,
                 QString("P%1").arg(i + 1));
    }

    // Draw Anchor Point marker (green target crosshair)
    const double ax = sx + anchor_pt_.x * sw;
    const double ay = sy + anchor_pt_.y * sh;
    p.setPen(QPen(QColor(0x10, 0xB9, 0x81), 2.0));
    p.setBrush(QColor(0x10, 0xB9, 0x81, 120));
    p.drawEllipse(QPointF(ax, ay), 5.0, 5.0);
    p.drawLine(QPointF(ax - 8.0, ay), QPointF(ax + 8.0, ay));
    p.drawLine(QPointF(ax, ay - 8.0), QPointF(ax, ay + 8.0));
    p.setPen(QColor(0x10, 0xB9, 0x81));
    p.setFont(QFont("sans-serif", 7, QFont::Bold));
    p.drawText(QRectF(ax + 7.0, ay - 8.0, 100.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter, "Anchor");

    // Draw Resize Control Point marker (cyan diamond handle)
    const double rx = sx + resize_pt_.x * sw;
    const double ry = sy + resize_pt_.y * sh;
    QPolygonF res_diamond;
    res_diamond << QPointF(rx, ry - 6.0) << QPointF(rx + 6.0, ry)
                << QPointF(rx, ry + 6.0) << QPointF(rx - 6.0, ry);
    p.setPen(QPen(QColor(0x00, 0xBC, 0xD4), 2.0));
    p.setBrush(QColor(0x00, 0xBC, 0xD4, 160));
    p.drawPolygon(res_diamond);
    p.setPen(QColor(0x00, 0xBC, 0xD4));
    p.drawText(QRectF(rx + 7.0, ry - 8.0, 100.0, 16.0), Qt::AlignLeft | Qt::AlignVCenter, "Resize Point");
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && cached_sw_ > 1.0 && cached_sh_ > 1.0) {
      for (size_t i = 0; i < control_points_.size(); ++i) {
        const double px = cached_sx_ + control_points_[i].x * cached_sw_;
        const double py = cached_sy_ + control_points_[i].y * cached_sh_;
        const double dx = event->position().x() - px;
        const double dy = event->position().y() - py;
        if (dx * dx + dy * dy <= 64.0) {
          dragged_point_idx_ = static_cast<int>(i);
          update();
          event->accept();
          return;
        }
      }
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragged_point_idx_ >= 0 && dragged_point_idx_ < static_cast<int>(control_points_.size()) &&
        cached_sw_ > 1.0 && cached_sh_ > 1.0) {
      const double nx = std::clamp((event->position().x() - cached_sx_) / cached_sw_, 0.0, 1.0);
      const double ny = std::clamp((event->position().y() - cached_sy_) / cached_sh_, 0.0, 1.0);
      control_points_[dragged_point_idx_] = {nx, ny};
      emit control_point_moved(dragged_point_idx_, nx, ny);
      update();
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (dragged_point_idx_ >= 0) {
      dragged_point_idx_ = -1;
      update();
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && cached_sw_ > 1.0 && cached_sh_ > 1.0) {
      const double nx = std::clamp((event->position().x() - cached_sx_) / cached_sw_, 0.0, 1.0);
      const double ny = std::clamp((event->position().y() - cached_sy_) / cached_sh_, 0.0, 1.0);
      emit control_point_double_clicked(nx, ny);
      event->accept();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }

private:
  QString name_;
  double symbol_w_{160.0};
  double symbol_h_{100.0};
  QColor fill_{0x2A, 0x2A, 0x3C};
  QColor stroke_{0x7A, 0x88, 0xCF};
  QColor banner_{0x3B, 0x82, 0xF6};
  std::vector<std::tuple<QString, QString, QString, double>> pins_;
  core::Vec2d anchor_pt_{0.0, 0.0};
  core::Vec2d resize_pt_{1.0, 1.0};
  std::vector<core::Vec2d> control_points_;

  double cached_sx_{0.0};
  double cached_sy_{0.0};
  double cached_sw_{0.0};
  double cached_sh_{0.0};
  int dragged_point_idx_{-1};
};

class SymbolEditorDialog : public QDialog {
  Q_OBJECT

public:
  struct PinConfig {
    core::PortId id{core::InvalidPortId};
    QString name;
    QString direction; // "input", "output", "inout"
    QString side;      // "West", "East", "North", "South"
    double normalized_pos{0.5};
  };

  SymbolEditorDialog(core::Node& node, core::GraphModel& parent_graph,
                     core::GraphModel* child_graph = nullptr,
                     QWidget* parent = nullptr)
      : QDialog(parent), node_(node), parent_graph_(parent_graph), child_graph_(child_graph) {
    setWindowTitle(QString("Symbol Editor - %1").arg(QString::fromStdString(node.name)));
    resize(820, 600);

    auto* main_layout = new QHBoxLayout(this);

    // Left column: Tabbed Controls
    auto* left_vbox = new QVBoxLayout();
    tab_widget_ = new QTabWidget(this);

    // 1. Symbol Dimensions & Colors Group
    auto* grp_props = new QGroupBox("Symbol Dimensions & Visual Style", this);
    auto* f_props = new QFormLayout(grp_props);

    txt_name_ = new QLineEdit(QString::fromStdString(node.name), this);
    spin_w_ = new QDoubleSpinBox(this);
    spin_w_->setRange(60.0, 5000.0);
    spin_w_->setValue(node.bounds.width);
    spin_w_->setSuffix(" px");

    spin_h_ = new QDoubleSpinBox(this);
    spin_h_->setRange(40.0, 5000.0);
    spin_h_->setValue(node.bounds.height);
    spin_h_->setSuffix(" px");

    btn_auto_fit_ = new QPushButton("Auto-Fit Dimensions to Pins", this);

    btn_color_banner_ = new QPushButton("Header Banner Color...", this);
    btn_color_fill_ = new QPushButton("Fill Color...", this);
    btn_color_stroke_ = new QPushButton("Border Stroke Color...", this);

    banner_color_ = QColor(0x3B, 0x82, 0xF6);
    fill_color_ = QColor(0x2A, 0x2A, 0x3C);
    stroke_color_ = QColor(0x7A, 0x88, 0xCF);

    if (auto it = node.properties.find("banner_color"); it != node.properties.end()) {
      QColor c(QString::fromStdString(it->second));
      if (c.isValid()) banner_color_ = c;
    }
    if (auto it = node.properties.find("fill_color"); it != node.properties.end()) {
      QColor c(QString::fromStdString(it->second));
      if (c.isValid()) fill_color_ = c;
    }
    if (auto it = node.properties.find("stroke_color"); it != node.properties.end()) {
      QColor c(QString::fromStdString(it->second));
      if (c.isValid()) stroke_color_ = c;
    }

    f_props->addRow("Symbol Label:", txt_name_);
    auto* dims_row = new QHBoxLayout();
    dims_row->addWidget(new QLabel("W:"));
    dims_row->addWidget(spin_w_);
    dims_row->addWidget(new QLabel("H:"));
    dims_row->addWidget(spin_h_);
    dims_row->addWidget(btn_auto_fit_);
    f_props->addRow("Bounding Size:", dims_row);

    auto* colors_row = new QHBoxLayout();
    colors_row->addWidget(btn_color_banner_);
    colors_row->addWidget(btn_color_fill_);
    colors_row->addWidget(btn_color_stroke_);
    f_props->addRow("Colors:", colors_row);

    // 2. Instantiation Anchor & Resize Control Point Group
    auto* grp_inst = new QGroupBox("Instantiation Anchor & Resize Control Point", this);
    auto* f_inst = new QFormLayout(grp_inst);

    combo_anchor_preset_ = new QComboBox(this);
    combo_anchor_preset_->addItem("Top-Left (0.0, 0.0)", QPointF(0.0, 0.0));
    combo_anchor_preset_->addItem("Center (0.5, 0.5)", QPointF(0.5, 0.5));
    combo_anchor_preset_->addItem("Bottom-Left (0.0, 1.0)", QPointF(0.0, 1.0));
    combo_anchor_preset_->addItem("Top-Right (1.0, 0.0)", QPointF(1.0, 0.0));
    combo_anchor_preset_->addItem("Middle-Left (0.0, 0.5)", QPointF(0.0, 0.5));
    combo_anchor_preset_->addItem("Middle-Right (1.0, 0.5)", QPointF(1.0, 0.5));
    combo_anchor_preset_->addItem("Custom Normalized...", QPointF(-1.0, -1.0));

    spin_anchor_x_ = new QDoubleSpinBox(this);
    spin_anchor_x_->setRange(0.0, 1.0);
    spin_anchor_x_->setSingleStep(0.05);
    spin_anchor_y_ = new QDoubleSpinBox(this);
    spin_anchor_y_->setRange(0.0, 1.0);
    spin_anchor_y_->setSingleStep(0.05);

    const auto cur_anc = node.symbol_anchor();
    spin_anchor_x_->setValue(cur_anc.x);
    spin_anchor_y_->setValue(cur_anc.y);

    auto* anchor_row = new QHBoxLayout();
    anchor_row->addWidget(combo_anchor_preset_);
    anchor_row->addWidget(new QLabel("X:"));
    anchor_row->addWidget(spin_anchor_x_);
    anchor_row->addWidget(new QLabel("Y:"));
    anchor_row->addWidget(spin_anchor_y_);
    f_inst->addRow("Anchor (Insertion Origin):", anchor_row);

    combo_resize_preset_ = new QComboBox(this);
    combo_resize_preset_->addItem("Bottom-Right (1.0, 1.0)", QPointF(1.0, 1.0));
    combo_resize_preset_->addItem("Bottom-Center (0.5, 1.0)", QPointF(0.5, 1.0));
    combo_resize_preset_->addItem("Middle-Right (1.0, 0.5)", QPointF(1.0, 0.5));
    combo_resize_preset_->addItem("Top-Right (1.0, 0.0)", QPointF(1.0, 0.0));
    combo_resize_preset_->addItem("Bottom-Left (0.0, 1.0)", QPointF(0.0, 1.0));
    combo_resize_preset_->addItem("Custom Normalized...", QPointF(-1.0, -1.0));

    spin_resize_x_ = new QDoubleSpinBox(this);
    spin_resize_x_->setRange(0.0, 1.0);
    spin_resize_x_->setSingleStep(0.05);
    spin_resize_y_ = new QDoubleSpinBox(this);
    spin_resize_y_->setRange(0.0, 1.0);
    spin_resize_y_->setSingleStep(0.05);

    const auto cur_res = node.symbol_resize_point();
    spin_resize_x_->setValue(cur_res.x);
    spin_resize_y_->setValue(cur_res.y);

    auto* resize_row = new QHBoxLayout();
    resize_row->addWidget(combo_resize_preset_);
    resize_row->addWidget(new QLabel("X:"));
    resize_row->addWidget(spin_resize_x_);
    resize_row->addWidget(new QLabel("Y:"));
    resize_row->addWidget(spin_resize_y_);
    f_inst->addRow("Resize Control Point:", resize_row);

    // 3. Definition Synchronization & Drift Group
    auto* grp_sync = new QGroupBox("Definition Synchronization & Drift Mode", this);
    auto* v_sync = new QVBoxLayout(grp_sync);

    rad_follow_primary_ = new QRadioButton("Follow Primary Definition (Synchronized with primary template)", this);
    rad_allow_drift_ = new QRadioButton("Allow Drift (Customized / Independent instance override)", this);

    const std::string sync_mode = node.properties.contains("definition_sync_mode")
        ? node.properties.at("definition_sync_mode") : "follow_primary";

    if (sync_mode == "allow_drift") {
      rad_allow_drift_->setChecked(true);
    } else {
      rad_follow_primary_->setChecked(true);
    }

    auto* sync_btns_row = new QHBoxLayout();
    btn_resync_primary_ = new QPushButton("↺ Sync Pins with Primary Definition", this);
    sync_btns_row->addWidget(rad_follow_primary_);
    sync_btns_row->addWidget(rad_allow_drift_);
    sync_btns_row->addWidget(btn_resync_primary_);
    v_sync->addLayout(sync_btns_row);

    // Assemble Tab 1: Dimensions & Style
    auto* tab_dims_widget = new QWidget(this);
    auto* v_tab_dims = new QVBoxLayout(tab_dims_widget);
    v_tab_dims->addWidget(grp_props);
    v_tab_dims->addWidget(grp_inst);
    v_tab_dims->addWidget(grp_sync);
    v_tab_dims->addStretch();
    tab_widget_->addTab(tab_dims_widget, "Dimensions & Style");

    // Assemble Tab 2: Control Points & Geometry
    auto* tab_points_widget = new QWidget(this);
    auto* v_tab_points = new QVBoxLayout(tab_points_widget);

    auto* lbl_pt_info = new QLabel(
        "<b>Parametric Control Points & Polygon Vertices:</b><br>"
        "Points define the shape perimeter. You can add, edit, or remove points below, "
        "or double-click and drag handles directly on the live preview.", this);
    lbl_pt_info->setWordWrap(true);
    v_tab_points->addWidget(lbl_pt_info);

    point_table_ = new QTableWidget(this);
    point_table_->setColumnCount(3);
    point_table_->setHorizontalHeaderLabels({"Point", "Normalized X [0..1]", "Normalized Y [0..1]"});
    point_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    point_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    point_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    point_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    point_table_->setSelectionMode(QAbstractItemView::SingleSelection);

    load_control_points();

    auto* pt_btn_row = new QHBoxLayout();
    btn_add_point_ = new QPushButton("+ Add Control Point", this);
    btn_remove_point_ = new QPushButton("- Remove Point", this);
    btn_reset_points_ = new QPushButton("Reset to Default", this);

    pt_btn_row->addWidget(btn_add_point_);
    pt_btn_row->addWidget(btn_remove_point_);
    pt_btn_row->addWidget(btn_reset_points_);

    v_tab_points->addWidget(point_table_);
    v_tab_points->addLayout(pt_btn_row);
    tab_widget_->addTab(tab_points_widget, "Control Points & Geometry");

    // Assemble Tab 3: Pins & Interface Ports Table
    auto* tab_pins_widget = new QWidget(this);
    auto* v_pins = new QVBoxLayout(tab_pins_widget);

    pin_table_ = new QTableWidget(this);
    pin_table_->setColumnCount(4);
    pin_table_->setHorizontalHeaderLabels({"Pin Name", "Direction", "Perimeter Side", "Position Offset"});
    pin_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    pin_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    pin_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    pin_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    pin_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pin_table_->setSelectionMode(QAbstractItemView::SingleSelection);

    load_pins();

    auto* pin_btn_row = new QHBoxLayout();
    btn_add_pin_ = new QPushButton("+ Add Pin", this);
    btn_remove_pin_ = new QPushButton("- Remove Pin", this);
    btn_move_up_ = new QPushButton("▲ Move Up", this);
    btn_move_down_ = new QPushButton("▼ Move Down", this);
    btn_flip_side_ = new QPushButton("⇄ Flip Side", this);

    pin_btn_row->addWidget(btn_add_pin_);
    pin_btn_row->addWidget(btn_remove_pin_);
    pin_btn_row->addWidget(btn_move_up_);
    pin_btn_row->addWidget(btn_move_down_);
    pin_btn_row->addWidget(btn_flip_side_);

    v_pins->addWidget(pin_table_);
    v_pins->addLayout(pin_btn_row);
    tab_widget_->addTab(tab_pins_widget, "Pins & Ports");

    left_vbox->addWidget(tab_widget_);

    // Dialog action buttons
    auto* dlg_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(dlg_buttons, &QDialogButtonBox::accepted, this, &SymbolEditorDialog::apply_and_accept);
    connect(dlg_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    left_vbox->addWidget(dlg_buttons);

    main_layout->addLayout(left_vbox, 3);

    // Right column: Real-Time Symbol Preview
    auto* right_vbox = new QVBoxLayout();
    right_vbox->addWidget(new QLabel("<b>Live Symbol Preview:</b>", this));
    preview_widget_ = new SymbolPreviewWidget(this);
    right_vbox->addWidget(preview_widget_);
    main_layout->addLayout(right_vbox, 2);

    setup_connections();
    refresh_preview();
  }

  [[nodiscard]] bool is_drift_allowed() const noexcept {
    return rad_allow_drift_->isChecked();
  }

  void add_control_point(core::Vec2d pt) {
    core::insert_polygon_vertex_at_normalized(node_, pt);
    control_points_ = node_.polygon_vertices;
    populate_point_table();
    refresh_preview();
  }

  void remove_control_point(int index) {
    if (core::remove_polygon_vertex(node_, index)) {
      control_points_ = node_.polygon_vertices;
      populate_point_table();
      refresh_preview();
    }
  }

  [[nodiscard]] const std::vector<core::Vec2d>& control_points() const noexcept {
    return control_points_;
  }

  void set_control_points(const std::vector<core::Vec2d>& pts) {
    control_points_ = pts;
    populate_point_table();
    refresh_preview();
  }

private:
  void load_control_points() {
    control_points_ = node_.polygon_vertices;
    if (control_points_.empty()) {
      if (node_.shape == core::ShapeType::Rectangle || node_.shape == core::ShapeType::RoundedRect ||
          node_.shape == core::ShapeType::HierarchicalBlock || node_.shape == core::ShapeType::CustomPolygon) {
        control_points_ = {
          {0.0, 0.0},
          {1.0, 0.0},
          {1.0, 1.0},
          {0.0, 1.0}
        };
      }
    }
    populate_point_table();
  }

  void populate_point_table() {
    point_table_->blockSignals(true);
    point_table_->setRowCount(static_cast<int>(control_points_.size()));
    for (int r = 0; r < static_cast<int>(control_points_.size()); ++r) {
      const auto& pt = control_points_[static_cast<size_t>(r)];

      auto* item_idx = new QTableWidgetItem(QString("Point %1").arg(r + 1));
      item_idx->setFlags(item_idx->flags() & ~Qt::ItemIsEditable);
      point_table_->setItem(r, 0, item_idx);

      auto* spin_x = new QDoubleSpinBox(point_table_);
      spin_x->setRange(0.0, 1.0);
      spin_x->setSingleStep(0.05);
      spin_x->setValue(pt.x);
      point_table_->setCellWidget(r, 1, spin_x);

      auto* spin_y = new QDoubleSpinBox(point_table_);
      spin_y->setRange(0.0, 1.0);
      spin_y->setSingleStep(0.05);
      spin_y->setValue(pt.y);
      point_table_->setCellWidget(r, 2, spin_y);

      connect(spin_x, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, r](double vx) {
        if (r < static_cast<int>(control_points_.size())) {
          control_points_[static_cast<size_t>(r)].x = vx;
          refresh_preview();
        }
      });
      connect(spin_y, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, r](double vy) {
        if (r < static_cast<int>(control_points_.size())) {
          control_points_[static_cast<size_t>(r)].y = vy;
          refresh_preview();
        }
      });
    }
    point_table_->blockSignals(false);
  }

  void sync_control_points_from_table() {
    for (int r = 0; r < point_table_->rowCount() && r < static_cast<int>(control_points_.size()); ++r) {
      if (auto* sx = qobject_cast<QDoubleSpinBox*>(point_table_->cellWidget(r, 1))) {
        control_points_[static_cast<size_t>(r)].x = sx->value();
      }
      if (auto* sy = qobject_cast<QDoubleSpinBox*>(point_table_->cellWidget(r, 2))) {
        control_points_[static_cast<size_t>(r)].y = sy->value();
      }
    }
  }

  void load_pins() {
    pins_.clear();
    for (core::PortId pid : node_.port_ids) {
      if (const auto* p = parent_graph_.find_port(pid)) {
        PinConfig pc;
        pc.id = pid;
        pc.name = QString::fromStdString(p->name);
        pc.direction = QString::fromStdString(p->pin_type.empty() ? "generic" : p->pin_type);
        switch (p->direction) {
          case core::PortDirection::West: pc.side = "West"; break;
          case core::PortDirection::East: pc.side = "East"; break;
          case core::PortDirection::North: pc.side = "North"; break;
          case core::PortDirection::South: pc.side = "South"; break;
          case core::PortDirection::Omnidirectional: pc.side = "West"; break;
        }
        pc.normalized_pos = (pc.side == "West" || pc.side == "East") ? p->local_anchor.y : p->local_anchor.x;
        pins_.push_back(pc);
      }
    }
    populate_pin_table();
  }

  void populate_pin_table() {
    pin_table_->setRowCount(static_cast<int>(pins_.size()));
    for (int r = 0; r < static_cast<int>(pins_.size()); ++r) {
      const auto& pc = pins_[static_cast<size_t>(r)];

      auto* item_name = new QTableWidgetItem(pc.name);
      pin_table_->setItem(r, 0, item_name);

      auto* combo_dir = new QComboBox(pin_table_);
      combo_dir->addItems({"input", "output", "inout", "generic"});
      combo_dir->setCurrentText(pc.direction);
      pin_table_->setCellWidget(r, 1, combo_dir);

      auto* combo_side = new QComboBox(pin_table_);
      combo_side->addItems({"West", "East", "North", "South"});
      combo_side->setCurrentText(pc.side);
      pin_table_->setCellWidget(r, 2, combo_side);

      auto* spin_pos = new QDoubleSpinBox(pin_table_);
      spin_pos->setRange(0.05, 0.95);
      spin_pos->setSingleStep(0.05);
      spin_pos->setValue(pc.normalized_pos);
      pin_table_->setCellWidget(r, 3, spin_pos);

      connect(combo_dir, &QComboBox::currentTextChanged, this, [this, r](const QString& d) {
        if (r < static_cast<int>(pins_.size())) pins_[static_cast<size_t>(r)].direction = d;
        refresh_preview();
      });
      connect(combo_side, &QComboBox::currentTextChanged, this, [this, r](const QString& s) {
        if (r < static_cast<int>(pins_.size())) pins_[static_cast<size_t>(r)].side = s;
        refresh_preview();
      });
      connect(spin_pos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, r](double v) {
        if (r < static_cast<int>(pins_.size())) pins_[static_cast<size_t>(r)].normalized_pos = v;
        refresh_preview();
      });
    }
  }

  void setup_connections() {
    connect(txt_name_, &QLineEdit::textChanged, this, &SymbolEditorDialog::refresh_preview);
    connect(spin_w_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SymbolEditorDialog::refresh_preview);
    connect(spin_h_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SymbolEditorDialog::refresh_preview);

    connect(spin_anchor_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SymbolEditorDialog::refresh_preview);
    connect(spin_anchor_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SymbolEditorDialog::refresh_preview);
    connect(spin_resize_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SymbolEditorDialog::refresh_preview);
    connect(spin_resize_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SymbolEditorDialog::refresh_preview);

    connect(combo_anchor_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
      const QPointF pt = combo_anchor_preset_->itemData(idx).toPointF();
      if (pt.x() >= 0.0 && pt.y() >= 0.0) {
        spin_anchor_x_->setValue(pt.x());
        spin_anchor_y_->setValue(pt.y());
      }
    });

    connect(combo_resize_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
      const QPointF pt = combo_resize_preset_->itemData(idx).toPointF();
      if (pt.x() >= 0.0 && pt.y() >= 0.0) {
        spin_resize_x_->setValue(pt.x());
        spin_resize_y_->setValue(pt.y());
      }
    });

    connect(btn_color_banner_, &QPushButton::clicked, this, [this]() {
      QColor c = QColorDialog::getColor(banner_color_, this, "Select Header Banner Color");
      if (c.isValid()) { banner_color_ = c; refresh_preview(); }
    });
    connect(btn_color_fill_, &QPushButton::clicked, this, [this]() {
      QColor c = QColorDialog::getColor(fill_color_, this, "Select Symbol Fill Color");
      if (c.isValid()) { fill_color_ = c; refresh_preview(); }
    });
    connect(btn_color_stroke_, &QPushButton::clicked, this, [this]() {
      QColor c = QColorDialog::getColor(stroke_color_, this, "Select Symbol Border Color");
      if (c.isValid()) { stroke_color_ = c; refresh_preview(); }
    });

    connect(btn_auto_fit_, &QPushButton::clicked, this, &SymbolEditorDialog::auto_fit_dimensions);

    connect(btn_add_pin_, &QPushButton::clicked, this, [this]() {
      sync_pins_from_table();
      PinConfig pc;
      pc.id = core::InvalidPortId;
      pc.name = QString("pin_%1").arg(pins_.size() + 1);
      pc.direction = "input";
      pc.side = "West";
      pc.normalized_pos = 0.5;
      pins_.push_back(pc);
      populate_pin_table();
      refresh_preview();
    });

    connect(btn_remove_pin_, &QPushButton::clicked, this, [this]() {
      const int row = pin_table_->currentRow();
      if (row >= 0 && row < static_cast<int>(pins_.size())) {
        sync_pins_from_table();
        pins_.erase(pins_.begin() + row);
        populate_pin_table();
        refresh_preview();
      }
    });

    connect(btn_move_up_, &QPushButton::clicked, this, [this]() {
      const int row = pin_table_->currentRow();
      if (row > 0 && row < static_cast<int>(pins_.size())) {
        sync_pins_from_table();
        std::swap(pins_[static_cast<size_t>(row)], pins_[static_cast<size_t>(row - 1)]);
        populate_pin_table();
        pin_table_->selectRow(row - 1);
        refresh_preview();
      }
    });

    connect(btn_move_down_, &QPushButton::clicked, this, [this]() {
      const int row = pin_table_->currentRow();
      if (row >= 0 && row + 1 < static_cast<int>(pins_.size())) {
        sync_pins_from_table();
        std::swap(pins_[static_cast<size_t>(row)], pins_[static_cast<size_t>(row + 1)]);
        populate_pin_table();
        pin_table_->selectRow(row + 1);
        refresh_preview();
      }
    });

    connect(btn_flip_side_, &QPushButton::clicked, this, [this]() {
      const int row = pin_table_->currentRow();
      if (row >= 0 && row < static_cast<int>(pins_.size())) {
        sync_pins_from_table();
        auto& pc = pins_[static_cast<size_t>(row)];
        if (pc.side == "West") { pc.side = "East"; pc.direction = "output"; }
        else if (pc.side == "East") { pc.side = "West"; pc.direction = "input"; }
        else if (pc.side == "North") { pc.side = "South"; }
        else { pc.side = "North"; }
        populate_pin_table();
        pin_table_->selectRow(row);
        refresh_preview();
      }
    });

    connect(btn_resync_primary_, &QPushButton::clicked, this, [this]() {
      if (child_graph_) {
        core::SymbolGenerator::sync_symbol_from_diagram(node_, parent_graph_, *child_graph_);
        load_pins();
        refresh_preview();
      }
    });

    // Control Point Button Connections
    connect(btn_add_point_, &QPushButton::clicked, this, [this]() {
      sync_control_points_from_table();
      if (control_points_.empty()) {
        control_points_ = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
      }
      const int sel_row = point_table_->currentRow();
      const size_t insert_idx = (sel_row >= 0 && sel_row < static_cast<int>(control_points_.size()))
          ? static_cast<size_t>(sel_row) : control_points_.size() - 1;
      const size_t next_idx = (insert_idx + 1) % control_points_.size();
      const auto pA = control_points_[insert_idx];
      const auto pB = control_points_[next_idx];
      const core::Vec2d mid = {(pA.x + pB.x) * 0.5, (pA.y + pB.y) * 0.5};
      control_points_.insert(control_points_.begin() + insert_idx + 1, mid);
      populate_point_table();
      point_table_->selectRow(static_cast<int>(insert_idx + 1));
      refresh_preview();
    });

    connect(btn_remove_point_, &QPushButton::clicked, this, [this]() {
      const int row = point_table_->currentRow();
      if (row >= 0 && row < static_cast<int>(control_points_.size()) && control_points_.size() > 3) {
        sync_control_points_from_table();
        control_points_.erase(control_points_.begin() + row);
        populate_point_table();
        refresh_preview();
      }
    });

    connect(btn_reset_points_, &QPushButton::clicked, this, [this]() {
      control_points_ = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
      populate_point_table();
      refresh_preview();
    });

    // Preview interactive signals
    connect(preview_widget_, &SymbolPreviewWidget::control_point_moved, this, [this](int idx, double nx, double ny) {
      if (idx >= 0 && idx < static_cast<int>(control_points_.size())) {
        control_points_[static_cast<size_t>(idx)] = {nx, ny};
        if (auto* sx = qobject_cast<QDoubleSpinBox*>(point_table_->cellWidget(idx, 1))) {
          sx->blockSignals(true);
          sx->setValue(nx);
          sx->blockSignals(false);
        }
        if (auto* sy = qobject_cast<QDoubleSpinBox*>(point_table_->cellWidget(idx, 2))) {
          sy->blockSignals(true);
          sy->setValue(ny);
          sy->blockSignals(false);
        }
      }
    });

    connect(preview_widget_, &SymbolPreviewWidget::control_point_double_clicked, this, [this](double nx, double ny) {
      sync_control_points_from_table();
      if (control_points_.empty()) {
        control_points_ = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
      }
      core::Node dummy;
      dummy.polygon_vertices = control_points_;
      const int ins = core::insert_polygon_vertex_at_normalized(dummy, {nx, ny});
      control_points_ = dummy.polygon_vertices;
      populate_point_table();
      point_table_->selectRow(ins);
      refresh_preview();
    });
  }

  void sync_pins_from_table() {
    for (int r = 0; r < pin_table_->rowCount() && r < static_cast<int>(pins_.size()); ++r) {
      if (auto* item = pin_table_->item(r, 0)) {
        pins_[static_cast<size_t>(r)].name = item->text().trimmed();
      }
      if (auto* cd = qobject_cast<QComboBox*>(pin_table_->cellWidget(r, 1))) {
        pins_[static_cast<size_t>(r)].direction = cd->currentText();
      }
      if (auto* cs = qobject_cast<QComboBox*>(pin_table_->cellWidget(r, 2))) {
        pins_[static_cast<size_t>(r)].side = cs->currentText();
      }
      if (auto* sp = qobject_cast<QDoubleSpinBox*>(pin_table_->cellWidget(r, 3))) {
        pins_[static_cast<size_t>(r)].normalized_pos = sp->value();
      }
    }
  }

  void auto_fit_dimensions() {
    sync_pins_from_table();
    size_t count_west = 0, count_east = 0, count_north = 0, count_south = 0;
    for (const auto& p : pins_) {
      if (p.side == "West") count_west++;
      else if (p.side == "East") count_east++;
      else if (p.side == "North") count_north++;
      else if (p.side == "South") count_south++;
    }
    const double needed_h = std::max(80.0, std::max(count_west, count_east) * 30.0 + 44.0);
    const double needed_w = std::max(140.0, std::max(count_north, count_south) * 40.0 + 80.0);
    spin_w_->setValue(needed_w);
    spin_h_->setValue(needed_h);
    refresh_preview();
  }

public:
  void refresh_preview() {
    sync_pins_from_table();
    sync_control_points_from_table();

    std::vector<std::tuple<QString, QString, QString, double>> preview_pins;
    for (const auto& p : pins_) {
      preview_pins.emplace_back(p.name, p.direction, p.side, p.normalized_pos);
    }
    const core::Vec2d anc_pt = (spin_anchor_x_ && spin_anchor_y_)
        ? core::Vec2d{spin_anchor_x_->value(), spin_anchor_y_->value()} : core::Vec2d{0.0, 0.0};
    const core::Vec2d res_pt = (spin_resize_x_ && spin_resize_y_)
        ? core::Vec2d{spin_resize_x_->value(), spin_resize_y_->value()} : core::Vec2d{1.0, 1.0};

    preview_widget_->set_symbol_info(
        txt_name_->text(), spin_w_->value(), spin_h_->value(),
        fill_color_, stroke_color_, banner_color_, preview_pins,
        anc_pt, res_pt, control_points_);
  }

  void apply_and_accept() {
    sync_pins_from_table();
    sync_control_points_from_table();

    // 1. Update node basic parameters
    node_.name = txt_name_->text().toStdString();
    node_.bounds.width = spin_w_->value();
    node_.bounds.height = spin_h_->value();
    node_.properties["fill_color"] = fill_color_.name().toStdString();
    node_.properties["stroke_color"] = stroke_color_.name().toStdString();
    node_.properties["banner_color"] = banner_color_.name().toStdString();

    if (spin_anchor_x_ && spin_anchor_y_) {
      node_.set_symbol_anchor({spin_anchor_x_->value(), spin_anchor_y_->value()});
    }
    if (spin_resize_x_ && spin_resize_y_) {
      node_.set_symbol_resize_point({spin_resize_x_->value(), spin_resize_y_->value()});
    }

    // Save custom control points
    if (!control_points_.empty()) {
      node_.polygon_vertices = control_points_;
      node_.shape = core::ShapeType::CustomPolygon;
      node_.properties["parametric_type"] = "custom_polygon";
    }

    const bool drift = rad_allow_drift_->isChecked();
    node_.properties["definition_sync_mode"] = drift ? "allow_drift" : "follow_primary";

    // 2. Synchronize pins into parent_graph_ ports
    std::unordered_set<core::PortId> kept_ports;
    for (const auto& pc : pins_) {
      core::PortDirection pdir = core::PortDirection::West;
      core::Vec2d anchor{0.0, pc.normalized_pos};

      if (pc.side == "East") {
        pdir = core::PortDirection::East;
        anchor = {1.0, pc.normalized_pos};
      } else if (pc.side == "North") {
        pdir = core::PortDirection::North;
        anchor = {pc.normalized_pos, 0.0};
      } else if (pc.side == "South") {
        pdir = core::PortDirection::South;
        anchor = {pc.normalized_pos, 1.0};
      }

      if (pc.id != core::InvalidPortId && parent_graph_.find_port(pc.id)) {
        if (auto* p = parent_graph_.find_port(pc.id)) {
          p->name = pc.name.toStdString();
          p->pin_type = pc.direction.toStdString();
          p->direction = pdir;
          p->local_anchor = anchor;
          p->is_boundary_interface = true;
          kept_ports.insert(pc.id);
        }
      } else {
        core::Port np;
        np.parent_node = node_.id;
        np.name = pc.name.toStdString();
        np.pin_type = pc.direction.toStdString();
        np.direction = pdir;
        np.local_anchor = anchor;
        np.is_boundary_interface = true;
        const core::PortId new_id = parent_graph_.add_port(std::move(np));
        kept_ports.insert(new_id);
      }
    }

    // Remove deleted boundary ports
    for (auto it = node_.port_ids.begin(); it != node_.port_ids.end();) {
      const core::PortId pid = *it;
      if (const auto* p = parent_graph_.find_port(pid)) {
        if (p->is_boundary_interface && !kept_ports.contains(pid)) {
          parent_graph_.remove_port(pid);
          it = node_.port_ids.erase(it);
          continue;
        }
      }
      ++it;
    }

    // Ensure all kept ports are in node_.port_ids
    for (core::PortId pid : kept_ports) {
      if (std::find(node_.port_ids.begin(), node_.port_ids.end(), pid) == node_.port_ids.end()) {
        node_.port_ids.push_back(pid);
      }
    }

    parent_graph_.update_all_edge_routes();
    accept();
  }

  core::Node& node_;
  core::GraphModel& parent_graph_;
  core::GraphModel* child_graph_{nullptr};

  QTabWidget* tab_widget_{nullptr};

  QLineEdit* txt_name_{nullptr};
  QDoubleSpinBox* spin_w_{nullptr};
  QDoubleSpinBox* spin_h_{nullptr};
  QPushButton* btn_auto_fit_{nullptr};
  QPushButton* btn_color_banner_{nullptr};
  QPushButton* btn_color_fill_{nullptr};
  QPushButton* btn_color_stroke_{nullptr};

  QComboBox* combo_anchor_preset_{nullptr};
  QDoubleSpinBox* spin_anchor_x_{nullptr};
  QDoubleSpinBox* spin_anchor_y_{nullptr};
  QComboBox* combo_resize_preset_{nullptr};
  QDoubleSpinBox* spin_resize_x_{nullptr};
  QDoubleSpinBox* spin_resize_y_{nullptr};

  QRadioButton* rad_follow_primary_{nullptr};
  QRadioButton* rad_allow_drift_{nullptr};
  QPushButton* btn_resync_primary_{nullptr};

  QTableWidget* point_table_{nullptr};
  QPushButton* btn_add_point_{nullptr};
  QPushButton* btn_remove_point_{nullptr};
  QPushButton* btn_reset_points_{nullptr};

  QTableWidget* pin_table_{nullptr};
  QPushButton* btn_add_pin_{nullptr};
  QPushButton* btn_remove_pin_{nullptr};
  QPushButton* btn_move_up_{nullptr};
  QPushButton* btn_move_down_{nullptr};
  QPushButton* btn_flip_side_{nullptr};

  SymbolPreviewWidget* preview_widget_{nullptr};

  QColor banner_color_;
  QColor fill_color_;
  QColor stroke_color_;
  std::vector<PinConfig> pins_;
  std::vector<core::Vec2d> control_points_;
};

} // namespace vector::gui

#endif // VECTOR_GUI_SYMBOL_EDITOR_DIALOG_HPP
