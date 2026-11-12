// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_GUI_TOOL_LIBRARY_DOCK_HPP
#define MADI_GUI_TOOL_LIBRARY_DOCK_HPP

#ifdef MADI_ENABLE_QT

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "madi/core/Command.hpp"
#include "madi/core/GraphModel.hpp"
#include "madi/gui/IconFactory.hpp"
#include "madi/io/DrawioParser.hpp"
#include "madi/io/StorageEngine.hpp"
#include "madi/renderer/CanvasWidget.hpp"

namespace madi::gui {

class ToolLibraryDock;

// ==============================================================================
// ToolItem: Represents an individual tool or action within a category
// ==============================================================================

struct ToolItem {
  QString id;
  QString name;
  QString display_text;
  QString tooltip;
  std::optional<IconFactory::IconType> icon_type{std::nullopt};
  QToolButton* button{nullptr};
  QTreeWidgetItem* tree_item{nullptr};
  std::function<void()> on_triggered;
  bool is_builtin{true};
};

// ==============================================================================
// ToolCategoryHeader: Collapsible accordion header with non-editable indicator
// ==============================================================================

class ToolCategoryHeader : public QWidget {
public:
  ToolCategoryHeader(const QString& title, bool is_builtin, QWidget* container_widget, ToolLibraryDock* dock, QWidget* parent = nullptr);

  void set_title(const QString& title, bool is_builtin) {
    is_builtin_ = is_builtin;
    btn_toggle_->setText(title + (is_builtin ? " 🔒" : ""));
  }

  [[nodiscard]] bool is_expanded() const noexcept {
    return btn_toggle_->isChecked();
  }

  void set_expanded(bool exp) {
    btn_toggle_->setChecked(exp);
  }

  [[nodiscard]] bool is_builtin() const noexcept { return is_builtin_; }
  [[nodiscard]] QString title() const noexcept { return title_; }

protected:
  void contextMenuEvent(QContextMenuEvent* event) override;

private:
  QString title_;
  QToolButton* btn_toggle_{nullptr};
  QWidget* container_widget_{nullptr};
  ToolLibraryDock* dock_{nullptr};
  bool is_builtin_{false};
};

// ==============================================================================
// ToolCategorySection: Holds the header, responsive grid container, and items
// ==============================================================================

class ToolCategorySection : public QObject {
public:
  ToolCategorySection(const QString& title, bool is_builtin, ToolLibraryDock* dock, QWidget* parent_widget);

  [[nodiscard]] QString title() const noexcept { return title_; }
  [[nodiscard]] bool is_builtin() const noexcept { return is_builtin_; }
  [[nodiscard]] ToolCategoryHeader* header() const noexcept { return header_; }
  [[nodiscard]] QWidget* container() const noexcept { return container_; }
  [[nodiscard]] QGridLayout* grid_layout() const noexcept { return grid_layout_; }
  [[nodiscard]] const std::vector<std::shared_ptr<ToolItem>>& items() const noexcept { return items_; }

  void set_title(const QString& title) {
    title_ = title;
    if (header_) header_->set_title(title, is_builtin_);
  }

  void add_item(const std::shared_ptr<ToolItem>& item) {
    items_.push_back(item);
  }

  void remove_item(const QString& id) {
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const auto& it) {
      return it->id == id;
    }), items_.end());
  }

  void reflow(int available_width) {
    if (!grid_layout_ || !container_) return;
    const int btn_w = 76;
    const int cols = std::max(1, (available_width - 16) / (btn_w + 4));

    while (grid_layout_->count() > 0) {
      grid_layout_->takeAt(0);
    }

    int visible_idx = 0;
    for (const auto& it : items_) {
      if (it && it->button && it->button->isVisible()) {
        const int r = visible_idx / cols;
        const int c = visible_idx % cols;
        grid_layout_->addWidget(it->button, r, c, Qt::AlignLeft | Qt::AlignTop);
        visible_idx++;
      }
    }
    grid_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  }

private:
  QString title_;
  bool is_builtin_{false};
  ToolCategoryHeader* header_{nullptr};
  QWidget* container_{nullptr};
  QGridLayout* grid_layout_{nullptr};
  std::vector<std::shared_ptr<ToolItem>> items_;
};

// ==============================================================================
// ToolLibraryDock: Responsive left dock for tools with parity to StencilLibraryDock
// ==============================================================================

class ToolLibraryDock : public QDockWidget {
  Q_OBJECT

public:
  enum class ViewMode {
    Columns, // Responsive flow grid (columns depend on available width)
    Tree     // Hierarchical tree view
  };

  explicit ToolLibraryDock(QWidget* parent = nullptr)
      : QDockWidget("Tools", parent) {
    setObjectName("DockTools");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* root_container = new QWidget(this);
    auto* root_layout = new QVBoxLayout(root_container);
    root_layout->setContentsMargins(4, 4, 4, 4);
    root_layout->setSpacing(4);

    // Top Row: Search bar & View Mode Switcher
    auto* top_row = new QHBoxLayout();
    top_row->setContentsMargins(0, 0, 0, 0);
    top_row->setSpacing(3);

    search_edit_ = new QLineEdit(root_container);
    search_edit_->setPlaceholderText("🔍 Search tools & operations...");
    search_edit_->setClearButtonEnabled(true);
    search_edit_->setFixedHeight(24);
    search_edit_->setStyleSheet("QLineEdit { padding: 2px 6px; font-size: 11px; }");
    connect(search_edit_, &QLineEdit::textChanged, this, &ToolLibraryDock::filter_items);
    top_row->addWidget(search_edit_, 1);

    btn_view_mode_ = new QToolButton(root_container);
    btn_view_mode_->setFixedHeight(24);
    btn_view_mode_->setText("▦ Columns");
    btn_view_mode_->setToolTip("Toggle between Responsive Columns Grid and Hierarchical Tree");
    btn_view_mode_->setStyleSheet("QToolButton { font-size: 10px; font-weight: bold; padding: 2px 5px; }");
    connect(btn_view_mode_, &QToolButton::clicked, this, &ToolLibraryDock::toggle_view_mode);
    top_row->addWidget(btn_view_mode_);
    root_layout->addLayout(top_row);

    const int two_lines = compute_two_lines_height();

    // Stacked central view: Index 0 = Responsive Columns View, Index 1 = Tree View
    view_stack_ = new QStackedWidget(root_container);

    // 1. Columns View (Accordion scroll area with responsive grid lists)
    scroll_columns_ = new QScrollArea(view_stack_);
    scroll_columns_->setWidgetResizable(true);
    scroll_columns_->setFrameShape(QFrame::NoFrame);
    scroll_columns_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    widget_columns_ = new QWidget(scroll_columns_);
    layout_columns_ = new QVBoxLayout(widget_columns_);
    layout_columns_->setContentsMargins(2, 2, 2, 2);
    layout_columns_->setSpacing(4);
    layout_columns_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scroll_columns_->setWidget(widget_columns_);
    view_stack_->addWidget(scroll_columns_);

    // 2. Tree View (Tree of tools with word wrap and two-lines icon size)
    tree_ = new QTreeWidget(view_stack_);
    tree_->setHeaderHidden(true);
    tree_->setIndentation(12);
    tree_->setAnimated(true);
    tree_->setWordWrap(true);
    tree_->setIconSize(QSize(two_lines, two_lines));
    tree_->setStyleSheet(
        "QTreeWidget { font-size: 11px; } "
        "QTreeWidget::item { padding: 3px 4px; min-height: 38px; } "
        "QTreeWidget::item:selected { background-color: #0078D4; color: #FFFFFF; }");
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ToolLibraryDock::show_tree_context_menu);
    view_stack_->addWidget(tree_);

    // Default to Columns Mode
    view_stack_->setCurrentIndex(0);
    root_layout->addWidget(view_stack_, 1);

    // Bottom Action Rows:
    // Row 1: [ ➕ Add... ] [ 📁 + Category ] [ 📦 Load Package... ]
    auto* action_row1 = new QHBoxLayout();
    action_row1->setContentsMargins(0, 0, 0, 0);
    action_row1->setSpacing(3);

    btn_add_ = new QPushButton("➕ Add...", root_container);
    btn_add_->setFixedHeight(24);
    btn_add_->setToolTip("Add custom tool, macro command, or custom operation");
    btn_add_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    connect(btn_add_, &QPushButton::clicked, this, &ToolLibraryDock::prompt_add_tool_dialog);
    action_row1->addWidget(btn_add_);

    btn_add_category_ = new QPushButton("📁 + Category", root_container);
    btn_add_category_->setFixedHeight(24);
    btn_add_category_->setToolTip("Add a new custom category for tools and operations");
    btn_add_category_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    connect(btn_add_category_, &QPushButton::clicked, this, &ToolLibraryDock::prompt_new_section_dialog);
    action_row1->addWidget(btn_add_category_);

    btn_load_package_ = new QPushButton("📦 Load Package...", root_container);
    btn_load_package_->setFixedHeight(24);
    btn_load_package_->setToolTip("Load a package (.vlib, .vdiag, .madi, .json, .drawio, .xml) with more tool categories");
    btn_load_package_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    connect(btn_load_package_, &QPushButton::clicked, this, &ToolLibraryDock::prompt_load_package_dialog);
    action_row1->addWidget(btn_load_package_);
    root_layout->addLayout(action_row1);

    // Row 2: [ 🗑 Remove ] [ Execute ]
    auto* action_row2 = new QHBoxLayout();
    action_row2->setContentsMargins(0, 0, 0, 0);
    action_row2->setSpacing(3);

    btn_remove_ = new QPushButton("🗑 Remove", root_container);
    btn_remove_->setFixedHeight(24);
    btn_remove_->setEnabled(false);
    btn_remove_->setToolTip("Remove selected custom tool or custom section (Delete key)");
    btn_remove_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 6px; }");
    connect(btn_remove_, &QPushButton::clicked, this, &ToolLibraryDock::remove_current_selected);
    action_row2->addWidget(btn_remove_);

    btn_execute_ = new QPushButton("Execute", root_container);
    btn_execute_->setFixedHeight(24);
    btn_execute_->setToolTip("Execute the currently selected tool or action");
    btn_execute_->setStyleSheet("QPushButton { font-size: 11px; font-weight: bold; padding: 2px 8px; }");
    connect(btn_execute_, &QPushButton::clicked, this, &ToolLibraryDock::execute_current_selected);
    action_row2->addWidget(btn_execute_);
    root_layout->addLayout(action_row2);

    setWidget(root_container);
    setMinimumWidth(180);

    populate_builtin_tools();

    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int col) {
      (void)col;
      trigger_tree_item(item);
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

    auto* del_shortcut = new QShortcut(QKeySequence::Delete, this);
    connect(del_shortcut, &QShortcut::activated, this, &ToolLibraryDock::remove_current_selected);
  }

  [[nodiscard]] int compute_two_lines_height() const noexcept {
    const int line_spacing = fontMetrics().lineSpacing();
    return std::max(32, line_spacing * 2);
  }

  void set_canvas(renderer::CanvasWidget* canvas) noexcept {
    canvas_ = canvas;
  }

  void set_command_history(core::CommandHistory* history) noexcept {
    history_ = history;
  }

  [[nodiscard]] ViewMode view_mode() const noexcept { return view_mode_; }
  [[nodiscard]] QToolButton* view_mode_button() const noexcept { return btn_view_mode_; }
  [[nodiscard]] QTreeWidget* tree_widget() const noexcept { return tree_; }
  [[nodiscard]] QLineEdit* search_edit() const noexcept { return search_edit_; }

  void toggle_view_mode() {
    if (view_mode_ == ViewMode::Columns) {
      view_mode_ = ViewMode::Tree;
      view_stack_->setCurrentIndex(1);
      btn_view_mode_->setText("☰ Tree");
    } else {
      view_mode_ = ViewMode::Columns;
      view_stack_->setCurrentIndex(0);
      btn_view_mode_->setText("▦ Columns");
      reflow_columns();
    }
  }

  void reflow_columns() {
    const int avail_w = scroll_columns_->viewport() ?
        std::max(60, scroll_columns_->viewport()->width()) :
        std::max(60, width() - 16);
    for (auto& sec : sections_) {
      if (sec) {
        sec->reflow(avail_w);
      }
    }
  }

  void update_icons(const QColor& fg) {
    current_fg_ = fg;
    for (auto& sec : sections_) {
      if (!sec) continue;
      for (auto& it : sec->items()) {
        if (!it) continue;
        if (it->icon_type.has_value()) {
          const QIcon ic = IconFactory::create_icon(*it->icon_type, fg);
          if (it->button) it->button->setIcon(ic);
          if (it->tree_item) it->tree_item->setIcon(0, ic);
        }
      }
    }
  }

  [[nodiscard]] bool has_section(const QString& name) const {
    const QString q = name.toLower();
    for (const auto& sec : sections_) {
      if (sec && sec->title().toLower().contains(q)) return true;
    }
    return false;
  }

  [[nodiscard]] int custom_section_count() const {
    int cnt = 0;
    for (const auto& sec : sections_) {
      if (sec && !sec->is_builtin()) cnt++;
    }
    return cnt;
  }

  [[nodiscard]] int total_tool_count() const {
    int cnt = 0;
    for (const auto& sec : sections_) {
      if (sec) cnt += static_cast<int>(sec->items().size());
    }
    return cnt;
  }

  [[nodiscard]] bool is_category_builtin(QTreeWidgetItem* item) const {
    if (!item) return false;
    return item->data(0, Qt::UserRole + 10).toBool();
  }

  [[nodiscard]] bool is_item_builtin(QTreeWidgetItem* item) const {
    if (!item) return false;
    return item->data(0, Qt::UserRole + 10).toBool();
  }

  std::shared_ptr<ToolCategorySection> find_or_create_section(const QString& title, bool is_builtin = false) {
    for (auto& s : sections_) {
      if (s && s->title() == title) return s;
    }

    auto sec = std::make_shared<ToolCategorySection>(title, is_builtin, this, widget_columns_);
    layout_columns_->addWidget(sec->header());
    layout_columns_->addWidget(sec->container());

    // Insert before stretch in columns layout
    sections_.push_back(sec);

    // Tree item
    auto* cat_item = new QTreeWidgetItem(tree_, QStringList() << (title + (is_builtin ? " 🔒" : "")));
    cat_item->setData(0, Qt::UserRole + 10, is_builtin);
    cat_item->setExpanded(true);
    QFont bold_font = cat_item->font(0);
    bold_font.setBold(true);
    cat_item->setFont(0, bold_font);
    cat_tree_items_[title] = cat_item;

    reflow_columns();
    return sec;
  }

  QTreeWidgetItem* create_new_section(const QString& title) {
    auto sec = find_or_create_section(title, false);
    return cat_tree_items_.value(title, nullptr);
  }

  bool remove_section(const QString& title_query) {
    const QString q = title_query.toLower();
    for (auto it = sections_.begin(); it != sections_.end(); ++it) {
      if (*it && (*it)->title().toLower().contains(q)) {
        if ((*it)->is_builtin()) {
          return false; // Non-editable built-in groups protected!
        }
        const QString full_title = (*it)->title();
        delete (*it)->header();
        delete (*it)->container();
        sections_.erase(it);

        if (cat_tree_items_.contains(full_title)) {
          delete cat_tree_items_.take(full_title);
        }
        reflow_columns();
        return true;
      }
    }
    return false;
  }

  std::shared_ptr<ToolItem> add_tool(const QString& category_title,
                                     const QString& id,
                                     const QString& name,
                                     const QString& display_text,
                                     std::optional<IconFactory::IconType> icon_type,
                                     const QString& tooltip,
                                     std::function<void()> on_triggered,
                                     bool is_builtin = true) {
    auto sec = find_or_create_section(category_title, is_builtin);
    auto item = std::make_shared<ToolItem>();
    item->id = id;
    item->name = name;
    item->display_text = display_text;
    item->tooltip = tooltip;
    item->icon_type = icon_type;
    item->on_triggered = std::move(on_triggered);
    item->is_builtin = is_builtin;

    const int two_lines = compute_two_lines_height();

    // Create Tool Button with picture of two text lines and wrapped text under icon
    auto* btn = new QToolButton(sec->container());
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(two_lines, two_lines));
    btn->setFixedSize(76, two_lines + 34);
    btn->setText(display_text);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        "QToolButton { font-size: 10px; border-radius: 4px; padding: 2px; text-align: center; border: 1px solid transparent; } "
        "QToolButton:hover { background-color: rgba(128, 128, 128, 0.18); border: 1px solid rgba(128, 128, 128, 0.3); } "
        "QToolButton:checked { background-color: #0078D4; color: #FFFFFF; border: 1px solid #005A9E; font-weight: bold; } "
        "QToolButton:disabled { color: #888888; }");

    if (icon_type.has_value()) {
      btn->setIcon(IconFactory::create_icon(*icon_type, current_fg_));
    }

    connect(btn, &QToolButton::clicked, this, [item]() {
      if (item->on_triggered) item->on_triggered();
    });

    item->button = btn;

    // Create Tree Item
    auto* cat_tree = cat_tree_items_.value(category_title, nullptr);
    if (cat_tree) {
      auto* t_item = new QTreeWidgetItem(cat_tree, QStringList() << name);
      t_item->setToolTip(0, tooltip);
      t_item->setIcon(0, btn->icon());
      t_item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void*>(item.get())));
      t_item->setData(0, Qt::UserRole + 10, is_builtin);
      item->tree_item = t_item;
    }

    sec->add_item(item);
    reflow_columns();
    return item;
  }

  bool load_package(const QString& file_path) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
      return false;
    }
    const QByteArray data = file.readAll();
    const QString content = QString::fromUtf8(data);
    QFileInfo fi(file_path);
    const QString ext = fi.suffix().toLower();
    const QString base_name = fi.baseName();
    const QString cat_title = "📦 " + base_name;

    int imported_count = 0;

    if (ext == "madi" || ext == "json" || ext == "jsonc" || ext == "vlib" || ext == "vdiag" || ext == "yaml") {
      io::StorageEngine engine;
      core::GraphModel loaded_graph;
      core::BlockLibraryManager loaded_lib;
      auto res = engine.deserialize(content.toStdString(), loaded_graph, &loaded_lib);
      if (res.success) {
        for (const auto& [uri, def] : loaded_lib.definitions()) {
          QString item_name = QString::fromStdString(def.name().empty() ? uri : def.name());
          add_tool(cat_title, "pkg_" + item_name, item_name, item_name.left(10) + "\nBlock",
                   IconFactory::IconType::SubsystemBlock,
                   "Package Subsystem Block: " + item_name,
                   [this, item_name]() {
                     if (canvas_) {
                       statusBar_msg("Package tool invoked: " + item_name);
                     }
                   }, false);
          imported_count++;
        }
        for (const auto& [nid, node] : loaded_graph.nodes()) {
          (void)nid;
          if (node.is_subsystem() || !node.name.empty()) {
            QString nname = QString::fromStdString(node.name.empty() ? "Block" : node.name);
            add_tool(cat_title, "pkg_node_" + QString::number(nid), nname, nname.left(10) + "\nItem",
                     IconFactory::IconType::Rectangle,
                     "Diagram Element: " + nname,
                     [this, nname]() {
                       statusBar_msg("Diagram tool invoked: " + nname);
                     }, false);
            imported_count++;
          }
        }
      }
    } else if (ext == "drawio" || ext == "xml") {
      io::DrawioParser parser;
      core::GraphModel parsed_graph;
      auto parse_res = parser.import_xml(content.toStdString(), parsed_graph);
      if (parse_res.success) {
        for (const auto& [nid, node] : parsed_graph.nodes()) {
          (void)nid;
          QString nname = QString::fromStdString(node.name.empty() ? "Drawio Cell" : node.name);
          add_tool(cat_title, "drawio_" + QString::number(nid), nname, nname.left(10) + "\nShape",
                   IconFactory::IconType::Rectangle,
                   "Draw.io Stencil: " + nname,
                   [this, nname]() {
                     statusBar_msg("Draw.io tool invoked: " + nname);
                   }, false);
          imported_count++;
        }
      }
    }

    if (imported_count == 0) {
      add_tool(cat_title, "pkg_default", base_name, base_name.left(10) + "\nPackage",
               IconFactory::IconType::SubsystemBlock,
               "Loaded Package: " + base_name,
               [this, base_name]() {
                 statusBar_msg("Package tool: " + base_name);
               }, false);
      imported_count++;
    }

    reflow_columns();
    return imported_count > 0;
  }

  // Getters for specific built-in tool buttons used by MainWindow & tests
  [[nodiscard]] QToolButton* btn_select() const noexcept { return btn_select_; }
  [[nodiscard]] QToolButton* btn_wire() const noexcept { return btn_wire_; }
  [[nodiscard]] QToolButton* btn_pan() const noexcept { return btn_pan_; }
  [[nodiscard]] QToolButton* btn_delete() const noexcept { return btn_delete_; }

  [[nodiscard]] QToolButton* btn_merge() const noexcept { return btn_merge_; }
  [[nodiscard]] QToolButton* btn_intersect() const noexcept { return btn_intersect_; }
  [[nodiscard]] QToolButton* btn_subtract() const noexcept { return btn_subtract_; }
  [[nodiscard]] QToolButton* btn_xor() const noexcept { return btn_xor_; }
  [[nodiscard]] QToolButton* btn_split() const noexcept { return btn_split_; }

  [[nodiscard]] QToolButton* btn_drill_down() const noexcept { return btn_drill_down_; }
  [[nodiscard]] QToolButton* btn_pop_scope() const noexcept { return btn_pop_scope_; }

  [[nodiscard]] QToolButton* btn_front() const noexcept { return btn_front_; }
  [[nodiscard]] QToolButton* btn_forward() const noexcept { return btn_forward_; }
  [[nodiscard]] QToolButton* btn_backward() const noexcept { return btn_backward_; }
  [[nodiscard]] QToolButton* btn_back() const noexcept { return btn_back_; }

  [[nodiscard]] QToolButton* btn_rot_ccw() const noexcept { return btn_rot_ccw_; }
  [[nodiscard]] QToolButton* btn_rot_cw() const noexcept { return btn_rot_cw_; }
  [[nodiscard]] QToolButton* btn_angle_snap() const noexcept { return btn_angle_snap_; }

  [[nodiscard]] QToolButton* btn_undo() const noexcept { return btn_undo_; }
  [[nodiscard]] QToolButton* btn_redo() const noexcept { return btn_redo_; }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QDockWidget::resizeEvent(event);
    reflow_columns();
  }

private:
  void statusBar_msg(const QString& msg) {
    if (auto* w = window()) {
      if (auto* mw = qobject_cast<QMainWindow*>(w)) {
        mw->statusBar()->showMessage(msg, 3000);
      }
    }
  }

  void filter_items(const QString& text) {
    const QString query = text.trimmed().toLower();
    for (auto& sec : sections_) {
      if (!sec) continue;
      int visible_in_sec = 0;
      for (auto& it : sec->items()) {
        if (!it) continue;
        const bool match = query.isEmpty() ||
            it->name.toLower().contains(query) ||
            it->display_text.toLower().contains(query) ||
            it->tooltip.toLower().contains(query);
        if (it->button) it->button->setVisible(match);
        if (it->tree_item) it->tree_item->setHidden(!match);
        if (match) visible_in_sec++;
      }
      if (sec->header()) sec->header()->setVisible(visible_in_sec > 0 || query.isEmpty());
      if (sec->container()) sec->container()->setVisible(visible_in_sec > 0 || query.isEmpty());
    }
    reflow_columns();
  }

  void populate_builtin_tools() {
    // 1. Interaction & Selection Tools 🔒
    const QString cat_interact = "🎯 Interaction Tools";
    auto it_select = add_tool(cat_interact, "tool_select", "Select & Transform", "Select &\nTransform",
                              IconFactory::IconType::Select,
                              "Select & Transform (S)\nPick, move, resize, and edit items on canvas",
                              [this]() {
                                if (canvas_) canvas_->set_tool_mode(madi::renderer::ToolMode::Select);
                              }, true);
    btn_select_ = it_select->button;
    btn_select_->setCheckable(true);
    btn_select_->setChecked(true);

    auto it_wire = add_tool(cat_interact, "tool_wire", "Orthogonal Net Connector", "Net Wire\nConnector",
                            IconFactory::IconType::Wire,
                            "Orthogonal Net Connector (C)\nRoute orthogonal obstacle-avoiding wires",
                            [this]() {
                              if (canvas_) canvas_->set_tool_mode(madi::renderer::ToolMode::AddConnector);
                            }, true);
    btn_wire_ = it_wire->button;
    btn_wire_->setCheckable(true);

    auto it_pan = add_tool(cat_interact, "tool_pan", "Pan Canvas", "Pan\nCanvas",
                           IconFactory::IconType::Pan,
                           "Pan Canvas (Space+Drag)\nHold Space and drag with mouse to pan",
                           [this]() {
                             statusBar_msg("Pan Tool: Click and drag canvas or hold Spacebar to pan viewport.");
                           }, true);
    btn_pan_ = it_pan->button;

    auto it_del = add_tool(cat_interact, "tool_delete", "Delete Selected", "Delete\nSelected",
                           IconFactory::IconType::Delete,
                           "Delete Selected (Del)\nRemove selected figures and connections",
                           [this]() {
                             if (canvas_) canvas_->delete_selected();
                           }, true);
    btn_delete_ = it_del->button;
    btn_delete_->setEnabled(false);

    // 2. Boolean & Combine 🔒
    const QString cat_bool = "📐 Boolean & Combine";
    auto it_merge = add_tool(cat_bool, "tool_merge", "Union (OR Merge)", "Union\n(Merge)",
                             IconFactory::IconType::BooleanUnion,
                             "Union / OR (Merge) (Ctrl+G)\nCombine multiple figures into a unified polygon",
                             [this]() {
                               if (canvas_) canvas_->merge_selected_figures();
                             }, true);
    btn_merge_ = it_merge->button;
    btn_merge_->setEnabled(false);

    auto it_intersect = add_tool(cat_bool, "tool_intersect", "Intersection (AND)", "Intersection\n(AND)",
                                 IconFactory::IconType::BooleanIntersect,
                                 "Intersection (AND) (Ctrl+Shift+I)\nKeep overlapping region of figures",
                                 [this]() {
                                   if (canvas_) canvas_->boolean_combine_selected(madi::core::BooleanOp::Intersection);
                                 }, true);
    btn_intersect_ = it_intersect->button;
    btn_intersect_->setEnabled(false);

    auto it_subtract = add_tool(cat_bool, "tool_subtract", "Subtract / Difference", "Subtract\n(Difference)",
                                IconFactory::IconType::BooleanSubtract,
                                "Subtract / Difference (Ctrl+Shift+D)\nCut subsequent figures out of first figure",
                                [this]() {
                                  if (canvas_) canvas_->boolean_combine_selected(madi::core::BooleanOp::Difference);
                                }, true);
    btn_subtract_ = it_subtract->button;
    btn_subtract_->setEnabled(false);

    auto it_xor = add_tool(cat_bool, "tool_xor", "XOR / Symmetric Difference", "XOR\n(SymDiff)",
                           IconFactory::IconType::BooleanXor,
                           "XOR / Symmetric Difference (Ctrl+Shift+X)\nKeep regions that do not overlap",
                           [this]() {
                             if (canvas_) canvas_->boolean_combine_selected(madi::core::BooleanOp::Xor);
                           }, true);
    btn_xor_ = it_xor->button;
    btn_xor_->setEnabled(false);

    auto it_split = add_tool(cat_bool, "tool_split", "Split Selected Figure", "Split\nFigure",
                             IconFactory::IconType::SplitFigure,
                             "Split Selected Figure (Ctrl+Shift+G)\nUngroup composite figure into elements",
                             [this]() {
                               if (canvas_) canvas_->split_selected_figure();
                             }, true);
    btn_split_ = it_split->button;
    btn_split_->setEnabled(false);

    // 3. Hierarchy & Scope 🔒
    const QString cat_hier = "⚡ Hierarchy & Scope";
    auto it_down = add_tool(cat_hier, "tool_down", "Drill Down", "Drill Down\n(Enter)",
                            IconFactory::IconType::DrillDown,
                            "Drill Down: Descend into internal view (Enter)",
                            [this]() {
                              if (canvas_) canvas_->drill_down_selected();
                            }, true);
    btn_drill_down_ = it_down->button;
    btn_drill_down_->setEnabled(false);

    auto it_up = add_tool(cat_hier, "tool_up", "Up to Parent", "Up to\nParent (Esc)",
                          IconFactory::IconType::PopScope,
                          "Up to Parent: Ascend to parent diagram level (Esc)",
                          [this]() {
                            if (canvas_) canvas_->pop_hierarchy_scope();
                          }, true);
    btn_pop_scope_ = it_up->button;
    btn_pop_scope_->setEnabled(false);

    // 4. Stacking & Order 🔒
    const QString cat_stack = "📑 Stacking & Order";
    auto it_front = add_tool(cat_stack, "tool_front", "Bring to Front", "Bring to\nFront",
                             IconFactory::IconType::BringToFront,
                             "Bring to Front (Ctrl+Shift+])",
                             [this]() {
                               if (canvas_) canvas_->bring_to_front_selected();
                             }, true);
    btn_front_ = it_front->button;
    btn_front_->setEnabled(false);

    auto it_forward = add_tool(cat_stack, "tool_forward", "Bring Forward", "Bring\nForward",
                               IconFactory::IconType::BringForward,
                               "Bring Forward (Ctrl+])",
                               [this]() {
                                 if (canvas_) canvas_->bring_forward_selected();
                               }, true);
    btn_forward_ = it_forward->button;
    btn_forward_->setEnabled(false);

    auto it_backward = add_tool(cat_stack, "tool_backward", "Send Backward", "Send\nBackward",
                                IconFactory::IconType::SendBackward,
                                "Send Backward (Ctrl+[)",
                                [this]() {
                                  if (canvas_) canvas_->send_backward_selected();
                                }, true);
    btn_backward_ = it_backward->button;
    btn_backward_->setEnabled(false);

    auto it_back = add_tool(cat_stack, "tool_back", "Send to Back", "Send to\nBack",
                            IconFactory::IconType::SendToBack,
                            "Send to Back (Ctrl+Shift+[)",
                            [this]() {
                              if (canvas_) canvas_->send_to_back_selected();
                            }, true);
    btn_back_ = it_back->button;
    btn_back_->setEnabled(false);

    // 5. Rotate & Snapping 🔒
    const QString cat_rot = "🔄 Rotate & Snap";
    auto it_ccw = add_tool(cat_rot, "tool_rot_ccw", "Rotate CCW", "Rotate CCW\n(-90°)",
                           IconFactory::IconType::RotateCCW,
                           "Rotate 90° Counter-Clockwise (Ctrl+Shift+R)",
                           [this]() {
                             if (canvas_) canvas_->rotate_selected(-90.0);
                           }, true);
    btn_rot_ccw_ = it_ccw->button;
    btn_rot_ccw_->setEnabled(false);

    auto it_cw = add_tool(cat_rot, "tool_rot_cw", "Rotate CW", "Rotate CW\n(+90°)",
                          IconFactory::IconType::RotateCW,
                          "Rotate 90° Clockwise (Ctrl+R)",
                          [this]() {
                            if (canvas_) canvas_->rotate_selected(90.0);
                          }, true);
    btn_rot_cw_ = it_cw->button;
    btn_rot_cw_->setEnabled(false);

    auto it_angle_snap = add_tool(cat_rot, "tool_angle_snap", "Angle Snap", "Angle Snap\n(Toggle)",
                                  IconFactory::IconType::AngleSnap,
                                  "Toggle Angle Snapping (Alt+A)\nSnaps figure rotation to angular increments",
                                  [this]() {
                                    if (canvas_) {
                                      canvas_->set_angle_snapping_enabled(!canvas_->is_angle_snapping_enabled());
                                      btn_angle_snap_->setChecked(canvas_->is_angle_snapping_enabled());
                                    }
                                  }, true);
    btn_angle_snap_ = it_angle_snap->button;
    btn_angle_snap_->setCheckable(true);
    btn_angle_snap_->setChecked(canvas_ ? canvas_->is_angle_snapping_enabled() : true);

    // 6. History / Undo 🔒
    const QString cat_hist = "⏱ History & Undo";
    auto it_undo = add_tool(cat_hist, "tool_undo", "Undo", "Undo\nAction",
                            IconFactory::IconType::Undo,
                            "Undo (Ctrl+Z)",
                            [this]() {
                              if (history_) history_->undo();
                            }, true);
    btn_undo_ = it_undo->button;
    btn_undo_->setEnabled(false);

    auto it_redo = add_tool(cat_hist, "tool_redo", "Redo", "Redo\nAction",
                            IconFactory::IconType::Redo,
                            "Redo (Ctrl+Y)",
                            [this]() {
                              if (history_) history_->redo();
                            }, true);
    btn_redo_ = it_redo->button;
    btn_redo_->setEnabled(false);

    layout_columns_->addStretch();
  }

  void trigger_tree_item(QTreeWidgetItem* item) {
    if (!item) return;
    auto* ptr = static_cast<ToolItem*>(item->data(0, Qt::UserRole).value<void*>());
    if (ptr && ptr->button) {
      ptr->button->animateClick();
    }
  }

  void execute_current_selected() {
    auto* cur = tree_->currentItem();
    if (cur) {
      trigger_tree_item(cur);
    }
  }

  void remove_current_selected() {
    auto* cur = tree_->currentItem();
    if (!cur) return;

    if (cur->parent() == nullptr) {
      if (is_category_builtin(cur)) return;
      const QString title = cur->text(0).replace(" 🔒", "").trimmed();
      remove_section(title);
    } else {
      if (is_item_builtin(cur)) return;
      auto* ptr = static_cast<ToolItem*>(cur->data(0, Qt::UserRole).value<void*>());
      if (ptr) {
        if (ptr->button) delete ptr->button;
        auto* par = cur->parent();
        const QString par_title = par ? par->text(0).replace(" 🔒", "").trimmed() : "";
        for (auto& sec : sections_) {
          if (sec && sec->title() == par_title) {
            sec->remove_item(ptr->id);
            break;
          }
        }
        delete cur;
        reflow_columns();
      }
    }
  }

  void prompt_new_section_dialog() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "New Tool Category", "Enter category name for custom tools:",
        QLineEdit::Normal, "⚡ Custom Operations", &ok);
    if (ok && !name.trimmed().isEmpty()) {
      create_new_section(name.trimmed());
    }
  }

  void prompt_add_tool_dialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Add Custom Tool");
    auto* form = new QFormLayout(&dlg);

    auto* edit_name = new QLineEdit(&dlg);
    edit_name->setPlaceholderText("Tool Name (e.g. Align Centers)");
    form->addRow("Tool Name:", edit_name);

    auto* edit_desc = new QLineEdit(&dlg);
    edit_desc->setPlaceholderText("Description or Shortcut");
    form->addRow("Description:", edit_desc);

    auto* combo_cat = new QComboBox(&dlg);
    for (const auto& sec : sections_) {
      if (sec) combo_cat->addItem(sec->title());
    }
    form->addRow("Category:", combo_cat);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
      const QString name = edit_name->text().trimmed();
      if (!name.isEmpty()) {
        const QString cat = combo_cat->currentText();
        const QString desc = edit_desc->text().trimmed();
        add_tool(cat, "custom_" + QString::number(rand()), name, name.left(10) + "\nCustom",
                 IconFactory::IconType::Select, desc.isEmpty() ? name : desc,
                 [this, name]() {
                   statusBar_msg("Executed custom tool: " + name);
                 }, false);
      }
    }
  }

  void prompt_load_package_dialog() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Tool Package", QString(),
        "Supported Packages (*.vlib *.vdiag *.madi *.json *.drawio *.xml);;All Files (*.*)");
    if (!path.isEmpty()) {
      load_package(path);
    }
  }

  void show_tree_context_menu(const QPoint& pos) {
    auto* item = tree_->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    if (item->parent() == nullptr) {
      if (is_category_builtin(item)) {
        auto* act = menu.addAction("🔒 Built-in Category (Non-Editable)");
        act->setEnabled(false);
      } else {
        auto* act_rename = menu.addAction("Rename Category...");
        connect(act_rename, &QAction::triggered, this, [this, item]() {
          bool ok = false;
          const QString cur_text = item->text(0).replace(" 🔒", "").trimmed();
          const QString new_name = QInputDialog::getText(
              this, "Rename Category", "New category name:",
              QLineEdit::Normal, cur_text, &ok);
          if (ok && !new_name.trimmed().isEmpty()) {
            for (auto& sec : sections_) {
              if (sec && sec->title() == cur_text) {
                sec->set_title(new_name.trimmed());
                break;
              }
            }
            item->setText(0, new_name.trimmed());
          }
        });

        auto* act_del = menu.addAction("Delete Category");
        connect(act_del, &QAction::triggered, this, &ToolLibraryDock::remove_current_selected);
      }
    } else {
      if (is_item_builtin(item)) {
        auto* act = menu.addAction("🔒 Built-in Tool (Non-Editable)");
        act->setEnabled(false);
      } else {
        auto* act_del = menu.addAction("Delete Tool");
        connect(act_del, &QAction::triggered, this, &ToolLibraryDock::remove_current_selected);
      }
    }
    menu.exec(tree_->mapToGlobal(pos));
  }

  renderer::CanvasWidget* canvas_{nullptr};
  core::CommandHistory* history_{nullptr};

  ViewMode view_mode_{ViewMode::Columns};
  QLineEdit* search_edit_{nullptr};
  QToolButton* btn_view_mode_{nullptr};

  QStackedWidget* view_stack_{nullptr};
  QScrollArea* scroll_columns_{nullptr};
  QWidget* widget_columns_{nullptr};
  QVBoxLayout* layout_columns_{nullptr};

  QTreeWidget* tree_{nullptr};

  QPushButton* btn_add_{nullptr};
  QPushButton* btn_add_category_{nullptr};
  QPushButton* btn_load_package_{nullptr};
  QPushButton* btn_remove_{nullptr};
  QPushButton* btn_execute_{nullptr};

  std::vector<std::shared_ptr<ToolCategorySection>> sections_;
  QMap<QString, QTreeWidgetItem*> cat_tree_items_;
  QColor current_fg_{Qt::white};

  // Cached pointers to built-in tool buttons
  QToolButton* btn_select_{nullptr};
  QToolButton* btn_wire_{nullptr};
  QToolButton* btn_pan_{nullptr};
  QToolButton* btn_delete_{nullptr};

  QToolButton* btn_merge_{nullptr};
  QToolButton* btn_intersect_{nullptr};
  QToolButton* btn_subtract_{nullptr};
  QToolButton* btn_xor_{nullptr};
  QToolButton* btn_split_{nullptr};

  QToolButton* btn_drill_down_{nullptr};
  QToolButton* btn_pop_scope_{nullptr};

  QToolButton* btn_front_{nullptr};
  QToolButton* btn_forward_{nullptr};
  QToolButton* btn_backward_{nullptr};
  QToolButton* btn_back_{nullptr};

  QToolButton* btn_rot_ccw_{nullptr};
  QToolButton* btn_rot_cw_{nullptr};
  QToolButton* btn_angle_snap_{nullptr};

  QToolButton* btn_undo_{nullptr};
  QToolButton* btn_redo_{nullptr};
};

// ==============================================================================
// Inline Implementation of ToolCategoryHeader & ToolCategorySection
// ==============================================================================

inline ToolCategoryHeader::ToolCategoryHeader(const QString& title, bool is_builtin, QWidget* container_widget, ToolLibraryDock* dock, QWidget* parent)
    : QWidget(parent), title_(title), container_widget_(container_widget), dock_(dock), is_builtin_(is_builtin) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(4, 2, 4, 2);
  layout->setSpacing(4);
  layout->setAlignment(Qt::AlignLeft);

  btn_toggle_ = new QToolButton(this);
  btn_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  btn_toggle_->setText(title + (is_builtin ? " 🔒" : ""));
  btn_toggle_->setStyleSheet("QToolButton { font-weight: bold; font-size: 11px; border: none; background: transparent; text-align: left; }");
  btn_toggle_->setCheckable(true);
  btn_toggle_->setChecked(true);
  btn_toggle_->setArrowType(Qt::DownArrow);

  connect(btn_toggle_, &QToolButton::toggled, this, [this](bool checked) {
    btn_toggle_->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    if (container_widget_) {
      container_widget_->setVisible(checked);
    }
    if (dock_ && checked) {
      dock_->reflow_columns();
    }
  });

  layout->addWidget(btn_toggle_, 1);
}

inline void ToolCategoryHeader::contextMenuEvent(QContextMenuEvent* event) {
  QMenu menu(this);
  if (is_builtin_) {
    auto* act = menu.addAction("🔒 Built-in Group (Non-Editable)");
    act->setEnabled(false);
  } else {
    auto* act_rename = menu.addAction("Rename Category...");
    connect(act_rename, &QAction::triggered, this, [this]() {
      bool ok = false;
      const QString new_name = QInputDialog::getText(
          this, "Rename Category", "New category name:",
          QLineEdit::Normal, title_, &ok);
      if (ok && !new_name.trimmed().isEmpty() && dock_) {
        title_ = new_name.trimmed();
        set_title(title_, is_builtin_);
      }
    });

    auto* act_del = menu.addAction("Delete Category");
    connect(act_del, &QAction::triggered, this, [this]() {
      if (dock_) dock_->remove_section(title_);
    });
  }
  menu.exec(event->globalPos());
}

inline ToolCategorySection::ToolCategorySection(const QString& title, bool is_builtin, ToolLibraryDock* dock, QWidget* parent_widget)
    : QObject(parent_widget), title_(title), is_builtin_(is_builtin) {
  container_ = new QWidget(parent_widget);
  grid_layout_ = new QGridLayout(container_);
  grid_layout_->setContentsMargins(2, 2, 2, 2);
  grid_layout_->setSpacing(4);
  grid_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

  header_ = new ToolCategoryHeader(title, is_builtin, container_, dock, parent_widget);
}

} // namespace madi::gui

#endif // MADI_ENABLE_QT
#endif // MADI_GUI_TOOL_LIBRARY_DOCK_HPP
