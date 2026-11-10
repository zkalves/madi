// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_GUI_SYMBOL_EDITOR_DIALOG_HPP
#define VECTOR_GUI_SYMBOL_EDITOR_DIALOG_HPP

#include "vector/core/Geometry.hpp"
#include "vector/core/GraphModel.hpp"
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
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vector::gui {

class SymbolPreviewWidget : public QWidget {
  Q_OBJECT

public:
  explicit SymbolPreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumSize(240, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  void set_symbol_info(const QString& name, double w, double h,
                       const QColor& fill, const QColor& stroke, const QColor& banner,
                       const std::vector<std::tuple<QString, QString, QString, double>>& pins) {
    name_ = name;
    symbol_w_ = w;
    symbol_h_ = h;
    fill_ = fill;
    stroke_ = stroke;
    banner_ = banner;
    pins_ = pins;
    update();
  }

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

    const QRectF box(sx, sy, sw, sh);

    // Box fill & stroke
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
    p.drawRect(QRectF(sx, sy + banner_h - 4.0, sw, 4.0)); // Square bottom corners of banner

    // Label
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(std::max(7, static_cast<int>(banner_h * 0.5)));
    p.setFont(f);
    p.drawText(banner_rect, Qt::AlignCenter, name_);

    // Pins
    f.setBold(false);
    f.setPointSize(std::max(6, f.pointSize() - 2));
    p.setFont(f);

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

      // Draw pin terminal square/dot
      p.setBrush(stroke_);
      p.setPen(Qt::NoPen);
      p.drawRect(QRectF(px - 3.0, py - 3.0, 6.0, 6.0));

      // Draw pin name
      p.setPen(QColor(0xD1, 0xD5, 0xDB));
      p.drawText(QRectF(lx, ly, 54.0, 16.0), align, pname);
    }
  }

private:
  QString name_;
  double symbol_w_{160.0};
  double symbol_h_{100.0};
  QColor fill_{0x2A, 0x2A, 0x3C};
  QColor stroke_{0x7A, 0x88, 0xCF};
  QColor banner_{0x3B, 0x82, 0xF6};
  std::vector<std::tuple<QString, QString, QString, double>> pins_;
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
    resize(780, 580);

    auto* main_layout = new QHBoxLayout(this);

    // Left column: Controls and Pin Table
    auto* left_vbox = new QVBoxLayout();

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

    left_vbox->addWidget(grp_props);

    // 2. Definition Synchronization & Drift Group (Inclusive Language)
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

    left_vbox->addWidget(grp_sync);

    // 3. Pins & Ports Table
    auto* grp_pins = new QGroupBox("Symbol Pins & Boundary Interface Ports", this);
    auto* v_pins = new QVBoxLayout(grp_pins);

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
    left_vbox->addWidget(grp_pins);

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

private:
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

  void refresh_preview() {
    sync_pins_from_table();
    std::vector<std::tuple<QString, QString, QString, double>> preview_pins;
    for (const auto& p : pins_) {
      preview_pins.emplace_back(p.name, p.direction, p.side, p.normalized_pos);
    }
    preview_widget_->set_symbol_info(
        txt_name_->text(), spin_w_->value(), spin_h_->value(),
        fill_color_, stroke_color_, banner_color_, preview_pins);
  }

  void apply_and_accept() {
    sync_pins_from_table();

    // 1. Update node basic parameters
    node_.name = txt_name_->text().toStdString();
    node_.bounds.width = spin_w_->value();
    node_.bounds.height = spin_h_->value();
    node_.properties["fill_color"] = fill_color_.name().toStdString();
    node_.properties["stroke_color"] = stroke_color_.name().toStdString();
    node_.properties["banner_color"] = banner_color_.name().toStdString();

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

  QLineEdit* txt_name_{nullptr};
  QDoubleSpinBox* spin_w_{nullptr};
  QDoubleSpinBox* spin_h_{nullptr};
  QPushButton* btn_auto_fit_{nullptr};
  QPushButton* btn_color_banner_{nullptr};
  QPushButton* btn_color_fill_{nullptr};
  QPushButton* btn_color_stroke_{nullptr};

  QRadioButton* rad_follow_primary_{nullptr};
  QRadioButton* rad_allow_drift_{nullptr};
  QPushButton* btn_resync_primary_{nullptr};

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
};

} // namespace vector::gui

#endif // VECTOR_GUI_SYMBOL_EDITOR_DIALOG_HPP
