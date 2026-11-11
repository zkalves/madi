// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_GUI_KEYBOARD_SHORTCUTS_DIALOG_HPP
#define MADI_GUI_KEYBOARD_SHORTCUTS_DIALOG_HPP

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QTabWidget>
#include <QGuiApplication>
#include <QClipboard>
#include <vector>

namespace madi::gui {

struct ShortcutEntry {
  QString action;
  QString shortcut;
  QString category;
  QString description;
};

class KeyboardShortcutsDialog : public QDialog {
  Q_OBJECT

public:
  explicit KeyboardShortcutsDialog(QWidget* parent = nullptr)
      : QDialog(parent) {
    setWindowTitle("Keyboard Shortcuts Reference");
    resize(760, 620);
    setMinimumSize(600, 480);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(14, 14, 14, 14);
    main_layout->setSpacing(10);

    // Header banner
    auto* header_widget = new QWidget(this);
    auto* header_layout = new QVBoxLayout(header_widget);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(3);

    auto* title_lbl = new QLabel("<h2>⌨️ Keyboard Shortcuts Reference</h2>", header_widget);
    auto* subtitle_lbl = new QLabel(
        "Comprehensive keyboard shortcut reference for schematic diagrams, routing, and canvas navigation.",
        header_widget);
    subtitle_lbl->setStyleSheet("color: #64748B; font-size: 12px;");
    header_layout->addWidget(title_lbl);
    header_layout->addWidget(subtitle_lbl);
    main_layout->addWidget(header_widget);

    // Search bar
    auto* search_layout = new QHBoxLayout();
    search_input_ = new QLineEdit(this);
    search_input_->setPlaceholderText("🔍 Filter shortcuts (e.g. select, wire, snap, rotate, zoom, group, nudge)...");
    search_input_->setClearButtonEnabled(true);
    search_input_->setStyleSheet("QLineEdit { padding: 6px 10px; font-size: 13px; border: 1px solid #CBD5E1; border-radius: 6px; }");
    search_layout->addWidget(search_input_);
    main_layout->addLayout(search_layout);

    // Initialize database of shortcuts
    init_shortcuts_data();

    // Table widget
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Action / Command", "Shortcut", "Category", "Description"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setStyleSheet(
        "QTableWidget { gridline-color: #E2E8F0; font-size: 12px; border: 1px solid #CBD5E1; border-radius: 6px; }"
        "QHeaderView::section { background-color: #F8FAFC; padding: 6px; font-weight: bold; border: none; border-bottom: 1px solid #CBD5E1; }");

    populate_table();
    main_layout->addWidget(table_, 1);

    connect(search_input_, &QLineEdit::textChanged, this, &KeyboardShortcutsDialog::filter_table);

    // Bottom action row
    auto* bottom_layout = new QHBoxLayout();
    auto* btn_copy = new QPushButton("📋 Copy Shortcuts to Clipboard", this);
    btn_copy->setToolTip("Copy the complete shortcut table as Markdown text to your clipboard");
    connect(btn_copy, &QPushButton::clicked, this, &KeyboardShortcutsDialog::copy_to_clipboard);

    auto* btn_close = new QPushButton("Close", this);
    btn_close->setDefault(true);
    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);

    bottom_layout->addWidget(btn_copy);
    bottom_layout->addStretch();
    bottom_layout->addWidget(btn_close);
    main_layout->addLayout(bottom_layout);
  }

  void filter_table(const QString& text) {
    populate_table(text);
  }

  void populate_table(const QString& filter = "") {
    table_->setRowCount(0);
    const QString f = filter.trimmed().toLower();

    for (const auto& e : entries_) {
      if (!f.isEmpty()) {
        const bool match = e.action.toLower().contains(f) ||
                           e.shortcut.toLower().contains(f) ||
                           e.category.toLower().contains(f) ||
                           e.description.toLower().contains(f);
        if (!match) continue;
      }

      const int row = table_->rowCount();
      table_->insertRow(row);

      auto* item_action = new QTableWidgetItem(e.action);
      item_action->setFont(QFont("sans-serif", 10, QFont::Bold));

      auto* item_sc = new QTableWidgetItem(e.shortcut);
      item_sc->setForeground(QColor(0x02, 0x84, 0xC7)); // Blue highlight
      item_sc->setFont(QFont("monospace", 10, QFont::Bold));

      auto* item_cat = new QTableWidgetItem(e.category);
      auto* item_desc = new QTableWidgetItem(e.description);

      table_->setItem(row, 0, item_action);
      table_->setItem(row, 1, item_sc);
      table_->setItem(row, 2, item_cat);
      table_->setItem(row, 3, item_desc);
    }
  }

  void copy_to_clipboard() {
    QString md = "# Madí Keyboard Shortcuts Reference\n\n";
    md += "| Action | Shortcut | Category | Description |\n";
    md += "| :--- | :--- | :--- | :--- |\n";
    for (const auto& e : entries_) {
      md += QString("| **%1** | `%2` | %3 | %4 |\n")
                .arg(e.action, e.shortcut, e.category, e.description);
    }

    if (auto* cb = QGuiApplication::clipboard()) {
      cb->setText(md);
    }
  }

private:
  void init_shortcuts_data() {
    entries_ = {
      // 1. Interaction & Drawing Tools
      {"Pointer / Select Tool", "Ctrl+1, S, V", "Tools", "Select, transform, resize, and edit figures on canvas"},
      {"Wire / Connector Tool", "Ctrl+3, C, W", "Tools", "Route orthogonal obstacle-avoiding connections between ports"},
      {"Rectangle Block", "Ctrl+8, R", "Tools", "Insert or drag-draw rectangular schematic blocks"},
      {"Ellipse / Circle", "Ctrl+9, E, O", "Tools", "Insert or drag-draw state ellipses and circles"},
      {"Rounded Rectangle", "U", "Tools", "Insert rounded corner figure blocks"},
      {"Polygon", "P", "Tools", "Click points on canvas to draw custom polygon, Enter to finish"},
      {"Hierarchical Block", "H", "Tools", "Create subsystem block with isolated internal diagram view"},
      {"Input Port", "I", "Tools", "Add hierarchical input port synced to symbol view"},
      {"Output Port", "O", "Tools", "Add hierarchical output port synced to symbol view"},
      {"Pan Canvas Tool", "Space + Drag", "Tools", "Hold Spacebar and drag canvas with mouse to pan viewport"},
      {"Cancel / Return to Select", "Escape", "Tools", "Cancel active tool, wiring, or placement and return to Select mode"},

      // 2. View & Navigation
      {"Zoom In", "Ctrl++ / Ctrl+=", "View", "Zoom in viewport centered on cursor"},
      {"Zoom Out", "Ctrl+-", "View", "Zoom out viewport centered on cursor"},
      {"Fit to Window", "F3, Ctrl+Shift+W, Ctrl+0", "View", "Zoom viewport to fit entire diagram content on screen"},
      {"Reset View (100%)", "Ctrl+Home, Ctrl+Alt+0", "View", "Reset zoom level to 100% and center origin"},

      // 3. Visual Aids, Grid & Snapping
      {"Toggle Grid", "Shift+F9, Shift+G, Ctrl+'", "Snapping", "Toggle visible Cartesian coordinate grid dots"},
      {"Toggle Snapping", "F9, Ctrl+B, Shift+S", "Snapping", "Toggle object geometry, grid, and port snapping"},
      {"Snap & Grid Settings", "Alt+F9", "Snapping", "Open Snap & Grid settings dialog with tabs"},
      {"Toggle Angle Snapping", "Alt+A", "Snapping", "Snap figure rotation to angular increments (15° default)"},
      {"Free Rotation Override", "Alt (Hold while dragging)", "Snapping", "Bypass angle snap for smooth free rotation"},
      {"Toggle Rulers", "Alt+Shift+R", "Snapping", "Toggle metric canvas rulers on top and left margins"},
      {"Toggle Smart Guides", "Ctrl+Shift+G, Alt+F10", "Snapping", "Toggle dynamic alignment and spacing visual guides"},
      {"Toggle Line Jumps", "None", "Snapping", "Toggle wire bridge loops at crossing intersections"},

      // 4. Figure Rotation & Transform
      {"Rotate 90° Clockwise", "Ctrl+R, Ctrl+Shift+Right", "Arrange", "Rotate selected figure(s) 90 degrees clockwise"},
      {"Rotate 90° CCW", "Ctrl+Shift+R, Ctrl+L", "Arrange", "Rotate selected figure(s) 90 degrees counter-clockwise"},
      {"Rotate 180°", "Ctrl+Alt+R", "Arrange", "Rotate selected figure(s) upside-down (180 degrees)"},
      {"Reset Rotation (0°)", "Ctrl+Alt+0", "Arrange", "Reset rotation of selected figure(s) to 0 degrees"},
      {"Nudge 1 Pixel", "Arrow Keys", "Arrange", "Move selected figure(s) by 1 pixel in arrow direction"},
      {"Nudge 10 Pixels (Large)", "Shift + Arrow Keys", "Arrange", "Move selected figure(s) by 10 pixels / grid step"},

      // 5. Stacking & Grouping
      {"Bring to Front", "Ctrl+Shift+], Ctrl+Shift+F", "Arrange", "Move selected figure(s) to the very top of stacking order"},
      {"Send to Back", "Ctrl+Shift+[, Ctrl+Shift+B", "Arrange", "Move selected figure(s) to the very bottom of stacking order"},
      {"Bring Forward", "Ctrl+], Ctrl+F", "Arrange", "Move selected figure(s) one step forward in stacking order"},
      {"Send Backward", "Ctrl+[", "Arrange", "Move selected figure(s) one step backward in stacking order"},
      {"Union / Group (Merge)", "Ctrl+G", "Arrange", "Combine selected figures into a unified polygon figure"},
      {"Split Figure (Ungroup)", "Ctrl+Shift+G, Ctrl+Shift+U", "Arrange", "Ungroup composite figure into constituent elements"},

      // 6. Edit & History
      {"Undo", "Ctrl+Z", "Edit", "Undo last diagram or geometry operation"},
      {"Redo", "Ctrl+Y, Ctrl+Shift+Z", "Edit", "Redo last undone operation"},
      {"Cut", "Ctrl+X", "Edit", "Cut selected figures or connections to clipboard"},
      {"Copy", "Ctrl+C", "Edit", "Copy selected figures or connections to clipboard"},
      {"Paste", "Ctrl+V", "Edit", "Paste figures from clipboard with offset"},
      {"Duplicate in Place", "Ctrl+D", "Edit", "Duplicate selected figure(s) offset by 20px"},
      {"Delete Selected", "Delete, Backspace", "Edit", "Delete selected figures, ports, or wire connections"},
      {"Select All", "Ctrl+A", "Edit", "Select all figures and connections in current hierarchy scope"},

      // 7. Hierarchy & Subsystems
      {"Drill Down (Push)", "Enter, Ctrl+Down", "Hierarchy", "Descend into selected subsystem / figure internal diagram"},
      {"Up to Parent (Pop)", "Alt+Up, Ctrl+Up", "Hierarchy", "Ascend back to parent diagram hierarchy level"},
      {"Edit Symbol Definition", "Ctrl+Shift+E", "Hierarchy", "Open Symbol Editor for dual-view EDA symbol customization"},

      // 8. Console & Application
      {"Command Console", "F2, : , /", "General", "Focus command console input prompt"},
      {"Keyboard Shortcuts", "F1, ?", "General", "Open this Keyboard Shortcuts Reference Dialog"}
    };
  }

  QLineEdit* search_input_{nullptr};
  QTableWidget* table_{nullptr};
  std::vector<ShortcutEntry> entries_;
};

} // namespace madi::gui

#endif // MADI_GUI_KEYBOARD_SHORTCUTS_DIALOG_HPP
