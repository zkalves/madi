// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_GUI_STENCIL_LIBRARY_DOCK_HPP
#define VECTOR_GUI_STENCIL_LIBRARY_DOCK_HPP

#ifdef VECTOR_ENABLE_QT

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <cmath>
#include <numbers>
#include <vector>

#include "vector/core/GraphModel.hpp"
#include "vector/gui/IconFactory.hpp"
#include "vector/gui/ThemeManager.hpp"
#include "vector/renderer/CanvasWidget.hpp"

namespace vector::gui {

class StencilLibraryDock;

class StencilTreeWidget : public QTreeWidget {
public:
  explicit StencilTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
  }

  void set_dock(StencilLibraryDock* dock) noexcept { dock_ = dock; }

protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      drag_start_pos_ = event->pos();
    }
    QTreeWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  QPoint drag_start_pos_;
  StencilLibraryDock* dock_{nullptr};
};

class StencilLibraryDock : public QDockWidget {
  Q_OBJECT

public:
  enum class StencilItemType {
    // 1. 2D Geometric Primitives (Universal Constructive CSG Basis)
    ShapeRect,
    ShapeRightTriangle,
    ShapeTriangle,
    ShapeEllipse,
    ShapeRoundedRect,
    ShapeDiamond,
    ShapeParallelogram,
    ShapeTrapezoid,
    ShapeCross,
    ShapeStar,
    ShapeChevron,
    ShapeCylinder,
    ShapePolygonTool,

    // 2. Regular N-gon Series
    ShapePentagon,
    ShapeHexagon,
    ShapeHeptagon,
    ShapeOctagon,
    ShapeDecagon,
    ShapeDodecagon,
    ShapePolygonN,
    ShapePolygon = ShapePolygonN,

    // 3. Hierarchy & EDA Ports
    ShapeInPort,
    ShapeOutPort,
    ShapeBiPort,
    SubsystemBuiltin,
    SubsystemFile,

    // 4. Connectors & Routing
    ToolConnector,
    ToolAutoArrow,

    // 5. Custom Stencils
    CustomShape
  };

  explicit StencilLibraryDock(QWidget* parent = nullptr)
      : QDockWidget("Shapes", parent) {
    setObjectName("ShapesDock");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Search bar (compact selector)
    search_edit_ = new QLineEdit(container);
    search_edit_->setPlaceholderText("🔍 Search shapes & primitives...");
    search_edit_->setClearButtonEnabled(true);
    search_edit_->setFixedHeight(24);
    search_edit_->setStyleSheet("QLineEdit { padding: 2px 6px; font-size: 11px; }");
    connect(search_edit_, &QLineEdit::textChanged, this, &StencilLibraryDock::filter_items);
    layout->addWidget(search_edit_);

    // Tree of stencils with prominent 28x28 icons for clear visibility
    tree_ = new StencilTreeWidget(container);
    tree_->set_dock(this);
    tree_->setHeaderHidden(true);
    tree_->setIndentation(12);
    tree_->setAnimated(true);
    tree_->setIconSize(QSize(28, 28));
    tree_->setStyleSheet(
        "QTreeWidget { font-size: 11px; } "
        "QTreeWidget::item { padding: 3px 4px; min-height: 32px; } "
        "QTreeWidget::item:selected { background-color: #0078D4; color: #FFFFFF; }");
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &StencilLibraryDock::show_context_menu);
    layout->addWidget(tree_, 1);

    // Bottom Action Row: [ ➕ Add... ] [ 🗑 Remove ] [ Insert ]
    auto* action_row = new QHBoxLayout();
    action_row->setContentsMargins(0, 0, 0, 0);
    action_row->setSpacing(3);

    btn_add_ = new QPushButton("➕ Add...", container);
    btn_add_->setFixedHeight(24);
    btn_add_->setToolTip("Add custom shape, new section, or capture from canvas element");
    btn_add_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 6px; }");
    connect(btn_add_, &QPushButton::clicked, this, &StencilLibraryDock::show_add_menu);
    action_row->addWidget(btn_add_);

    btn_remove_ = new QPushButton("🗑 Remove", container);
    btn_remove_->setFixedHeight(24);
    btn_remove_->setEnabled(false);
    btn_remove_->setToolTip("Remove selected custom shape or custom section (Delete key)");
    btn_remove_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 6px; }");
    connect(btn_remove_, &QPushButton::clicked, this, &StencilLibraryDock::remove_current_selected);
    action_row->addWidget(btn_remove_);

    btn_insert_ = new QPushButton("Insert", container);
    btn_insert_->setFixedHeight(24);
    btn_insert_->setToolTip("Insert selected shape onto canvas center");
    btn_insert_->setStyleSheet("QPushButton { font-size: 11px; font-weight: bold; padding: 2px 8px; }");
    connect(btn_insert_, &QPushButton::clicked, this, &StencilLibraryDock::insert_current_selected);
    action_row->addWidget(btn_insert_);

    layout->addLayout(action_row);

    setWidget(container);

    populate_stencils();

    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int col) {
      (void)col;
      trigger_item(item);
    });

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this]() {
      auto* current = tree_->currentItem();
      if (!current) {
        btn_remove_->setEnabled(false);
        return;
      }
      if (current->parent() == nullptr) {
        btn_remove_->setEnabled(!is_category_builtin(current));
      } else {
        btn_remove_->setEnabled(!is_item_builtin(current));
      }
    });

    auto* del_shortcut = new QShortcut(QKeySequence::Delete, tree_);
    connect(del_shortcut, &QShortcut::activated, this, &StencilLibraryDock::remove_current_selected);
  }

  void set_canvas(renderer::CanvasWidget* canvas) noexcept {
    canvas_ = canvas;
  }

  [[nodiscard]] int total_shape_count() const noexcept {
    int count = 0;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      count += tree_->topLevelItem(i)->childCount();
    }
    return count;
  }

  [[nodiscard]] bool has_shape(const QString& name) const noexcept {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      auto* cat = tree_->topLevelItem(i);
      for (int j = 0; j < cat->childCount(); ++j) {
        if (cat->child(j)->text(0).contains(name, Qt::CaseInsensitive)) {
          return true;
        }
      }
    }
    return false;
  }

  void update_icons(const QColor& fg) {
    current_fg_ = fg;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      auto* cat = tree_->topLevelItem(i);
      for (int j = 0; j < cat->childCount(); ++j) {
        auto* child = cat->child(j);
        QVariant itype_var = child->data(0, Qt::UserRole + 3);
        if (itype_var.isValid()) {
          auto itype = static_cast<IconFactory::IconType>(itype_var.toInt());
          child->setIcon(0, IconFactory::create_icon(itype, fg));
        }
      }
    }
  }

  QTreeWidgetItem* add_shape_item(
      const QString& name,
      StencilItemType type,
      int param = 0,
      const QString& category = "⭐ Custom Shapes",
      IconFactory::IconType itype = IconFactory::IconType::Rectangle) {
    auto* cat = find_or_create_category(category);
    QString title = name;
    if (itype == IconFactory::IconType::Rectangle) {
      switch (type) {
        case StencilItemType::ShapeRect: itype = IconFactory::IconType::Rectangle; break;
        case StencilItemType::ShapeRightTriangle: itype = IconFactory::IconType::RightTriangle; break;
        case StencilItemType::ShapeTriangle: itype = IconFactory::IconType::Triangle; break;
        case StencilItemType::ShapeRoundedRect: itype = IconFactory::IconType::RoundedRect; break;
        case StencilItemType::ShapeEllipse: itype = IconFactory::IconType::Ellipse; break;
        case StencilItemType::ShapeDiamond: itype = IconFactory::IconType::Diamond; break;
        case StencilItemType::ShapePentagon: itype = IconFactory::IconType::Pentagon; break;
        case StencilItemType::ShapeHexagon: itype = IconFactory::IconType::PolygonHex; break;
        case StencilItemType::ShapeHeptagon: itype = IconFactory::IconType::Heptagon; break;
        case StencilItemType::ShapeOctagon: itype = IconFactory::IconType::Octagon; break;
        case StencilItemType::ShapeDecagon: itype = IconFactory::IconType::Decagon; break;
        case StencilItemType::ShapeDodecagon: itype = IconFactory::IconType::Dodecagon; break;
        case StencilItemType::ShapeCross: itype = IconFactory::IconType::Cross; break;
        case StencilItemType::ShapeStar: itype = IconFactory::IconType::Star; break;
        case StencilItemType::ShapeTrapezoid: itype = IconFactory::IconType::Trapezoid; break;
        case StencilItemType::ShapeParallelogram: itype = IconFactory::IconType::Parallelogram; break;
        case StencilItemType::ShapeChevron: itype = IconFactory::IconType::Chevron; break;
        case StencilItemType::ShapeCylinder: itype = IconFactory::IconType::Cylinder; break;
        case StencilItemType::ShapeInPort: itype = IconFactory::IconType::InputPort; break;
        case StencilItemType::ShapeOutPort: itype = IconFactory::IconType::OutputPort; break;
        case StencilItemType::ShapeBiPort: itype = IconFactory::IconType::BidirectionalPort; break;
        case StencilItemType::SubsystemBuiltin: itype = IconFactory::IconType::SubsystemBlock; break;
        case StencilItemType::SubsystemFile: itype = IconFactory::IconType::SubsystemFile; break;
        default: itype = IconFactory::IconType::Rectangle; break;
      }
    }
    auto* item = add_stencil_item(cat, title, type, param, itype, QString(), false);
    item->setData(0, Qt::UserRole + 2, name);
    item->setData(0, Qt::UserRole + 10, false);
    cat->setExpanded(true);
    tree_->setCurrentItem(item);
    return item;
  }

  [[nodiscard]] bool is_item_builtin(const QTreeWidgetItem* item) const noexcept {
    if (!item) return false;
    return item->data(0, Qt::UserRole + 10).toBool();
  }

  [[nodiscard]] bool is_category_builtin(const QTreeWidgetItem* cat) const noexcept {
    if (!cat) return false;
    return cat->data(0, Qt::UserRole + 10).toBool();
  }

  QTreeWidgetItem* create_new_section(const QString& name) {
    QString clean_name = name.trimmed();
    if (clean_name.isEmpty()) clean_name = "New Section";
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      if (tree_->topLevelItem(i)->text(0) == clean_name) {
        return tree_->topLevelItem(i);
      }
    }
    auto* new_cat = new QTreeWidgetItem(tree_, QStringList() << clean_name);
    new_cat->setData(0, Qt::UserRole + 10, false);
    new_cat->setExpanded(true);
    QFont bold_font = new_cat->font(0);
    bold_font.setBold(true);
    new_cat->setFont(0, bold_font);
    return new_cat;
  }

  [[nodiscard]] bool has_section(const QString& name) const noexcept {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      if (tree_->topLevelItem(i)->text(0).contains(name, Qt::CaseInsensitive)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] int custom_section_count() const noexcept {
    int count = 0;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      if (!is_category_builtin(tree_->topLevelItem(i))) {
        count++;
      }
    }
    return count;
  }

  bool remove_section(const QString& name) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      auto* cat = tree_->topLevelItem(i);
      if (cat->text(0).contains(name, Qt::CaseInsensitive)) {
        if (is_category_builtin(cat)) return false; // Built-in sections protected!
        delete tree_->takeTopLevelItem(i);
        return true;
      }
    }
    return false;
  }

  void prompt_new_section_dialog() {
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "New Section", "Section Name:",
        QLineEdit::Normal, "⭐ My Custom Shapes", &ok);
    if (ok && !name.trimmed().isEmpty()) {
      auto* cat = create_new_section(name.trimmed());
      tree_->setCurrentItem(cat);
    }
  }

  void prompt_rename_section(QTreeWidgetItem* cat) {
    if (!cat || is_category_builtin(cat)) return;
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "Rename Section", "New Section Name:",
        QLineEdit::Normal, cat->text(0), &ok);
    if (ok && !name.trimmed().isEmpty()) {
      cat->setText(0, name.trimmed());
    }
  }

  bool remove_shape(const QString& name) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      auto* cat = tree_->topLevelItem(i);
      for (int j = 0; j < cat->childCount(); ++j) {
        auto* child = cat->child(j);
        if (child->text(0).contains(name, Qt::CaseInsensitive)) {
          if (is_item_builtin(child)) continue; // Built-in shapes protected; check for custom duplicates
          cat->removeChild(child);
          delete child;
          return true;
        }
      }
    }
    return false;
  }

  void reset_default_shapes() {
    populate_stencils();
  }

  [[nodiscard]] StencilTreeWidget* tree_widget() const noexcept { return tree_; }
  [[nodiscard]] QLineEdit* search_edit() const noexcept { return search_edit_; }
  [[nodiscard]] QPushButton* add_button() const noexcept { return btn_add_; }
  [[nodiscard]] QPushButton* remove_button() const noexcept { return btn_remove_; }
  [[nodiscard]] QPushButton* insert_button() const noexcept { return btn_insert_; }

  QTreeWidgetItem* handle_dropped_template(const renderer::ShapePlacementTemplate& tmpl, QTreeWidgetItem* target_item = nullptr) {
    QTreeWidgetItem* target_cat = nullptr;
    if (target_item) {
      if (target_item->parent() == nullptr) {
        if (!is_category_builtin(target_item)) {
          target_cat = target_item;
        }
      } else {
        if (!is_category_builtin(target_item->parent())) {
          target_cat = target_item->parent();
        }
      }
    }
    if (!target_cat) {
      for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* cat = tree_->topLevelItem(i);
        if (!is_category_builtin(cat)) {
          target_cat = cat;
          break;
        }
      }
    }
    if (!target_cat) {
      target_cat = create_new_section("⭐ Custom Shapes");
    }

    QString title = QString::fromStdString(tmpl.name.empty() ? "Custom Shape" : tmpl.name);
    StencilItemType stype = StencilItemType::CustomShape;
    IconFactory::IconType itype = IconFactory::IconType::Rectangle;
    int sides = 0;

    if (tmpl.category == renderer::PlacementCategory::HierarchicalPort) {
      if (tmpl.shape_type == core::ShapeType::BidirectionalPort || tmpl.port_type == "inout" || tmpl.port_type == "bidirectional") {
        stype = StencilItemType::ShapeBiPort;
        itype = IconFactory::IconType::BidirectionalPort;
      } else if (tmpl.is_input_port || tmpl.port_type == "in") {
        stype = StencilItemType::ShapeInPort;
        itype = IconFactory::IconType::InputPort;
      } else {
        stype = StencilItemType::ShapeOutPort;
        itype = IconFactory::IconType::OutputPort;
      }
    } else if (tmpl.category == renderer::PlacementCategory::SubsystemBuiltin) {
      stype = StencilItemType::SubsystemBuiltin;
      itype = IconFactory::IconType::SubsystemBlock;
    } else if (tmpl.category == renderer::PlacementCategory::SubsystemFile) {
      stype = StencilItemType::SubsystemFile;
      itype = IconFactory::IconType::SubsystemFile;
    } else if (tmpl.category == renderer::PlacementCategory::RegularPolygon) {
      stype = StencilItemType::ShapePolygonN;
      sides = tmpl.polygon_sides;
      itype = (sides == 3) ? IconFactory::IconType::Triangle :
              (sides == 4) ? IconFactory::IconType::Diamond :
              (sides == 5) ? IconFactory::IconType::Pentagon :
              (sides == 6) ? IconFactory::IconType::PolygonHex :
              (sides == 8) ? IconFactory::IconType::Octagon : IconFactory::IconType::PolygonDraw;
    } else if (tmpl.category == renderer::PlacementCategory::CustomPolygon) {
      stype = StencilItemType::CustomShape;
      itype = IconFactory::IconType::PolygonDraw;
    } else {
      switch (tmpl.shape_type) {
        case core::ShapeType::Rectangle:
          stype = StencilItemType::ShapeRect;
          itype = IconFactory::IconType::Rectangle;
          break;
        case core::ShapeType::RoundedRect:
          stype = StencilItemType::ShapeRoundedRect;
          itype = IconFactory::IconType::RoundedRect;
          break;
        case core::ShapeType::Ellipse:
          stype = StencilItemType::ShapeEllipse;
          itype = IconFactory::IconType::Ellipse;
          break;
        case core::ShapeType::BidirectionalPort:
          stype = StencilItemType::ShapeBiPort;
          itype = IconFactory::IconType::BidirectionalPort;
          break;
        case core::ShapeType::InputPort:
          stype = StencilItemType::ShapeInPort;
          itype = IconFactory::IconType::InputPort;
          break;
        case core::ShapeType::OutputPort:
          stype = StencilItemType::ShapeOutPort;
          itype = IconFactory::IconType::OutputPort;
          break;
        default:
          stype = StencilItemType::CustomShape;
          itype = IconFactory::IconType::Rectangle;
          break;
      }
    }

    auto* item = new QTreeWidgetItem(target_cat, QStringList() << title);
    item->setData(0, Qt::UserRole, static_cast<int>(stype));
    item->setData(0, Qt::UserRole + 1, sides);
    item->setData(0, Qt::UserRole + 2, title);
    item->setData(0, Qt::UserRole + 3, static_cast<int>(itype));
    item->setData(0, Qt::UserRole + 4, QPointF(tmpl.anchor_point.x, tmpl.anchor_point.y));
    item->setData(0, Qt::UserRole + 5, QPointF(tmpl.resize_point.x, tmpl.resize_point.y));
    item->setData(0, Qt::UserRole + 6, renderer::serialize_placement_template(tmpl));
    item->setData(0, Qt::UserRole + 10, false); // NOT builtin!
    item->setIcon(0, IconFactory::create_icon(itype, current_fg_));
    target_cat->setExpanded(true);
    tree_->setCurrentItem(item);
    return item;
  }

public slots:
  void insert_current_selected() {
    auto* current = tree_->currentItem();
    if (current && current->parent() != nullptr) {
      trigger_item(current);
    }
  }

  bool remove_current_selected() {
    auto* current = tree_->currentItem();
    if (!current) return false;
    if (current->parent() == nullptr) {
      if (is_category_builtin(current)) {
        return false; // Built-in category cannot be removed!
      }
      int idx = tree_->indexOfTopLevelItem(current);
      if (idx >= 0) {
        delete tree_->takeTopLevelItem(idx);
        btn_remove_->setEnabled(false);
        return true;
      }
      return false;
    }
    if (is_item_builtin(current)) {
      return false; // Built-in shape cannot be removed!
    }
    auto* parent = current->parent();
    parent->removeChild(current);
    delete current;
    btn_remove_->setEnabled(false);
    return true;
  }

  void show_add_menu() {
    QMenu menu(this);
    auto* act_new_sec = menu.addAction("📁 New Section...");
    auto* act_custom = menu.addAction("📐 Add Custom Shape / N-gon...");
    auto* act_from_sel = menu.addAction("⭐ Add Selected from Canvas");
    if (!canvas_ || canvas_->selected_count() == 0) {
      act_from_sel->setEnabled(false);
    }
    menu.addSeparator();
    auto* act_reset = menu.addAction("🔄 Restore Default Shapes");

    QPoint popup_pos = btn_add_ ? btn_add_->mapToGlobal(QPoint(0, btn_add_->height())) : QCursor::pos();
    QAction* chosen = menu.exec(popup_pos);
    if (chosen == act_new_sec) {
      prompt_new_section_dialog();
    } else if (chosen == act_custom) {
      prompt_custom_shape_dialog();
    } else if (chosen == act_from_sel) {
      add_from_selection();
    } else if (chosen == act_reset) {
      reset_default_shapes();
    }
  }

  void prompt_custom_shape_dialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Add Shape to Palette");
    auto* layout = new QVBoxLayout(&dlg);
    layout->setSpacing(8);
    auto* form = new QFormLayout();

    auto* edit_name = new QLineEdit(&dlg);
    edit_name->setPlaceholderText("e.g. Inverter, Chamfer Box, Motor");
    form->addRow("Shape Name:", edit_name);

    auto* combo_type = new QComboBox(&dlg);
    combo_type->addItem("Rectangle Block", static_cast<int>(StencilItemType::ShapeRect));
    combo_type->addItem("Right-Angled Triangle (Chamfer)", static_cast<int>(StencilItemType::ShapeRightTriangle));
    combo_type->addItem("Isosceles Triangle", static_cast<int>(StencilItemType::ShapeTriangle));
    combo_type->addItem("Circle & Ellipse", static_cast<int>(StencilItemType::ShapeEllipse));
    combo_type->addItem("Rounded Rectangle", static_cast<int>(StencilItemType::ShapeRoundedRect));
    combo_type->addItem("Diamond / Rhombus", static_cast<int>(StencilItemType::ShapeDiamond));
    combo_type->addItem("Parallelogram (Skewed Quad)", static_cast<int>(StencilItemType::ShapeParallelogram));
    combo_type->addItem("Trapezoid (Tapered Quad)", static_cast<int>(StencilItemType::ShapeTrapezoid));
    combo_type->addItem("Orthogonal Cross (+)", static_cast<int>(StencilItemType::ShapeCross));
    combo_type->addItem("5-Pointed Star", static_cast<int>(StencilItemType::ShapeStar));
    combo_type->addItem("Arrow Block / Chevron", static_cast<int>(StencilItemType::ShapeChevron));
    combo_type->addItem("Cylinder / Storage Drum", static_cast<int>(StencilItemType::ShapeCylinder));
    combo_type->addItem("Regular Polygon (N-gon)", static_cast<int>(StencilItemType::ShapePolygonN));
    combo_type->addItem("Hierarchical Input Port", static_cast<int>(StencilItemType::ShapeInPort));
    combo_type->addItem("Hierarchical Output Port", static_cast<int>(StencilItemType::ShapeOutPort));
    combo_type->addItem("Bidirectional Port", static_cast<int>(StencilItemType::ShapeBiPort));
    combo_type->addItem("Built-in Subsystem", static_cast<int>(StencilItemType::SubsystemBuiltin));
    form->addRow("Shape Type:", combo_type);

    auto* spin_sides = new QSpinBox(&dlg);
    spin_sides->setRange(3, 32);
    spin_sides->setValue(5);
    spin_sides->setEnabled(false);
    form->addRow("Polygon Sides (N):", spin_sides);

    connect(combo_type, QOverload<int>::of(&QComboBox::currentIndexChanged), [combo_type, spin_sides](int) {
      auto type = static_cast<StencilItemType>(combo_type->currentData().toInt());
      spin_sides->setEnabled(type == StencilItemType::ShapePolygonN);
    });

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() == QDialog::Accepted) {
      QString name = edit_name->text().trimmed();
      if (name.isEmpty()) name = combo_type->currentText();
      auto type = static_cast<StencilItemType>(combo_type->currentData().toInt());
      int sides = (type == StencilItemType::ShapePolygonN) ? spin_sides->value() : 0;
      add_shape_item(name, type, sides, "⭐ Custom Shapes");
    }
  }

  void add_from_selection() {
    if (!canvas_) return;
    const auto sel = canvas_->selected_nodes();
    if (sel.empty()) return;

    auto* graph = canvas_->graph_model();
    if (!graph) return;

    const auto* node = graph->find_node(sel.front());
    if (!node) return;

    renderer::ShapePlacementTemplate tmpl;
    tmpl.name = node->name.empty() ? "Canvas Shape" : node->name;
    tmpl.width = node->bounds.width;
    tmpl.height = node->bounds.height;
    tmpl.anchor_point = node->symbol_anchor();
    tmpl.resize_point = node->symbol_resize_point();

    if (node->is_interface_port()) {
      tmpl.category = renderer::PlacementCategory::HierarchicalPort;
      if (node->is_bidirectional_port()) {
        tmpl.shape_type = core::ShapeType::BidirectionalPort;
        tmpl.port_type = "inout";
        tmpl.is_input_port = false;
      } else if (node->is_input_port()) {
        tmpl.shape_type = core::ShapeType::InputPort;
        tmpl.port_type = "in";
        tmpl.is_input_port = true;
      } else {
        tmpl.shape_type = core::ShapeType::OutputPort;
        tmpl.port_type = "out";
        tmpl.is_input_port = false;
      }
    } else if (node->is_subsystem()) {
      if (node->is_file_subsystem()) {
        tmpl.category = renderer::PlacementCategory::SubsystemFile;
        tmpl.file_path = node->subsystem_file();
      } else {
        tmpl.category = renderer::PlacementCategory::SubsystemBuiltin;
      }
    } else if (node->shape == core::ShapeType::CustomPolygon) {
      tmpl.category = renderer::PlacementCategory::CustomPolygon;
      tmpl.shape_type = core::ShapeType::CustomPolygon;
      tmpl.custom_vertices = node->polygon_vertices;
    } else {
      tmpl.category = renderer::PlacementCategory::ShapeType;
      tmpl.shape_type = node->shape;
    }

    handle_dropped_template(tmpl, tree_->currentItem());
  }

private:
  QTreeWidgetItem* find_or_create_category(const QString& cat_name) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      auto* cat = tree_->topLevelItem(i);
      if (cat->text(0) == cat_name) return cat;
    }
    auto* new_cat = new QTreeWidgetItem(tree_, QStringList() << cat_name);
    new_cat->setData(0, Qt::UserRole + 10, false); // Custom category!
    new_cat->setExpanded(true);
    QFont bold_font = new_cat->font(0);
    bold_font.setBold(true);
    new_cat->setFont(0, bold_font);
    return new_cat;
  }

  void populate_stencils() {
    tree_->clear();
    populate_builtin_stencils();
  }

  void populate_builtin_stencils() {
    // 1. 2D Geometric Primitives (Universal Constructive Basis)
    auto* cat_primitives = new QTreeWidgetItem(QStringList() << "📐 2D Geometric Primitives (CSG Basis)");
    cat_primitives->setData(0, Qt::UserRole + 10, true);
    cat_primitives->setExpanded(true);
    QFont bold_font = cat_primitives->font(0);
    bold_font.setBold(true);
    cat_primitives->setFont(0, bold_font);
    tree_->insertTopLevelItem(0, cat_primitives);

    add_stencil_item(cat_primitives, "Rectangle Block", StencilItemType::ShapeRect, 0,
                     IconFactory::IconType::Rectangle,
                     "Rectangle Quadrilateral (R)\nFundamental orthogonal basis for chips, blocks, and frames.", true);
    add_stencil_item(cat_primitives, "Right-Angled Triangle", StencilItemType::ShapeRightTriangle, 0,
                     IconFactory::IconType::RightTriangle,
                     "Right Triangle (45°-90°-45°)\nFundamental basis for triangulation, slopes, chamfers, and ramps.", true);
    add_stencil_item(cat_primitives, "Isosceles Triangle", StencilItemType::ShapeTriangle, 3,
                     IconFactory::IconType::Triangle,
                     "Equilateral/Isosceles Triangle (3-gon)\nSymmetric directional wedge basis for delta connections and ramps.", true);
    add_stencil_item(cat_primitives, "State Ellipse & Circle", StencilItemType::ShapeEllipse, 0,
                     IconFactory::IconType::Ellipse,
                     "Circle & Ellipse (E)\nFundamental curved boundary basis for circular arcs, holes, cams, and washers.", true);
    add_stencil_item(cat_primitives, "Rounded Rectangle", StencilItemType::ShapeRoundedRect, 0,
                     IconFactory::IconType::RoundedRect,
                     "Rounded Rectangle (Stadium/Pill)\nCurved corner box basis with smooth radius corners.", true);
    add_stencil_item(cat_primitives, "Diamond / Rhombus", StencilItemType::ShapeDiamond, 4,
                     IconFactory::IconType::Diamond,
                     "Diamond / Rhombus (4-gon)\nRotated quadrilateral basis for decision blocks and flow branch gates.", true);
    add_stencil_item(cat_primitives, "Parallelogram", StencilItemType::ShapeParallelogram, 0,
                     IconFactory::IconType::Parallelogram,
                     "Parallelogram\nSheared quadrilateral basis for flowchart I/O and skewed projections.", true);
    add_stencil_item(cat_primitives, "Trapezoid", StencilItemType::ShapeTrapezoid, 0,
                     IconFactory::IconType::Trapezoid,
                     "Trapezoid\nTapered quadrilateral basis for funnels, perspective boxes, and flow pipelines.", true);
    add_stencil_item(cat_primitives, "Orthogonal Cross (+)", StencilItemType::ShapeCross, 0,
                     IconFactory::IconType::Cross,
                     "Orthogonal Cross (+)\n12-vertex symmetric orthogonal cross for junctions, fiducials, and safety signs.", true);
    add_stencil_item(cat_primitives, "5-Pointed Star", StencilItemType::ShapeStar, 5,
                     IconFactory::IconType::Star,
                     "Star (5-Pointed)\nConcave star polygon basis. Combine or intersect with circles and polygons.", true);
    add_stencil_item(cat_primitives, "Cylinder / Storage Drum", StencilItemType::ShapeCylinder, 0,
                     IconFactory::IconType::Cylinder,
                     "Cylinder / Database Drum\n3D curved cylinder projection basis for databases, capacitors, and barrels.", true);
    add_stencil_item(cat_primitives, "Arrow Block / Chevron", StencilItemType::ShapeChevron, 0,
                     IconFactory::IconType::Chevron,
                     "Arrow Block / Chevron\nDirectional block basis for pipeline stages, flow queues, and vectors.", true);
    add_stencil_item(cat_primitives, "Custom Polygon Tool", StencilItemType::ShapePolygonTool, 0,
                     IconFactory::IconType::PolygonDraw,
                     "Custom Polygon Tool (P)\nClick points on canvas to draw ANY arbitrary polygonal shape with any number of vertices.", true);

    // 2. Regular N-gon Series
    auto* cat_ngon = new QTreeWidgetItem(QStringList() << "⬡ Regular Polygons (N-gon Series)");
    cat_ngon->setData(0, Qt::UserRole + 10, true);
    cat_ngon->setExpanded(true);
    cat_ngon->setFont(0, bold_font);
    tree_->insertTopLevelItem(1, cat_ngon);

    add_stencil_item(cat_ngon, "Triangle (3-gon)", StencilItemType::ShapeTriangle, 3,
                     IconFactory::IconType::Triangle, "Regular 3-sided polygon (Equilateral Triangle)", true);
    add_stencil_item(cat_ngon, "Diamond (4-gon)", StencilItemType::ShapeDiamond, 4,
                     IconFactory::IconType::Diamond, "Regular 4-sided polygon (Square / Rhombus)", true);
    add_stencil_item(cat_ngon, "Pentagon (5-gon)", StencilItemType::ShapePentagon, 5,
                     IconFactory::IconType::Pentagon, "Regular 5-sided polygon", true);
    add_stencil_item(cat_ngon, "Hexagon (6-gon)", StencilItemType::ShapeHexagon, 6,
                     IconFactory::IconType::PolygonHex, "Regular 6-sided polygon (Honeycombs, pads)", true);
    add_stencil_item(cat_ngon, "Heptagon (7-gon)", StencilItemType::ShapeHeptagon, 7,
                     IconFactory::IconType::Heptagon, "Regular 7-sided polygon", true);
    add_stencil_item(cat_ngon, "Octagon (8-gon)", StencilItemType::ShapeOctagon, 8,
                     IconFactory::IconType::Octagon, "Regular 8-sided polygon", true);
    add_stencil_item(cat_ngon, "Decagon (10-gon)", StencilItemType::ShapeDecagon, 10,
                     IconFactory::IconType::Decagon, "Regular 10-sided polygon", true);
    add_stencil_item(cat_ngon, "Dodecagon (12-gon)", StencilItemType::ShapeDodecagon, 12,
                     IconFactory::IconType::Dodecagon, "Regular 12-sided polygon", true);

    // 3. Hierarchy & Ports Category
    auto* cat_ports = new QTreeWidgetItem(QStringList() << "⚙ Hierarchy & EDA Ports");
    cat_ports->setData(0, Qt::UserRole + 10, true);
    cat_ports->setExpanded(true);
    cat_ports->setFont(0, bold_font);
    tree_->insertTopLevelItem(2, cat_ports);

    add_stencil_item(cat_ports, "Hierarchical Input Port", StencilItemType::ShapeInPort, 0,
                     IconFactory::IconType::InputPort, "Hierarchical input signal boundary port (I)", true);
    add_stencil_item(cat_ports, "Hierarchical Output Port", StencilItemType::ShapeOutPort, 0,
                     IconFactory::IconType::OutputPort, "Hierarchical output signal boundary port (O)", true);
    add_stencil_item(cat_ports, "Bidirectional Port", StencilItemType::ShapeBiPort, 0,
                     IconFactory::IconType::BidirectionalPort, "Bidirectional I/O interface port", true);
    add_stencil_item(cat_ports, "Built-in Subsystem (Embedded)", StencilItemType::SubsystemBuiltin, 0,
                     IconFactory::IconType::SubsystemBlock, "Hierarchical subsystem block embedded in current diagram (H)", true);
    add_stencil_item(cat_ports, "External File Subsystem (.yaml)", StencilItemType::SubsystemFile, 0,
                     IconFactory::IconType::SubsystemFile, "Hierarchical subsystem linked to an external file", true);

    // 4. Connectors & Routing Category
    auto* cat_wiring = new QTreeWidgetItem(QStringList() << "⚡ Connectors & Routing");
    cat_wiring->setData(0, Qt::UserRole + 10, true);
    cat_wiring->setExpanded(true);
    cat_wiring->setFont(0, bold_font);
    tree_->insertTopLevelItem(3, cat_wiring);

    add_stencil_item(cat_wiring, "Orthogonal Net Connector", StencilItemType::ToolConnector, 0,
                     IconFactory::IconType::Wire, "Orthogonal obstacle-avoiding net wire (C)", true);
    add_stencil_item(cat_wiring, "Multi-Point Arrow Connector", StencilItemType::ToolAutoArrow, 0,
                     IconFactory::IconType::MultiPointArrow, "Multi-point polyline arrow connector with waypoints (Shift+W)", true);
  }

  QTreeWidgetItem* add_stencil_item(
      QTreeWidgetItem* parent,
      const QString& title,
      StencilItemType type,
      int param,
      IconFactory::IconType itype,
      const QString& tooltip = QString(),
      bool is_builtin = true) {
    auto* item = new QTreeWidgetItem(parent, QStringList() << title);
    item->setData(0, Qt::UserRole, static_cast<int>(type));
    item->setData(0, Qt::UserRole + 1, param);
    item->setData(0, Qt::UserRole + 3, static_cast<int>(itype));
    item->setData(0, Qt::UserRole + 10, is_builtin);
    item->setIcon(0, IconFactory::create_icon(itype, current_fg_));
    if (!tooltip.isEmpty()) {
      item->setToolTip(0, tooltip);
    }
    return item;
  }

public:
  static renderer::ShapePlacementTemplate template_from_stencil_data(
      int type_int,
      int param = 0,
      const QString& custom_name = QString()) {
    auto type = static_cast<StencilItemType>(type_int);
    renderer::ShapePlacementTemplate tmpl;

    switch (type) {
      case StencilItemType::ShapeRect:
        tmpl.category = renderer::PlacementCategory::ShapeType;
        tmpl.shape_type = core::ShapeType::Rectangle;
        tmpl.name = "Rectangle";
        tmpl.width = 120.0;
        tmpl.height = 70.0;
        break;
      case StencilItemType::ShapeRoundedRect:
        tmpl.category = renderer::PlacementCategory::ShapeType;
        tmpl.shape_type = core::ShapeType::RoundedRect;
        tmpl.name = "Rounded Rectangle";
        tmpl.width = 120.0;
        tmpl.height = 70.0;
        break;
      case StencilItemType::ShapeEllipse:
        tmpl.category = renderer::PlacementCategory::ShapeType;
        tmpl.shape_type = core::ShapeType::Ellipse;
        tmpl.name = "Ellipse";
        tmpl.width = 120.0;
        tmpl.height = 70.0;
        break;
      case StencilItemType::ShapeRightTriangle:
        tmpl.category = renderer::PlacementCategory::CustomPolygon;
        tmpl.custom_vertices = {{0.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
        tmpl.name = "Right Triangle";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeTriangle:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 3;
        tmpl.name = "Triangle";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeDiamond:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 4;
        tmpl.name = "Diamond";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapePentagon:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 5;
        tmpl.name = "Pentagon";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeHexagon:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 6;
        tmpl.name = "Hexagon";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeHeptagon:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 7;
        tmpl.name = "Heptagon";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeOctagon:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 8;
        tmpl.name = "Octagon";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeDecagon:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 10;
        tmpl.name = "Decagon";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeDodecagon:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = 12;
        tmpl.name = "Dodecagon";
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapePolygonN:
        tmpl.category = renderer::PlacementCategory::RegularPolygon;
        tmpl.polygon_sides = (param > 2) ? param : 5;
        tmpl.name = QString("Polygon %1-gon").arg(tmpl.polygon_sides).toStdString();
        tmpl.width = 120.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeParallelogram:
        tmpl.category = renderer::PlacementCategory::CustomPolygon;
        tmpl.custom_vertices = {{0.25, 0.0}, {1.0, 0.0}, {0.75, 1.0}, {0.0, 1.0}};
        tmpl.name = "Parallelogram";
        tmpl.width = 130.0;
        tmpl.height = 80.0;
        break;
      case StencilItemType::ShapeTrapezoid:
        tmpl.category = renderer::PlacementCategory::CustomPolygon;
        tmpl.custom_vertices = {{0.2, 0.0}, {0.8, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
        tmpl.name = "Trapezoid";
        tmpl.width = 130.0;
        tmpl.height = 80.0;
        break;
      case StencilItemType::ShapeCross:
        tmpl.category = renderer::PlacementCategory::CustomPolygon;
        tmpl.custom_vertices = {
          {0.35, 0.0}, {0.65, 0.0}, {0.65, 0.35},
          {1.0, 0.35}, {1.0, 0.65}, {0.65, 0.65},
          {0.65, 1.0}, {0.35, 1.0}, {0.35, 0.65},
          {0.0, 0.65}, {0.0, 0.35}, {0.35, 0.35}
        };
        tmpl.name = "Cross";
        tmpl.width = 100.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::ShapeStar: {
        tmpl.category = renderer::PlacementCategory::CustomPolygon;
        tmpl.name = "Star";
        tmpl.width = 110.0;
        tmpl.height = 110.0;
        tmpl.custom_vertices.reserve(10);
        for (int i = 0; i < 10; ++i) {
          const double r = (i % 2 == 0) ? 0.5 : 0.22;
          const double rad = -std::numbers::pi / 2.0 + (std::numbers::pi * i) / 5.0;
          tmpl.custom_vertices.push_back({0.5 + r * std::cos(rad), 0.5 + r * std::sin(rad)});
        }
        break;
      }
      case StencilItemType::ShapeChevron:
        tmpl.category = renderer::PlacementCategory::CustomPolygon;
        tmpl.custom_vertices = {
          {0.0, 0.2}, {0.6, 0.2}, {0.6, 0.0},
          {1.0, 0.5}, {0.6, 1.0}, {0.6, 0.8}, {0.0, 0.8}
        };
        tmpl.name = "Arrow Block";
        tmpl.width = 130.0;
        tmpl.height = 80.0;
        break;
      case StencilItemType::ShapeCylinder: {
        tmpl.category = renderer::PlacementCategory::CustomPolygon;
        tmpl.name = "Cylinder";
        tmpl.width = 100.0;
        tmpl.height = 120.0;
        tmpl.custom_vertices.push_back({0.0, 0.2});
        for (int i = 0; i <= 8; ++i) {
          const double rad = std::numbers::pi - (std::numbers::pi * i) / 8.0;
          tmpl.custom_vertices.push_back({0.5 + 0.5 * std::cos(rad), 0.2 - 0.15 * std::sin(rad)});
        }
        tmpl.custom_vertices.push_back({1.0, 0.8});
        for (int i = 0; i <= 8; ++i) {
          const double rad = -(std::numbers::pi * i) / 8.0;
          tmpl.custom_vertices.push_back({0.5 + 0.5 * std::cos(rad), 0.8 - 0.15 * std::sin(rad)});
        }
        break;
      }
      case StencilItemType::ShapeInPort:
        tmpl.category = renderer::PlacementCategory::HierarchicalPort;
        tmpl.is_input_port = true;
        tmpl.port_type = "in";
        tmpl.name = "in_port";
        tmpl.width = 84.0;
        tmpl.height = 28.0;
        break;
      case StencilItemType::ShapeOutPort:
        tmpl.category = renderer::PlacementCategory::HierarchicalPort;
        tmpl.is_input_port = false;
        tmpl.port_type = "out";
        tmpl.name = "out_port";
        tmpl.width = 84.0;
        tmpl.height = 28.0;
        break;
      case StencilItemType::ShapeBiPort:
        tmpl.category = renderer::PlacementCategory::HierarchicalPort;
        tmpl.shape_type = core::ShapeType::BidirectionalPort;
        tmpl.is_input_port = false;
        tmpl.port_type = "inout";
        tmpl.name = "inout_port";
        tmpl.width = 84.0;
        tmpl.height = 28.0;
        break;
      case StencilItemType::SubsystemBuiltin:
        tmpl.category = renderer::PlacementCategory::SubsystemBuiltin;
        tmpl.name = "Subsystem";
        tmpl.width = 180.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::SubsystemFile:
        tmpl.category = renderer::PlacementCategory::SubsystemFile;
        tmpl.name = "File Subsystem";
        tmpl.width = 180.0;
        tmpl.height = 100.0;
        break;
      case StencilItemType::CustomShape:
        if (param > 2) {
          tmpl.category = renderer::PlacementCategory::RegularPolygon;
          tmpl.polygon_sides = param;
          tmpl.name = custom_name.isEmpty() ? "Custom Polygon" : custom_name.toStdString();
          tmpl.width = 120.0;
          tmpl.height = 100.0;
        } else {
          tmpl.category = renderer::PlacementCategory::ShapeType;
          tmpl.shape_type = core::ShapeType::Rectangle;
          tmpl.name = custom_name.isEmpty() ? "Custom Shape" : custom_name.toStdString();
          tmpl.width = 120.0;
          tmpl.height = 70.0;
        }
        break;
      default:
        break;
    }
    return tmpl;
  }

  void trigger_item(QTreeWidgetItem* item) {
    if (!canvas_ || !item) return;

    if (item->data(0, Qt::UserRole + 6).isValid()) {
      const QByteArray bytes = item->data(0, Qt::UserRole + 6).toByteArray();
      renderer::ShapePlacementTemplate tmpl;
      if (renderer::deserialize_placement_template(bytes, tmpl)) {
        canvas_->start_shape_placement(tmpl);
        return;
      }
    }

    QVariant val = item->data(0, Qt::UserRole);
    if (!val.isValid()) return;

    auto type = static_cast<StencilItemType>(val.toInt());
    int param = item->data(0, Qt::UserRole + 1).toInt();
    QString custom_name = item->data(0, Qt::UserRole + 2).toString();

    // Tools that activate drawing modes
    if (type == StencilItemType::ShapePolygonTool) {
      canvas_->set_tool_mode(renderer::ToolMode::AddPolygon);
      return;
    }
    if (type == StencilItemType::ToolConnector) {
      canvas_->set_tool_mode(renderer::ToolMode::AddConnector);
      return;
    }
    if (type == StencilItemType::ToolAutoArrow) {
      canvas_->set_tool_mode(renderer::ToolMode::AddMultiPointArrow);
      return;
    }

    // Subsystem from external file can prompt for file path before attaching to cursor
    if (type == StencilItemType::SubsystemFile) {
      const QString fpath = QFileDialog::getOpenFileName(
          this, "Select or Create Subsystem File", "",
          "Supported Formats (*.yaml *.drawio *.xml);;All Files (*)");
      if (!fpath.isEmpty()) {
        QFileInfo fi(fpath);
        renderer::ShapePlacementTemplate tmpl = template_from_stencil_data(static_cast<int>(type), param, custom_name);
        tmpl.name = fi.baseName().toStdString();
        tmpl.file_path = fpath.toStdString();
        canvas_->start_shape_placement(tmpl);
      }
      return;
    }

    // Unified pointer placement: attach any shape candidate to mouse pointer
    renderer::ShapePlacementTemplate tmpl = template_from_stencil_data(static_cast<int>(type), param, custom_name);
    if (item->data(0, Qt::UserRole + 4).isValid()) {
      const QPointF pt = item->data(0, Qt::UserRole + 4).toPointF();
      tmpl.anchor_point = {pt.x(), pt.y()};
    }
    if (item->data(0, Qt::UserRole + 5).isValid()) {
      const QPointF pt = item->data(0, Qt::UserRole + 5).toPointF();
      tmpl.resize_point = {pt.x(), pt.y()};
    }
    canvas_->start_shape_placement(tmpl);
  }

private:
  void filter_items(const QString& filter_text) {
    const QString text = filter_text.trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
      auto* cat = tree_->topLevelItem(i);
      bool cat_visible = false;
      for (int j = 0; j < cat->childCount(); ++j) {
        auto* child = cat->child(j);
        const bool match = text.isEmpty() || child->text(0).contains(text, Qt::CaseInsensitive);
        child->setHidden(!match);
        if (match) cat_visible = true;
      }
      cat->setHidden(!cat_visible);
      if (!text.isEmpty() && cat_visible) cat->setExpanded(true);
    }
  }

  void show_context_menu(const QPoint& pt) {
    auto* item = tree_->itemAt(pt);
    QMenu menu(this);
    if (item && item->parent() == nullptr) {
      if (is_category_builtin(item)) {
        auto* act_prot = menu.addAction("🔒 Built-in Section (Protected)");
        act_prot->setEnabled(false);
      } else {
        menu.addAction("✏ Rename Section...", this, [this, item]() { prompt_rename_section(item); });
        menu.addAction("🗑 Delete Section", this, &StencilLibraryDock::remove_current_selected);
      }
      menu.addSeparator();
    } else if (item && item->parent() != nullptr) {
      menu.addAction("➕ Insert onto Canvas", this, [this, item]() { trigger_item(item); });
      if (is_item_builtin(item)) {
        auto* act_prot = menu.addAction("🔒 Built-in Shape (Protected)");
        act_prot->setEnabled(false);
      } else {
        menu.addAction("🗑 Remove Shape", this, &StencilLibraryDock::remove_current_selected);
      }
      menu.addSeparator();
    }
    menu.addAction("📁 New Section...", this, &StencilLibraryDock::prompt_new_section_dialog);
    menu.addAction("📐 Add Custom Shape / N-gon...", this, &StencilLibraryDock::prompt_custom_shape_dialog);
    auto* act_sel = menu.addAction("⭐ Add Selected from Canvas", this, &StencilLibraryDock::add_from_selection);
    if (!canvas_ || canvas_->selected_count() == 0) {
      act_sel->setEnabled(false);
    }
    menu.addSeparator();
    menu.addAction("🔄 Restore Default Shapes", this, &StencilLibraryDock::reset_default_shapes);
    menu.exec(tree_->mapToGlobal(pt));
  }

  QLineEdit* search_edit_{nullptr};
  StencilTreeWidget* tree_{nullptr};
  QPushButton* btn_add_{nullptr};
  QPushButton* btn_remove_{nullptr};
  QPushButton* btn_insert_{nullptr};
  renderer::CanvasWidget* canvas_{nullptr};
  QColor current_fg_{QColor("#CDD6F4")};
};

inline void StencilTreeWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!(event->buttons() & Qt::LeftButton)) {
    QTreeWidget::mouseMoveEvent(event);
    return;
  }
  if ((event->pos() - drag_start_pos_).manhattanLength() < QApplication::startDragDistance()) {
    QTreeWidget::mouseMoveEvent(event);
    return;
  }

  auto* item = itemAt(drag_start_pos_);
  if (!item || item->parent() == nullptr) {
    QTreeWidget::mouseMoveEvent(event);
    return;
  }

  const int type = item->data(0, Qt::UserRole).toInt();
  if (type == static_cast<int>(StencilLibraryDock::StencilItemType::ShapePolygonTool) ||
      type == static_cast<int>(StencilLibraryDock::StencilItemType::ToolConnector) ||
      type == static_cast<int>(StencilLibraryDock::StencilItemType::ToolAutoArrow)) {
    QTreeWidget::mouseMoveEvent(event);
    return;
  }

  const QString title = item->text(0);
  QByteArray item_data;

  if (item->data(0, Qt::UserRole + 6).isValid()) {
    item_data = item->data(0, Qt::UserRole + 6).toByteArray();
  } else {
    const int param = item->data(0, Qt::UserRole + 1).toInt();
    const QString custom_name = item->data(0, Qt::UserRole + 2).toString();

    renderer::ShapePlacementTemplate tmpl = StencilLibraryDock::template_from_stencil_data(type, param, custom_name);
    if (tmpl.name.empty() && !title.isEmpty()) {
      tmpl.name = title.toStdString();
    }
    if (item->data(0, Qt::UserRole + 4).isValid()) {
      const QPointF pt = item->data(0, Qt::UserRole + 4).toPointF();
      tmpl.anchor_point = {pt.x(), pt.y()};
    }
    if (item->data(0, Qt::UserRole + 5).isValid()) {
      const QPointF pt = item->data(0, Qt::UserRole + 5).toPointF();
      tmpl.resize_point = {pt.x(), pt.y()};
    }
    item_data = renderer::serialize_placement_template(tmpl);
  }

  auto* mime = new QMimeData();
  mime->setData("application/x-vector-stencil", item_data);
  mime->setData("application/x-vector-diagram-shape", item_data);
  mime->setText(title);

  auto* drag = new QDrag(this);
  drag->setMimeData(mime);

  QPixmap pix = item->icon(0).pixmap(32, 32);
  if (!pix.isNull()) {
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
  }
  drag->exec(Qt::CopyAction | Qt::MoveAction);
}

inline void StencilTreeWidget::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasFormat("application/x-vector-stencil") ||
      event->mimeData()->hasFormat("application/x-vector-diagram-shape") ||
      event->mimeData()->hasText()) {
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}

inline void StencilTreeWidget::dragMoveEvent(QDragMoveEvent* event) {
  if (event->mimeData()->hasFormat("application/x-vector-stencil") ||
      event->mimeData()->hasFormat("application/x-vector-diagram-shape") ||
      event->mimeData()->hasText()) {
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}

inline void StencilTreeWidget::dropEvent(QDropEvent* event) {
  if (event->mimeData()->hasFormat("application/x-vector-stencil") ||
      event->mimeData()->hasFormat("application/x-vector-diagram-shape")) {
    const QByteArray bytes = event->mimeData()->hasFormat("application/x-vector-stencil")
        ? event->mimeData()->data("application/x-vector-stencil")
        : event->mimeData()->data("application/x-vector-diagram-shape");
    renderer::ShapePlacementTemplate tmpl;
    if (renderer::deserialize_placement_template(bytes, tmpl)) {
      auto* target = itemAt(event->position().toPoint());
      if (dock_) {
        dock_->handle_dropped_template(tmpl, target);
      }
      event->acceptProposedAction();
      return;
    }
  }
  QTreeWidget::dropEvent(event);
}

} // namespace vector::gui

#endif // VECTOR_ENABLE_QT
#endif // VECTOR_GUI_STENCIL_LIBRARY_DOCK_HPP
