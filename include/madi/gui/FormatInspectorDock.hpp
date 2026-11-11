// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_GUI_FORMAT_INSPECTOR_DOCK_HPP
#define MADI_GUI_FORMAT_INSPECTOR_DOCK_HPP

#ifdef MADI_ENABLE_QT

#include <QDockWidget>
#include <QWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QFileDialog>
#include <QColorDialog>

#include "madi/core/GraphModel.hpp"
#include "madi/renderer/CanvasWidget.hpp"

namespace madi::gui {

class FormatInspectorDock : public QDockWidget {
  Q_OBJECT

public:
  explicit FormatInspectorDock(QWidget* parent = nullptr)
      : QDockWidget("Shape Format & Inspector", parent) {
    setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // 1. General & Geometry Group
    auto* grp_geom = new QGroupBox("Shape Geometry", container);
    auto* f_geom = new QFormLayout(grp_geom);
    f_geom->setSpacing(6);

    name_edit_ = new QLineEdit(grp_geom);
    shape_type_label_ = new QLabel("None", grp_geom);
    shape_type_label_->setStyleSheet("font-weight: bold; color: #0078D4;");

    spin_x_ = new QDoubleSpinBox(grp_geom);
    spin_x_->setRange(-100000.0, 100000.0);
    spin_y_ = new QDoubleSpinBox(grp_geom);
    spin_y_->setRange(-100000.0, 100000.0);
    spin_w_ = new QDoubleSpinBox(grp_geom);
    spin_w_->setRange(5.0, 10000.0);
    spin_h_ = new QDoubleSpinBox(grp_geom);
    spin_h_->setRange(5.0, 10000.0);
    spin_rot_ = new QDoubleSpinBox(grp_geom);
    spin_rot_->setRange(0.0, 360.0);
    spin_rot_->setSingleStep(15.0);
    spin_rot_->setSuffix("°");
    spin_rot_->setWrapping(true);

    f_geom->addRow("Label:", name_edit_);
    f_geom->addRow("Type:", shape_type_label_);
    f_geom->addRow("X (px):", spin_x_);
    f_geom->addRow("Y (px):", spin_y_);
    f_geom->addRow("Width:", spin_w_);
    f_geom->addRow("Height:", spin_h_);

    auto* rot_row = new QWidget(grp_geom);
    auto* rot_layout = new QHBoxLayout(rot_row);
    rot_layout->setContentsMargins(0, 0, 0, 0);
    rot_layout->setSpacing(4);
    rot_layout->addWidget(spin_rot_, 1);

    btn_insp_snap_ = new QToolButton(rot_row);
    btn_insp_snap_->setText("∠");
    btn_insp_snap_->setToolTip("Toggle Angle Snapping (15° increments)");
    btn_insp_snap_->setCheckable(true);
    btn_insp_snap_->setFixedSize(24, 24);
    if (canvas_) btn_insp_snap_->setChecked(canvas_->is_angle_snapping_enabled());
    connect(btn_insp_snap_, &QToolButton::toggled, this, [this](bool checked) {
      if (canvas_) canvas_->set_angle_snapping_enabled(checked);
    });
    rot_layout->addWidget(btn_insp_snap_);

    f_geom->addRow("Rotation:", rot_row);
    layout->addWidget(grp_geom);

    // 2. Subsystem Configuration Group
    subsystem_group_ = new QGroupBox("Subsystem Properties", container);
    auto* f_sub = new QFormLayout(subsystem_group_);
    f_sub->setSpacing(6);

    subsystem_type_combo_ = new QComboBox(subsystem_group_);
    subsystem_type_combo_->addItem("⚙ Built-in Subsystem (Embedded)");
    subsystem_type_combo_->addItem("📁 External File Subsystem");
    f_sub->addRow("Mode:", subsystem_type_combo_);

    auto* file_row = new QWidget(subsystem_group_);
    auto* file_row_layout = new QHBoxLayout(file_row);
    file_row_layout->setContentsMargins(0, 0, 0, 0);
    file_row_layout->setSpacing(4);
    file_path_edit_ = new QLineEdit(file_row);
    file_path_edit_->setPlaceholderText("subsystem.madi");
    btn_browse_ = new QPushButton("Browse...", file_row);
    file_row_layout->addWidget(file_path_edit_);
    file_row_layout->addWidget(btn_browse_);
    f_sub->addRow("File:", file_row);

    auto* action_row1 = new QWidget(subsystem_group_);
    auto* a1_layout = new QHBoxLayout(action_row1);
    a1_layout->setContentsMargins(0, 0, 0, 0);
    a1_layout->setSpacing(4);
    btn_drill_ = new QPushButton("⬇ Drill Down", action_row1);
    btn_drill_->setStyleSheet("font-weight: bold; background: #EBF8FF; padding: 4px;");
    btn_convert_ = new QPushButton("📤 Export / Embed", action_row1);
    a1_layout->addWidget(btn_drill_);
    a1_layout->addWidget(btn_convert_);
    f_sub->addRow(action_row1);

    auto* action_row2 = new QWidget(subsystem_group_);
    auto* a2_layout = new QHBoxLayout(action_row2);
    a2_layout->setContentsMargins(0, 0, 0, 0);
    a2_layout->setSpacing(4);
    btn_save_file_ = new QPushButton("💾 Save File", action_row2);
    btn_reload_file_ = new QPushButton("🔄 Reload File", action_row2);
    a2_layout->addWidget(btn_save_file_);
    a2_layout->addWidget(btn_reload_file_);
    f_sub->addRow(action_row2);

    layout->addWidget(subsystem_group_);
    subsystem_group_->setVisible(false);

    layout->addStretch();
    setWidget(container);

    setup_connections();
  }

  void set_canvas(renderer::CanvasWidget* canvas) noexcept {
    canvas_ = canvas;
    if (btn_insp_snap_ && canvas_) {
      btn_insp_snap_->setChecked(canvas_->is_angle_snapping_enabled());
    }
  }

  [[nodiscard]] QToolButton* btn_insp_snap() const noexcept {
    return btn_insp_snap_;
  }

  void set_graph_model(core::GraphModel* graph) noexcept {
    graph_model_ = graph;
  }

  void update_selection(core::NodeId selected_node) {
    active_node_id_ = selected_node;
    if (!graph_model_ || selected_node == core::InvalidNodeId) {
      name_edit_->clear();
      shape_type_label_->setText("None");
      spin_x_->setValue(0.0);
      spin_y_->setValue(0.0);
      spin_w_->setValue(0.0);
      spin_h_->setValue(0.0);
      spin_rot_->setValue(0.0);
      subsystem_group_->setVisible(false);
      return;
    }

    const auto* node = graph_model_->find_node(selected_node);
    if (!node) return;

    name_edit_->blockSignals(true);
    name_edit_->setText(QString::fromStdString(node->name));
    name_edit_->blockSignals(false);

    spin_x_->blockSignals(true);
    spin_y_->blockSignals(true);
    spin_w_->blockSignals(true);
    spin_h_->blockSignals(true);
    spin_rot_->blockSignals(true);
    spin_x_->setValue(node->bounds.x);
    spin_y_->setValue(node->bounds.y);
    spin_w_->setValue(node->bounds.width);
    spin_h_->setValue(node->bounds.height);
    spin_rot_->setValue(node->rotation_deg);
    spin_x_->blockSignals(false);
    spin_y_->blockSignals(false);
    spin_w_->blockSignals(false);
    spin_h_->blockSignals(false);
    spin_rot_->blockSignals(false);

    QString stype = "Rectangle";
    if (node->shape == core::ShapeType::RoundedRect) stype = "Rounded Rectangle";
    else if (node->shape == core::ShapeType::Ellipse) stype = "Ellipse";
    else if (node->shape == core::ShapeType::HierarchicalBlock) stype = "Subsystem Block";
    else if (node->shape == core::ShapeType::CustomPolygon) {
      stype = QString("Polygon (%1 vertices)").arg(node->get_world_polygon_vertices().size());
    }
    shape_type_label_->setText(stype);

    if (node->is_subsystem()) {
      subsystem_group_->setVisible(true);
      const bool is_file = node->is_file_subsystem();
      subsystem_type_combo_->blockSignals(true);
      subsystem_type_combo_->setCurrentIndex(is_file ? 1 : 0);
      subsystem_type_combo_->blockSignals(false);

      file_path_edit_->setEnabled(is_file);
      btn_browse_->setEnabled(is_file);
      btn_save_file_->setEnabled(is_file);
      btn_reload_file_->setEnabled(is_file);

      if (is_file) {
        file_path_edit_->setText(QString::fromStdString(node->subsystem_file()));
        btn_convert_->setText("📥 Embed in Diagram");
      } else {
        file_path_edit_->clear();
        btn_convert_->setText("📤 Export to File...");
      }
    } else {
      subsystem_group_->setVisible(false);
    }
  }

private:
  void setup_connections() {
    connect(name_edit_, &QLineEdit::editingFinished, this, [this]() {
      if (!graph_model_ || active_node_id_ == core::InvalidNodeId) return;
      if (auto* n = graph_model_->find_node(active_node_id_)) {
        n->name = name_edit_->text().toStdString();
        if (canvas_) canvas_->update();
      }
    });

    auto on_geom_changed = [this]() {
      if (!graph_model_ || active_node_id_ == core::InvalidNodeId) return;
      if (auto* n = graph_model_->find_node(active_node_id_)) {
        n->bounds.x = spin_x_->value();
        n->bounds.y = spin_y_->value();
        n->bounds.width = spin_w_->value();
        n->bounds.height = spin_h_->value();
        n->rotation_deg = spin_rot_->value();
        if (canvas_) {
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
      }
    };

    connect(spin_x_, &QDoubleSpinBox::editingFinished, this, on_geom_changed);
    connect(spin_y_, &QDoubleSpinBox::editingFinished, this, on_geom_changed);
    connect(spin_w_, &QDoubleSpinBox::editingFinished, this, on_geom_changed);
    connect(spin_h_, &QDoubleSpinBox::editingFinished, this, on_geom_changed);
    connect(spin_rot_, &QDoubleSpinBox::editingFinished, this, on_geom_changed);

    connect(btn_drill_, &QPushButton::clicked, this, [this]() {
      if (canvas_ && active_node_id_ != core::InvalidNodeId) {
        canvas_->drill_down_node(active_node_id_);
      }
    });

    connect(btn_save_file_, &QPushButton::clicked, this, [this]() {
      if (canvas_ && active_node_id_ != core::InvalidNodeId) {
        canvas_->save_subsystem_file(active_node_id_);
      }
    });

    connect(btn_reload_file_, &QPushButton::clicked, this, [this]() {
      if (canvas_ && active_node_id_ != core::InvalidNodeId) {
        canvas_->reload_subsystem_file(active_node_id_);
      }
    });

    connect(btn_browse_, &QPushButton::clicked, this, [this]() {
      if (!canvas_ || active_node_id_ == core::InvalidNodeId) return;
      const QString fpath = QFileDialog::getOpenFileName(
          this, "Select Subsystem File", "",
          "Supported Formats (*.madi *.json *.jsonc *.yaml *.drawio *.xml);;All Files (*)");
      if (!fpath.isEmpty()) {
        file_path_edit_->setText(fpath);
        canvas_->convert_subsystem_to_file(active_node_id_, fpath.toStdString());
        update_selection(active_node_id_);
      }
    });

    connect(btn_convert_, &QPushButton::clicked, this, [this]() {
      if (!canvas_ || !graph_model_ || active_node_id_ == core::InvalidNodeId) return;
      auto* n = graph_model_->find_node(active_node_id_);
      if (!n) return;
      if (n->is_file_subsystem()) {
        canvas_->convert_subsystem_to_builtin(active_node_id_);
      } else {
        const QString fpath = QFileDialog::getSaveFileName(
            this, "Export Subsystem to File",
            QString::fromStdString(n->name + ".madi"),
            "Madí Diagram (*.madi);;JSON Diagram (*.json *.jsonc);;Madí YAML (*.yaml);;All Files (*)");
        if (!fpath.isEmpty()) {
          canvas_->convert_subsystem_to_file(active_node_id_, fpath.toStdString());
        }
      }
      update_selection(active_node_id_);
    });

    connect(subsystem_type_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
      if (!canvas_ || !graph_model_ || active_node_id_ == core::InvalidNodeId) return;
      if (idx == 1) {
        QString fpath = file_path_edit_->text().trimmed();
        if (fpath.isEmpty()) {
          if (const auto* n = graph_model_->find_node(active_node_id_)) {
            fpath = QString::fromStdString(n->name + ".madi");
            file_path_edit_->setText(fpath);
          }
        }
        canvas_->convert_subsystem_to_file(active_node_id_, fpath.toStdString());
      } else {
        canvas_->convert_subsystem_to_builtin(active_node_id_);
      }
      update_selection(active_node_id_);
    });
  }

  core::NodeId active_node_id_{core::InvalidNodeId};
  renderer::CanvasWidget* canvas_{nullptr};
  core::GraphModel* graph_model_{nullptr};

  QLineEdit* name_edit_{nullptr};
  QLabel* shape_type_label_{nullptr};
  QDoubleSpinBox* spin_x_{nullptr};
  QDoubleSpinBox* spin_y_{nullptr};
  QDoubleSpinBox* spin_w_{nullptr};
  QDoubleSpinBox* spin_h_{nullptr};
  QDoubleSpinBox* spin_rot_{nullptr};
  QToolButton* btn_insp_snap_{nullptr};

  QGroupBox* subsystem_group_{nullptr};
  QComboBox* subsystem_type_combo_{nullptr};
  QLineEdit* file_path_edit_{nullptr};
  QPushButton* btn_browse_{nullptr};
  QPushButton* btn_drill_{nullptr};
  QPushButton* btn_convert_{nullptr};
  QPushButton* btn_save_file_{nullptr};
  QPushButton* btn_reload_file_{nullptr};
};

} // namespace madi::gui

#endif // MADI_ENABLE_QT
#endif // MADI_GUI_FORMAT_INSPECTOR_DOCK_HPP
