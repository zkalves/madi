// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_GUI_HIERARCHY_TREE_DOCK_HPP
#define MADI_GUI_HIERARCHY_TREE_DOCK_HPP

#ifdef MADI_ENABLE_QT

#include <QDockWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>

#include "madi/core/GraphModel.hpp"
#include "madi/renderer/CanvasWidget.hpp"

namespace madi::gui {

class HierarchyTreeDock : public QDockWidget {
  Q_OBJECT

public:
  explicit HierarchyTreeDock(QWidget* parent = nullptr)
      : QDockWidget("Hierarchy Explorer", parent) {
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    // Header Navigation Toolbar
    auto* nav_layout = new QHBoxLayout();
    btn_down_ = new QPushButton("⬇ Drill Down", container);
    btn_up_ = new QPushButton("⬆ Up", container);
    btn_refresh_ = new QPushButton("🔄", container);
    btn_refresh_->setToolTip("Refresh hierarchy tree");

    nav_layout->addWidget(btn_down_);
    nav_layout->addWidget(btn_up_);
    nav_layout->addWidget(btn_refresh_);
    layout->addLayout(nav_layout);

    // Tree View (Element Table)
    tree_ = new QTreeWidget(container);
    tree_->setObjectName("elementTable");
    tree_->setHeaderLabels(QStringList() << "Element" << "Kind");
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(tree_, 1);

    setWidget(container);

    connect(btn_down_, &QPushButton::clicked, this, &HierarchyTreeDock::on_drill_down);
    connect(btn_up_, &QPushButton::clicked, this, &HierarchyTreeDock::on_pop_scope);
    connect(btn_refresh_, &QPushButton::clicked, this, &HierarchyTreeDock::refresh);

    connect(tree_, &QTreeWidget::itemClicked, this, &HierarchyTreeDock::on_item_clicked);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, &HierarchyTreeDock::on_item_double_clicked);
  }

  void set_canvas(renderer::CanvasWidget* canvas) noexcept {
    canvas_ = canvas;
  }

  void set_graph_model(const core::GraphModel* graph) noexcept {
    graph_model_ = graph;
    refresh();
  }

  void set_scope_info(const QString& breadcrumb, bool can_pop) {
    current_scope_path_ = breadcrumb;
    btn_up_->setEnabled(can_pop);
    refresh();
  }

public slots:
  void refresh() {
    tree_->clear();
    if (!graph_model_) return;

    auto* root_item = new QTreeWidgetItem(tree_, QStringList()
        << (current_scope_path_.isEmpty() ? "📍 Top Level Diagram" : current_scope_path_)
        << QString("%1 nodes").arg(graph_model_->node_count()));
    root_item->setExpanded(true);
    QFont bold_font = root_item->font(0);
    bold_font.setBold(true);
    root_item->setFont(0, bold_font);

    // Categorize nodes
    for (const auto& [nid, node] : graph_model_->nodes()) {
      QString kind_str = "Rectangle";
      QString icon_prefix = "▭ ";

      if (node.is_subsystem()) {
        kind_str = node.is_file_subsystem() ? "File Subsystem" : "Built-in Subsystem";
        icon_prefix = node.is_file_subsystem() ? "📁 " : "⚙ ";
      } else if (node.shape == core::ShapeType::RoundedRect) {
        kind_str = "Rounded Rect";
        icon_prefix = "▢ ";
      } else if (node.shape == core::ShapeType::Ellipse) {
        kind_str = "Ellipse";
        icon_prefix = "⬭ ";
      } else if (node.shape == core::ShapeType::CustomPolygon) {
        kind_str = QString("%1-gon").arg(node.get_world_polygon_vertices().size());
        icon_prefix = "⬡ ";
      }

      auto* item = new QTreeWidgetItem(root_item, QStringList()
          << (icon_prefix + QString::fromStdString(node.name))
          << kind_str);
      item->setData(0, Qt::UserRole, static_cast<qulonglong>(nid));
      if (node.selected) {
        item->setSelected(true);
      }
    }

    // Connectors / Edges
    if (graph_model_->edge_count() > 0) {
      auto* wires_cat = new QTreeWidgetItem(root_item, QStringList()
          << QString("⚡ Wires & Nets (%1)").arg(graph_model_->edge_count())
          << "Connections");
      wires_cat->setExpanded(true);

      for (const auto& [eid, edge] : graph_model_->edges()) {
        const auto* src = graph_model_->find_node(edge.source_node);
        const auto* tgt = graph_model_->find_node(edge.target_node);
        QString label = QString::fromStdString(edge.name);
        if (src && tgt) {
          label += QString(" (%1 ➔ %2)").arg(QString::fromStdString(src->name), QString::fromStdString(tgt->name));
        }
        auto* edge_item = new QTreeWidgetItem(wires_cat, QStringList() << label << "Orthogonal Net");
        edge_item->setData(0, Qt::UserRole, static_cast<qulonglong>(eid));
      }
    }

    update_button_states();
  }

  void update_button_states() {
    if (canvas_) {
      btn_down_->setEnabled(canvas_->can_drill_down());
      btn_up_->setEnabled(canvas_->can_pop_scope());
    }
  }

private slots:
  void on_drill_down() {
    if (canvas_) {
      canvas_->drill_down_selected();
      refresh();
    }
  }

  void on_pop_scope() {
    if (canvas_) {
      canvas_->pop_hierarchy_scope();
      refresh();
    }
  }

  void on_item_clicked(QTreeWidgetItem* item, int col) {
    (void)col;
    if (!canvas_ || !item) return;
    QVariant val = item->data(0, Qt::UserRole);
    if (!val.isValid()) return;
    core::NodeId nid = val.toULongLong();
    canvas_->select_node(nid, true);
    update_button_states();
  }

  void on_item_double_clicked(QTreeWidgetItem* item, int col) {
    (void)col;
    if (!canvas_ || !item) return;
    QVariant val = item->data(0, Qt::UserRole);
    if (!val.isValid()) return;
    core::NodeId nid = val.toULongLong();
    canvas_->drill_down_node(nid);
    refresh();
  }

private:
  QPushButton* btn_down_{nullptr};
  QPushButton* btn_up_{nullptr};
  QPushButton* btn_refresh_{nullptr};
  QTreeWidget* tree_{nullptr};
  renderer::CanvasWidget* canvas_{nullptr};
  const core::GraphModel* graph_model_{nullptr};
  QString current_scope_path_{"Top"};
};

} // namespace madi::gui

#endif // MADI_ENABLE_QT
#endif // MADI_GUI_HIERARCHY_TREE_DOCK_HPP
