// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_GUI_SNAP_GRID_SETTINGS_DIALOG_HPP
#define MADI_GUI_SNAP_GRID_SETTINGS_DIALOG_HPP

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "madi/grid/GridManager.hpp"
#include "madi/renderer/CanvasWidget.hpp"

namespace madi::gui {

class SnapGridSettingsDialog : public QDialog {
  Q_OBJECT

public:
  explicit SnapGridSettingsDialog(QWidget* parent = nullptr)
      : QDialog(parent) {
    setWindowTitle("Snap & Grid Settings");
    resize(500, 560);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(12, 12, 12, 12);
    main_layout->setSpacing(10);

    tab_widget_ = new QTabWidget(this);

    // Tab 1: General (Snap & Glue)
    setup_general_tab();

    // Tab 2: Advanced (Snap Pull Strength & Type)
    setup_advanced_tab();

    // Tab 3: Snap and Grid (Ruler & Grid Spacing)
    setup_snap_grid_tab();

    // Tab 4: Page Setup (Presets, Dimensions, Margins)
    setup_page_tab();

    main_layout->addWidget(tab_widget_);

    // Button Box
    auto* btn_box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(btn_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(btn_box->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
      emit apply_requested();
    });
    main_layout->addWidget(btn_box);
  }

  void set_active_tab(int index) {
    if (tab_widget_ && index >= 0 && index < tab_widget_->count()) {
      tab_widget_->setCurrentIndex(index);
    }
  }

  void load_from(const grid::GridManager& mgr, bool show_smart_guides,
                 const renderer::CanvasWidget* canvas = nullptr) {
    // 1. General Tab
    chk_enable_snap_->setChecked(mgr.is_snap_enabled());
    const auto flags = mgr.snap_flags();
    chk_target_grid_->setChecked(grid::has_flag(flags, grid::SnapFlags::Grid));
    chk_target_guides_->setChecked(show_smart_guides);
    chk_target_geometry_->setChecked(
        grid::has_flag(flags, grid::SnapFlags::ShapeBounds) ||
        grid::has_flag(flags, grid::SnapFlags::ShapeCenter));
    chk_target_ports_->setChecked(grid::has_flag(flags, grid::SnapFlags::InterfacePort));
    chk_target_ortho_->setChecked(grid::has_flag(flags, grid::SnapFlags::ConnectorOrthogonal));
    if (chk_remember_settings_) {
      QSettings settings("VECTOR", "VectorDiagrammer");
      chk_remember_settings_->setChecked(settings.value("General/remember_settings_accepted", false).toBool());
    }
    if (canvas && chk_angle_snap_) {
      chk_angle_snap_->setChecked(canvas->is_angle_snapping_enabled());
      spin_angle_snap_step_->setValue(canvas->angle_snap_step());
      chk_cardinal_priority_->setChecked(canvas->cardinal_snap_priority());
    }

    // 2. Advanced Tab
    const double tol = mgr.snap_tolerance();
    spin_strength_->setValue(tol);
    slider_strength_->setValue(static_cast<int>(std::round(tol)));

    switch (mgr.snap_strength()) {
      case grid::SnapStrength::Off: rb_strength_off_->setChecked(true); break;
      case grid::SnapStrength::Weak: rb_strength_weak_->setChecked(true); break;
      case grid::SnapStrength::Normal: rb_strength_normal_->setChecked(true); break;
      case grid::SnapStrength::Strong: rb_strength_strong_->setChecked(true); break;
      case grid::SnapStrength::Custom: rb_strength_custom_->setChecked(true); break;
    }

    switch (mgr.snap_type()) {
      case grid::SnapType::Rectangular: rb_type_rect_->setChecked(true); break;
      case grid::SnapType::Isometric: rb_type_iso_->setChecked(true); break;
      case grid::SnapType::Polar: rb_type_polar_->setChecked(true); break;
    }
    spin_polar_angle_->setValue(static_cast<int>(mgr.polar_angle_deg()));

    // 3. Snap and Grid Tab
    const auto& cfg = mgr.config();
    if (cfg.spacing_mode == grid::GridSpacingMode::Fixed) {
      rb_spacing_fixed_->setChecked(true);
    } else {
      rb_spacing_var_->setChecked(true);
    }

    spin_snap_x_->setValue(cfg.snap_spacing_x);
    spin_snap_y_->setValue(cfg.snap_spacing_y);
    chk_equal_snap_->setChecked(std::abs(cfg.snap_spacing_x - cfg.snap_spacing_y) < 1e-4);

    chk_match_grid_to_snap_->setChecked(cfg.match_grid_to_snap);
    spin_grid_x_->setValue(cfg.major_spacing_x);
    spin_grid_y_->setValue(cfg.major_spacing_y);
    spin_grid_subdiv_->setValue(static_cast<int>(cfg.subdivisions_x));
    chk_grid_visible_->setChecked(cfg.visible);
    chk_grid_subdiv_->setChecked(cfg.show_subdivisions);

    // 4. Page Setup Tab
    if (canvas) {
      const auto preset = canvas->page_preset();
      int idx = combo_page_preset_->findData(static_cast<int>(preset));
      if (idx >= 0) {
        combo_page_preset_->blockSignals(true);
        combo_page_preset_->setCurrentIndex(idx);
        combo_page_preset_->blockSignals(false);
      }

      if (canvas->page_orientation() == renderer::PageOrientation::Portrait) {
        rb_orient_portrait_->setChecked(true);
      } else {
        rb_orient_landscape_->setChecked(true);
      }

      spin_page_width_->blockSignals(true);
      spin_page_height_->blockSignals(true);
      spin_page_width_->setValue(canvas->page_bounds().width);
      spin_page_height_->setValue(canvas->page_bounds().height);
      spin_page_width_->blockSignals(false);
      spin_page_height_->blockSignals(false);

      spin_page_margin_->setValue(canvas->page_margin());
      chk_show_page_sheet_->setChecked(canvas->show_page_sheet());
    }
  }

  void apply_to(grid::GridManager& mgr, bool& out_show_smart_guides,
                renderer::CanvasWidget* canvas = nullptr) const {
    // 1. General Tab
    grid::SnapFlags flags = grid::SnapFlags::None;
    if (chk_target_grid_->isChecked()) flags = flags | grid::SnapFlags::Grid;
    if (chk_target_geometry_->isChecked()) {
      flags = flags | grid::SnapFlags::ShapeBounds | grid::SnapFlags::ShapeCenter;
    }
    if (chk_target_ports_->isChecked()) flags = flags | grid::SnapFlags::InterfacePort;
    if (chk_target_ortho_->isChecked()) flags = flags | grid::SnapFlags::ConnectorOrthogonal;
    mgr.set_snap_flags(flags);

    out_show_smart_guides = chk_target_guides_->isChecked();
    if (chk_remember_settings_) {
      QSettings settings("VECTOR", "VectorDiagrammer");
      settings.setValue("General/remember_settings_accepted", chk_remember_settings_->isChecked());
    }

    // 2. Advanced Tab
    double tol = spin_strength_->value();
    if (rb_strength_off_->isChecked()) {
      mgr.set_snap_strength(grid::SnapStrength::Off);
    } else if (rb_strength_weak_->isChecked()) {
      mgr.set_snap_strength(grid::SnapStrength::Weak);
    } else if (rb_strength_normal_->isChecked()) {
      mgr.set_snap_strength(grid::SnapStrength::Normal);
    } else if (rb_strength_strong_->isChecked()) {
      mgr.set_snap_strength(grid::SnapStrength::Strong);
    } else {
      mgr.set_snap_tolerance(tol);
    }

    if (rb_type_iso_->isChecked()) {
      mgr.set_snap_type(grid::SnapType::Isometric);
    } else if (rb_type_polar_->isChecked()) {
      mgr.set_snap_type(grid::SnapType::Polar);
    } else {
      mgr.set_snap_type(grid::SnapType::Rectangular);
    }
    mgr.set_polar_angle_deg(spin_polar_angle_->value());

    // 3. Snap and Grid Tab
    auto cfg = mgr.config();
    cfg.spacing_mode = rb_spacing_fixed_->isChecked() ?
        grid::GridSpacingMode::Fixed : grid::GridSpacingMode::Variable;

    cfg.snap_spacing_x = spin_snap_x_->value();
    cfg.snap_spacing_y = spin_snap_y_->value();
    if (chk_equal_snap_->isChecked()) {
      cfg.snap_spacing_y = cfg.snap_spacing_x;
    }

    cfg.match_grid_to_snap = chk_match_grid_to_snap_->isChecked();
    cfg.major_spacing_x = spin_grid_x_->value();
    cfg.major_spacing_y = spin_grid_y_->value();
    cfg.subdivisions_x = static_cast<uint32_t>(spin_grid_subdiv_->value());
    cfg.subdivisions_y = cfg.subdivisions_x;
    cfg.visible = chk_grid_visible_->isChecked();
    cfg.show_subdivisions = chk_grid_subdiv_->isChecked();

    mgr.set_config(cfg);
    mgr.set_snap_enabled(chk_enable_snap_->isChecked());

    // 4. Page Setup Tab
    if (canvas) {
      const auto preset = static_cast<renderer::PagePreset>(combo_page_preset_->currentData().toInt());
      const auto orient = rb_orient_landscape_->isChecked() ?
          renderer::PageOrientation::Landscape : renderer::PageOrientation::Portrait;
      if (preset != renderer::PagePreset::Custom) {
        canvas->set_page_preset(preset, orient);
      } else {
        canvas->set_page_size(spin_page_width_->value(), spin_page_height_->value());
      }
      canvas->set_page_margin(spin_page_margin_->value());
      canvas->set_show_page_sheet(chk_show_page_sheet_->isChecked());
      if (chk_angle_snap_) {
        canvas->set_angle_snapping_enabled(chk_angle_snap_->isChecked());
        canvas->set_angle_snap_step(spin_angle_snap_step_->value());
        canvas->set_cardinal_snap_priority(chk_cardinal_priority_->isChecked());
      }
    }
  }

signals:
  void apply_requested();

private:
  void setup_general_tab() {
    auto* page = new QWidget(tab_widget_);
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);

    chk_enable_snap_ = new QCheckBox("Enable Snapping (F9 / Ctrl+B)", page);
    chk_enable_snap_->setStyleSheet("font-weight: bold; font-size: 13px;");
    layout->addWidget(chk_enable_snap_);

    auto* grp_targets = new QGroupBox("Snap Targets", page);
    auto* v_targets = new QVBoxLayout(grp_targets);
    v_targets->setSpacing(8);

    chk_target_grid_ = new QCheckBox("Grid lines and intersections", grp_targets);
    chk_target_guides_ = new QCheckBox("Alignment indicators (Smart Guides)", grp_targets);
    chk_target_geometry_ = new QCheckBox("Shape geometry (Edges, bounds, and centers)", grp_targets);
    chk_target_ports_ = new QCheckBox("Connection points (Ports / Quick-Connect)", grp_targets);
    chk_target_ortho_ = new QCheckBox("Connector orthogonal alignment", grp_targets);

    v_targets->addWidget(chk_target_grid_);
    v_targets->addWidget(chk_target_guides_);
    v_targets->addWidget(chk_target_geometry_);
    v_targets->addWidget(chk_target_ports_);
    v_targets->addWidget(chk_target_ortho_);
    layout->addWidget(grp_targets);

    auto* grp_persist = new QGroupBox("Workspace Persistence", page);
    auto* v_persist = new QVBoxLayout(grp_persist);
    v_persist->setSpacing(6);
    chk_remember_settings_ = new QCheckBox("Remember window positions, layout, colors, and themes across sessions", grp_persist);
    chk_remember_settings_->setToolTip("Save window geometry, dock layouts, active themes, and settings for future sessions");
    v_persist->addWidget(chk_remember_settings_);
    layout->addWidget(grp_persist);

    auto* grp_rot = new QGroupBox("Figure Rotation & Angle Snapping", page);
    auto* f_rot = new QFormLayout(grp_rot);
    f_rot->setSpacing(8);

    chk_angle_snap_ = new QCheckBox("Enable angle snapping during figure rotation", grp_rot);
    chk_angle_snap_->setToolTip("Snap rotation angle to increments (Hold Shift to snap, Alt for free rotation)");
    chk_angle_snap_->setChecked(true);

    spin_angle_snap_step_ = new QDoubleSpinBox(grp_rot);
    spin_angle_snap_step_->setRange(1.0, 90.0);
    spin_angle_snap_step_->setValue(15.0);
    spin_angle_snap_step_->setSingleStep(5.0);
    spin_angle_snap_step_->setSuffix("°");

    chk_cardinal_priority_ = new QCheckBox("Priority magnetic lock on cardinal angles (0°, 90°, 180°, 270°)", grp_rot);
    chk_cardinal_priority_->setChecked(true);

    f_rot->addRow(chk_angle_snap_);
    f_rot->addRow("Angle Increment:", spin_angle_snap_step_);
    f_rot->addRow(chk_cardinal_priority_);
    layout->addWidget(grp_rot);

    layout->addStretch();
    tab_widget_->addTab(page, "General");
  }

  void setup_advanced_tab() {
    auto* page = new QWidget(tab_widget_);
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);

    // Pull strength group
    auto* grp_strength = new QGroupBox("Snap Pull Strength", page);
    auto* v_strength = new QVBoxLayout(grp_strength);
    v_strength->setSpacing(8);

    auto* strength_row = new QHBoxLayout();
    slider_strength_ = new QSlider(Qt::Horizontal, grp_strength);
    slider_strength_->setRange(0, 50);
    spin_strength_ = new QDoubleSpinBox(grp_strength);
    spin_strength_->setRange(0.0, 50.0);
    spin_strength_->setSingleStep(1.0);
    spin_strength_->setSuffix(" px");

    connect(slider_strength_, &QSlider::valueChanged, this, [this](int val) {
      if (std::abs(spin_strength_->value() - val) > 1e-4) {
        spin_strength_->setValue(val);
      }
    });
    connect(spin_strength_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
      slider_strength_->setValue(static_cast<int>(std::round(val)));
      rb_strength_custom_->setChecked(true);
    });

    strength_row->addWidget(new QLabel("Tolerance:"));
    strength_row->addWidget(slider_strength_, 1);
    strength_row->addWidget(spin_strength_);
    v_strength->addLayout(strength_row);

    auto* grp_preset_btn = new QButtonGroup(this);
    rb_strength_off_ = new QRadioButton("Off (0 px)", grp_strength);
    rb_strength_weak_ = new QRadioButton("Weak (3 px)", grp_strength);
    rb_strength_normal_ = new QRadioButton("Normal (6 px)", grp_strength);
    rb_strength_strong_ = new QRadioButton("Strong (12 px)", grp_strength);
    rb_strength_custom_ = new QRadioButton("Custom", grp_strength);

    grp_preset_btn->addButton(rb_strength_off_);
    grp_preset_btn->addButton(rb_strength_weak_);
    grp_preset_btn->addButton(rb_strength_normal_);
    grp_preset_btn->addButton(rb_strength_strong_);
    grp_preset_btn->addButton(rb_strength_custom_);

    auto* radio_grid = new QHBoxLayout();
    radio_grid->addWidget(rb_strength_off_);
    radio_grid->addWidget(rb_strength_weak_);
    radio_grid->addWidget(rb_strength_normal_);
    radio_grid->addWidget(rb_strength_strong_);
    radio_grid->addWidget(rb_strength_custom_);
    v_strength->addLayout(radio_grid);

    connect(rb_strength_off_, &QRadioButton::toggled, this, [this](bool c) { if (c) spin_strength_->setValue(0.0); });
    connect(rb_strength_weak_, &QRadioButton::toggled, this, [this](bool c) { if (c) spin_strength_->setValue(3.0); });
    connect(rb_strength_normal_, &QRadioButton::toggled, this, [this](bool c) { if (c) spin_strength_->setValue(6.0); });
    connect(rb_strength_strong_, &QRadioButton::toggled, this, [this](bool c) { if (c) spin_strength_->setValue(12.0); });

    layout->addWidget(grp_strength);

    // Snap Type group
    auto* grp_type = new QGroupBox("Snap Type", page);
    auto* v_type = new QVBoxLayout(grp_type);
    v_type->setSpacing(8);

    auto* grp_type_btn = new QButtonGroup(this);
    rb_type_rect_ = new QRadioButton("Grid snap (Rectangular)", grp_type);
    rb_type_iso_ = new QRadioButton("Grid snap (Isometric - 30°)", grp_type);
    rb_type_polar_ = new QRadioButton("Polar snap (Angular tracking)", grp_type);

    grp_type_btn->addButton(rb_type_rect_);
    grp_type_btn->addButton(rb_type_iso_);
    grp_type_btn->addButton(rb_type_polar_);

    v_type->addWidget(rb_type_rect_);
    v_type->addWidget(rb_type_iso_);
    v_type->addWidget(rb_type_polar_);

    auto* polar_row = new QHBoxLayout();
    polar_row->addWidget(new QLabel("Polar angle increment:"));
    spin_polar_angle_ = new QSpinBox(grp_type);
    spin_polar_angle_->setRange(1, 90);
    spin_polar_angle_->setValue(45);
    spin_polar_angle_->setSuffix("°");
    polar_row->addWidget(spin_polar_angle_);
    polar_row->addStretch();
    v_type->addLayout(polar_row);

    layout->addWidget(grp_type);

    layout->addStretch();
    tab_widget_->addTab(page, "Advanced");
  }

  void setup_snap_grid_tab() {
    auto* page = new QWidget(tab_widget_);
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);

    // Grid Spacing Mode (Fixed vs Variable)
    auto* grp_mode = new QGroupBox("Grid Spacing Mode", page);
    auto* h_mode = new QHBoxLayout(grp_mode);
    rb_spacing_var_ = new QRadioButton("Variable (Scales with zoom)", grp_mode);
    rb_spacing_fixed_ = new QRadioButton("Fixed (Custom dimensions)", grp_mode);
    auto* mode_btn = new QButtonGroup(this);
    mode_btn->addButton(rb_spacing_var_);
    mode_btn->addButton(rb_spacing_fixed_);
    h_mode->addWidget(rb_spacing_var_);
    h_mode->addWidget(rb_spacing_fixed_);
    layout->addWidget(grp_mode);

    // Snap Spacing
    auto* grp_snap_space = new QGroupBox("Snap Spacing", page);
    auto* f_snap = new QFormLayout(grp_snap_space);
    f_snap->setSpacing(8);

    chk_equal_snap_ = new QCheckBox("Equal X and Y Snap Spacing", grp_snap_space);
    chk_equal_snap_->setChecked(true);
    f_snap->addRow("", chk_equal_snap_);

    spin_snap_x_ = new QDoubleSpinBox(grp_snap_space);
    spin_snap_x_->setRange(0.1, 1000.0);
    spin_snap_x_->setSingleStep(1.0);
    spin_snap_x_->setSuffix(" mm");

    spin_snap_y_ = new QDoubleSpinBox(grp_snap_space);
    spin_snap_y_->setRange(0.1, 1000.0);
    spin_snap_y_->setSingleStep(1.0);
    spin_snap_y_->setSuffix(" mm");

    connect(spin_snap_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
      if (chk_equal_snap_->isChecked()) {
        spin_snap_y_->setValue(val);
      }
    });

    f_snap->addRow("Snap X Spacing:", spin_snap_x_);
    f_snap->addRow("Snap Y Spacing:", spin_snap_y_);
    layout->addWidget(grp_snap_space);

    // Grid Spacing
    auto* grp_grid_space = new QGroupBox("Grid Spacing (Visual Lines)", page);
    auto* f_grid = new QFormLayout(grp_grid_space);
    f_grid->setSpacing(8);

    chk_match_grid_to_snap_ = new QCheckBox("Match visual grid lines to snap increments", grp_grid_space);
    f_grid->addRow("", chk_match_grid_to_snap_);

    spin_grid_x_ = new QDoubleSpinBox(grp_grid_space);
    spin_grid_x_->setRange(1.0, 1000.0);
    spin_grid_x_->setSingleStep(5.0);
    spin_grid_x_->setSuffix(" mm");

    spin_grid_y_ = new QDoubleSpinBox(grp_grid_space);
    spin_grid_y_->setRange(1.0, 1000.0);
    spin_grid_y_->setSingleStep(5.0);
    spin_grid_y_->setSuffix(" mm");

    spin_grid_subdiv_ = new QSpinBox(grp_grid_space);
    spin_grid_subdiv_->setRange(1, 20);
    spin_grid_subdiv_->setValue(4);

    chk_grid_visible_ = new QCheckBox("Show Grid Lines (Ctrl+G)", grp_grid_space);
    chk_grid_subdiv_ = new QCheckBox("Show Minor Subdivisions", grp_grid_space);

    f_grid->addRow("Major Spacing X:", spin_grid_x_);
    f_grid->addRow("Major Spacing Y:", spin_grid_y_);
    f_grid->addRow("Subdivisions:", spin_grid_subdiv_);
    f_grid->addRow("", chk_grid_visible_);
    f_grid->addRow("", chk_grid_subdiv_);
    layout->addWidget(grp_grid_space);

    layout->addStretch();
    tab_widget_->addTab(page, "Snap and Grid");
  }

  void setup_page_tab() {
    auto* page = new QWidget(tab_widget_);
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(12);

    // 1. Page Presets
    auto* grp_preset = new QGroupBox("Page Size Preset", page);
    auto* f_preset = new QFormLayout(grp_preset);

    combo_page_preset_ = new QComboBox(grp_preset);
    combo_page_preset_->addItem("Letter (8.5 × 11 in)", static_cast<int>(renderer::PagePreset::Letter));
    combo_page_preset_->addItem("Legal (8.5 × 14 in)", static_cast<int>(renderer::PagePreset::Legal));
    combo_page_preset_->addItem("Tabloid / Ledger (11 × 17 in)", static_cast<int>(renderer::PagePreset::Tabloid));
    combo_page_preset_->addItem("A4 (210 × 297 mm)", static_cast<int>(renderer::PagePreset::A4));
    combo_page_preset_->addItem("A3 (297 × 420 mm)", static_cast<int>(renderer::PagePreset::A3));
    combo_page_preset_->addItem("A2 (420 × 594 mm)", static_cast<int>(renderer::PagePreset::A2));
    combo_page_preset_->addItem("A1 (594 × 841 mm)", static_cast<int>(renderer::PagePreset::A1));
    combo_page_preset_->addItem("A0 (841 × 1189 mm)", static_cast<int>(renderer::PagePreset::A0));
    combo_page_preset_->addItem("Custom Dimensions", static_cast<int>(renderer::PagePreset::Custom));
    f_preset->addRow("Standard Format:", combo_page_preset_);

    auto* grp_orient = new QGroupBox("Orientation", grp_preset);
    auto* h_orient = new QHBoxLayout(grp_orient);
    rb_orient_landscape_ = new QRadioButton("Landscape", grp_orient);
    rb_orient_portrait_ = new QRadioButton("Portrait", grp_orient);
    rb_orient_landscape_->setChecked(true);
    h_orient->addWidget(rb_orient_landscape_);
    h_orient->addWidget(rb_orient_portrait_);
    f_preset->addRow("Orientation:", grp_orient);
    layout->addWidget(grp_preset);

    // 2. Custom Dimensions & Margins
    auto* grp_dim = new QGroupBox("Page Dimensions & Margins", page);
    auto* f_dim = new QFormLayout(grp_dim);

    spin_page_width_ = new QDoubleSpinBox(grp_dim);
    spin_page_width_->setRange(50.0, 50000.0);
    spin_page_width_->setSingleStep(10.0);
    spin_page_width_->setDecimals(1);
    spin_page_width_->setValue(1100.0);

    spin_page_height_ = new QDoubleSpinBox(grp_dim);
    spin_page_height_->setRange(50.0, 50000.0);
    spin_page_height_->setSingleStep(10.0);
    spin_page_height_->setDecimals(1);
    spin_page_height_->setValue(850.0);

    spin_page_margin_ = new QDoubleSpinBox(grp_dim);
    spin_page_margin_->setRange(0.0, 1000.0);
    spin_page_margin_->setSingleStep(5.0);
    spin_page_margin_->setDecimals(1);
    spin_page_margin_->setValue(25.0);

    f_dim->addRow("Page Width:", spin_page_width_);
    f_dim->addRow("Page Height:", spin_page_height_);
    f_dim->addRow("Print Margin:", spin_page_margin_);
    layout->addWidget(grp_dim);

    // 3. Display Options
    auto* grp_disp = new QGroupBox("Visual Page Sheet", page);
    auto* v_disp = new QVBoxLayout(grp_disp);
    chk_show_page_sheet_ = new QCheckBox("Display Bounded Page Sheet on Canvas", grp_disp);
    chk_show_page_sheet_->setChecked(true);
    v_disp->addWidget(chk_show_page_sheet_);
    layout->addWidget(grp_disp);

    layout->addStretch();
    tab_widget_->addTab(page, "Page Setup");

    // Dynamic synchronization
    auto update_dims_from_preset = [this]() {
      const auto preset = static_cast<renderer::PagePreset>(combo_page_preset_->currentData().toInt());
      if (preset != renderer::PagePreset::Custom) {
        const auto orient = rb_orient_landscape_->isChecked() ?
            renderer::PageOrientation::Landscape : renderer::PageOrientation::Portrait;
        const auto dims = renderer::get_page_preset_dimensions(preset, orient);
        spin_page_width_->blockSignals(true);
        spin_page_height_->blockSignals(true);
        spin_page_width_->setValue(dims.x);
        spin_page_height_->setValue(dims.y);
        spin_page_width_->blockSignals(false);
        spin_page_height_->blockSignals(false);
      }
    };

    connect(combo_page_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update_dims_from_preset);
    connect(rb_orient_landscape_, &QRadioButton::toggled, this, [update_dims_from_preset](bool checked) {
      if (checked) update_dims_from_preset();
    });
    connect(rb_orient_portrait_, &QRadioButton::toggled, this, [update_dims_from_preset](bool checked) {
      if (checked) update_dims_from_preset();
    });

    auto on_custom_dim_changed = [this]() {
      int custom_idx = combo_page_preset_->findData(static_cast<int>(renderer::PagePreset::Custom));
      if (custom_idx >= 0 && combo_page_preset_->currentIndex() != custom_idx) {
        combo_page_preset_->blockSignals(true);
        combo_page_preset_->setCurrentIndex(custom_idx);
        combo_page_preset_->blockSignals(false);
      }
      if (spin_page_width_->value() >= spin_page_height_->value()) {
        rb_orient_landscape_->blockSignals(true);
        rb_orient_landscape_->setChecked(true);
        rb_orient_landscape_->blockSignals(false);
      } else {
        rb_orient_portrait_->blockSignals(true);
        rb_orient_portrait_->setChecked(true);
        rb_orient_portrait_->blockSignals(false);
      }
    };

    connect(spin_page_width_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, on_custom_dim_changed);
    connect(spin_page_height_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, on_custom_dim_changed);
  }

public:
  QTabWidget* tab_widget_{nullptr};

  // General tab widgets
  QCheckBox* chk_enable_snap_{nullptr};
  QCheckBox* chk_target_grid_{nullptr};
  QCheckBox* chk_target_guides_{nullptr};
  QCheckBox* chk_target_geometry_{nullptr};
  QCheckBox* chk_target_ports_{nullptr};
  QCheckBox* chk_target_ortho_{nullptr};
  QCheckBox* chk_remember_settings_{nullptr};
  QCheckBox* chk_angle_snap_{nullptr};
  QDoubleSpinBox* spin_angle_snap_step_{nullptr};
  QCheckBox* chk_cardinal_priority_{nullptr};

  // Advanced tab widgets
  QSlider* slider_strength_{nullptr};
  QDoubleSpinBox* spin_strength_{nullptr};
  QRadioButton* rb_strength_off_{nullptr};
  QRadioButton* rb_strength_weak_{nullptr};
  QRadioButton* rb_strength_normal_{nullptr};
  QRadioButton* rb_strength_strong_{nullptr};
  QRadioButton* rb_strength_custom_{nullptr};

  QRadioButton* rb_type_rect_{nullptr};
  QRadioButton* rb_type_iso_{nullptr};
  QRadioButton* rb_type_polar_{nullptr};
  QSpinBox* spin_polar_angle_{nullptr};

  // Snap & Grid tab widgets
  QRadioButton* rb_spacing_var_{nullptr};
  QRadioButton* rb_spacing_fixed_{nullptr};
  QCheckBox* chk_equal_snap_{nullptr};
  QDoubleSpinBox* spin_snap_x_{nullptr};
  QDoubleSpinBox* spin_snap_y_{nullptr};

  QCheckBox* chk_match_grid_to_snap_{nullptr};
  QDoubleSpinBox* spin_grid_x_{nullptr};
  QDoubleSpinBox* spin_grid_y_{nullptr};
  QSpinBox* spin_grid_subdiv_{nullptr};
  QCheckBox* chk_grid_visible_{nullptr};
  QCheckBox* chk_grid_subdiv_{nullptr};

  // Page Setup tab widgets
  QComboBox* combo_page_preset_{nullptr};
  QRadioButton* rb_orient_landscape_{nullptr};
  QRadioButton* rb_orient_portrait_{nullptr};
  QDoubleSpinBox* spin_page_width_{nullptr};
  QDoubleSpinBox* spin_page_height_{nullptr};
  QDoubleSpinBox* spin_page_margin_{nullptr};
  QCheckBox* chk_show_page_sheet_{nullptr};
};

} // namespace madi::gui

#endif // MADI_GUI_SNAP_GRID_SETTINGS_DIALOG_HPP
