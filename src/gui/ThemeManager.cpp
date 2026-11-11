// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "madi/gui/ThemeManager.hpp"

#ifdef MADI_ENABLE_QT

#include <QCoreApplication>
#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QPalette>
#include <QStandardPaths>

namespace madi::gui {

QString UITheme::to_qss() const {
  const QString win_bg = get_color("window_bg", "#1E1E2E");
  const QString win_fg = get_color("window_fg", "#CDD6F4");
  const QString surf_bg = get_color("surface_bg", "#252538");
  const QString surf_fg = get_color("surface_fg", "#CDD6F4");
  const QString border = get_color("border", "#45475A");
  const QString border_lt = get_color("border_light", "#313244");
  const QString accent = get_color("accent", "#89B4FA");
  const QString accent_hov = get_color("accent_hover", "#B4BEFE");
  const QString sel_bg = get_color("selection_bg", "#313244");
  const QString sel_fg = get_color("selection_fg", "#89B4FA");
  const QString btn_bg = get_color("button_bg", "#313244");
  const QString btn_fg = get_color("button_fg", "#CDD6F4");
  const QString btn_border = get_color("button_border", "#45475A");
  const QString btn_hov = get_color("button_hover_bg", "#45475A");
  const QString inp_bg = get_color("input_bg", "#181825");
  const QString inp_fg = get_color("input_fg", "#CDD6F4");
  const QString inp_border = get_color("input_border", "#45475A");
  const QString inp_foc = get_color("input_focus_border", "#89B4FA");
  const QString tree_bg = get_color("tree_bg", "#181825");
  const QString tree_fg = get_color("tree_fg", "#CDD6F4");
  const QString tree_border = get_color("tree_border", "#45475A");
  const QString tree_hov = get_color("tree_hover_bg", "#252538");
  const QString tree_sel_bg = get_color("tree_selected_bg", "#313244");
  const QString tree_sel_fg = get_color("tree_selected_fg", "#89B4FA");
  const QString hdr_bg = get_color("header_bg", "#181825");
  const QString hdr_fg = get_color("header_fg", "#A6ADC8");
  const QString hdr_border = get_color("header_border", "#45475A");
  const QString menu_bg = get_color("menu_bg", "#252538");
  const QString menu_fg = get_color("menu_fg", "#CDD6F4");
  const QString tb_bg = get_color("toolbar_bg", "#181825");
  const QString tb_fg = get_color("toolbar_fg", "#CDD6F4");
  const QString dock_title_bg = get_color("dock_title_bg", "#181825");
  const QString dock_title_fg = get_color("dock_title_fg", "#A6ADC8");
  const QString sb_bg = get_color("statusbar_bg", "#181825");
  const QString sb_fg = get_color("statusbar_fg", "#A6ADC8");

  QString qss = R"(
/* Global Window & Controls */
QMainWindow, QWidget#centralWidget {
  background-color: @win_bg@;
  color: @win_fg@;
}

QWidget {
  color: @win_fg@;
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
}

/* Menu Bar & Menus */
QMenuBar {
  background-color: @win_bg@;
  color: @win_fg@;
  border-bottom: 1px solid @border@;
  padding: 2px;
}
QMenuBar::item {
  background: transparent;
  color: @win_fg@;
  padding: 4px 10px;
  border-radius: 4px;
}
QMenuBar::item:selected {
  background-color: @sel_bg@;
  color: @sel_fg@;
}
QMenu {
  background-color: @menu_bg@;
  color: @menu_fg@;
  border: 1px solid @border@;
  padding: 4px;
  border-radius: 6px;
}
QMenu::item {
  background: transparent;
  color: @menu_fg@;
  padding: 5px 24px 5px 20px;
  border-radius: 4px;
}
QMenu::item:selected {
  background-color: @sel_bg@;
  color: @sel_fg@;
}
QMenu::separator {
  height: 1px;
  background-color: @border@;
  margin: 4px 6px;
}

/* ToolBar */
QToolBar {
  background-color: @tb_bg@;
  color: @tb_fg@;
  border-bottom: 1px solid @border_lt@;
  spacing: 4px;
  padding: 4px;
}
QToolButton {
  background: transparent;
  color: @tb_fg@;
  border: 1px solid transparent;
  border-radius: 4px;
  padding: 4px 8px;
  font-weight: 500;
}
QToolButton:hover {
  background-color: @btn_hov@;
  border: 1px solid @border@;
}
QToolButton:checked {
  background-color: @sel_bg@;
  color: @sel_fg@;
  border: 1px solid @accent@;
  font-weight: bold;
}

/* Status Bar */
QStatusBar {
  background-color: @sb_bg@;
  color: @sb_fg@;
  border-top: 1px solid @border_lt@;
}
QStatusBar QLabel {
  color: @sb_fg@;
}

/* Main Window Separators & Splitters (Dividers between Internal Windows and Diagram) */
QMainWindow::separator {
  background-color: @border@;
  width: 6px;
  height: 6px;
  border: 1px solid @border_lt@;
}
QMainWindow::separator:hover {
  background-color: @accent@;
  border: 1px solid @accent_hov@;
}

/* Dock Widgets (Internal Tool Windows) */
QDockWidget {
  background-color: @win_bg@;
  color: @win_fg@;
  border: 1.5px solid @border@;
}
QDockWidget::title {
  background-color: @dock_title_bg@;
  color: @dock_title_fg@;
  padding: 6px 10px;
  font-weight: bold;
  font-size: 11px;
  letter-spacing: 0.5px;
  border: 1px solid @border@;
  border-bottom: 2px solid @accent@;
  border-top-left-radius: 4px;
  border-top-right-radius: 4px;
}
QDockWidget::close-button, QDockWidget::float-button {
  background: transparent;
  border: 1px solid transparent;
  border-radius: 3px;
  padding: 2px;
}
QDockWidget::close-button:hover, QDockWidget::float-button:hover {
  background-color: @btn_hov@;
  border: 1px solid @border@;
}
QDockWidget > QWidget {
  background-color: @surf_bg@;
  border: 1px solid @border@;
  border-top: none;
}

/* Central Diagram Viewport */
#mainDiagramCanvas, QWidget#centralWidget {
  background-color: @win_bg@;
  border: 2px solid @border@;
}

/* Tree Widgets & Element Table */
QTreeWidget, QTreeWidget#elementTable, QTreeView {
  background-color: @tree_bg@;
  color: @tree_fg@;
  border: 1px solid @tree_border@;
  border-radius: 4px;
  padding: 2px;
  font-size: 12px;
  outline: none;
}
QTreeWidget::item, QTreeView::item {
  color: @tree_fg@;
  padding: 4px 6px;
  border-radius: 3px;
}
QTreeWidget::item:hover, QTreeView::item:hover {
  background-color: @tree_hov@;
  color: @tree_fg@;
}
QTreeWidget::item:selected, QTreeView::item:selected {
  background-color: @tree_sel_bg@;
  color: @tree_sel_fg@;
  font-weight: bold;
}
QHeaderView::section {
  background-color: @hdr_bg@;
  color: @hdr_fg@;
  padding: 5px 8px;
  border: 1px solid @hdr_border@;
  font-weight: bold;
  font-size: 11px;
}

/* Table Widgets */
QTableWidget, QTableView {
  background-color: @tree_bg@;
  color: @tree_fg@;
  gridline-color: @border@;
  border: 1px solid @tree_border@;
  border-radius: 4px;
}

/* Inputs & Spinboxes */
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {
  background-color: @inp_bg@;
  color: @inp_fg@;
  border: 1px solid @inp_border@;
  border-radius: 4px;
  padding: 4px 8px;
  selection-background-color: @sel_bg@;
  selection-color: @sel_fg@;
  font-size: 12px;
}
QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus {
  border: 1.5px solid @inp_foc@;
}
QComboBox QAbstractItemView {
  background-color: @menu_bg@;
  color: @menu_fg@;
  selection-background-color: @sel_bg@;
  selection-color: @sel_fg@;
  border: 1px solid @border@;
}

/* Push Buttons */
QPushButton {
  background-color: @btn_bg@;
  color: @btn_fg@;
  border: 1px solid @btn_border@;
  border-radius: 4px;
  padding: 5px 12px;
  font-weight: 500;
  font-size: 12px;
}
QPushButton:hover {
  background-color: @btn_hov@;
  border-color: @accent@;
}
QPushButton:pressed {
  background-color: @sel_bg@;
  color: @sel_fg@;
}
QPushButton:disabled {
  background-color: @border_lt@;
  color: @border@;
  border-color: @border_lt@;
}

/* Group Boxes */
QGroupBox {
  color: @win_fg@;
  font-weight: bold;
  border: 1px solid @border@;
  border-radius: 6px;
  margin-top: 12px;
  padding-top: 10px;
}
QGroupBox::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  left: 10px;
  padding: 0 4px;
  color: @accent@;
}

/* Tab Bar */
QTabBar::tab {
  background: @border_lt@;
  color: @win_fg@;
  border: 1px solid @border@;
  padding: 6px 14px;
  border-top-left-radius: 4px;
  border-top-right-radius: 4px;
  margin-right: 2px;
}
QTabBar::tab:selected {
  background: @surf_bg@;
  color: @accent@;
  font-weight: bold;
  border: 1px solid @border@;
  border-bottom: 2px solid @accent@;
}
QTabBar::tab:hover:!selected {
  background: @btn_hov@;
  color: @win_fg@;
}

/* Scrollbars */
QScrollBar:vertical {
  background: @win_bg@;
  width: 10px;
  margin: 0px;
}
QScrollBar::handle:vertical {
  background: @border@;
  min-height: 20px;
  border-radius: 5px;
}
QScrollBar::handle:vertical:hover {
  background: @accent@;
}
QScrollBar:horizontal {
  background: @win_bg@;
  height: 10px;
  margin: 0px;
}
QScrollBar::handle:horizontal {
  background: @border@;
  min-width: 20px;
  border-radius: 5px;
}
QScrollBar::handle:horizontal:hover {
  background: @accent@;
}
QScrollBar::add-line, QScrollBar::sub-line {
  background: none;
  border: none;
}
)";

  qss.replace("@win_bg@", win_bg);
  qss.replace("@win_fg@", win_fg);
  qss.replace("@surf_bg@", surf_bg);
  qss.replace("@surf_fg@", surf_fg);
  qss.replace("@border@", border);
  qss.replace("@border_lt@", border_lt);
  qss.replace("@accent@", accent);
  qss.replace("@accent_hov@", accent_hov);
  qss.replace("@sel_bg@", sel_bg);
  qss.replace("@sel_fg@", sel_fg);
  qss.replace("@btn_bg@", btn_bg);
  qss.replace("@btn_fg@", btn_fg);
  qss.replace("@btn_border@", btn_border);
  qss.replace("@btn_hov@", btn_hov);
  qss.replace("@inp_bg@", inp_bg);
  qss.replace("@inp_fg@", inp_fg);
  qss.replace("@inp_border@", inp_border);
  qss.replace("@inp_foc@", inp_foc);
  qss.replace("@tree_bg@", tree_bg);
  qss.replace("@tree_fg@", tree_fg);
  qss.replace("@tree_border@", tree_border);
  qss.replace("@tree_hov@", tree_hov);
  qss.replace("@tree_sel_bg@", tree_sel_bg);
  qss.replace("@tree_sel_fg@", tree_sel_fg);
  qss.replace("@hdr_bg@", hdr_bg);
  qss.replace("@hdr_fg@", hdr_fg);
  qss.replace("@hdr_border@", hdr_border);
  qss.replace("@menu_bg@", menu_bg);
  qss.replace("@menu_fg@", menu_fg);
  qss.replace("@tb_bg@", tb_bg);
  qss.replace("@tb_fg@", tb_fg);
  qss.replace("@dock_title_bg@", dock_title_bg);
  qss.replace("@dock_title_fg@", dock_title_fg);
  qss.replace("@sb_bg@", sb_bg);
  qss.replace("@sb_fg@", sb_fg);

  return qss;
}

QPalette UITheme::to_palette() const {
  QPalette pal;
  const QColor win_bg(get_color("window_bg", "#1E1E2E"));
  const QColor win_fg(get_color("window_fg", "#CDD6F4"));
  const QColor surf_bg(get_color("surface_bg", "#252538"));
  const QColor sel_bg(get_color("selection_bg", "#313244"));
  const QColor sel_fg(get_color("selection_fg", "#89B4FA"));
  const QColor btn_bg(get_color("button_bg", "#313244"));
  const QColor btn_fg(get_color("button_fg", "#CDD6F4"));
  const QColor inp_bg(get_color("input_bg", "#181825"));

  pal.setColor(QPalette::Window, win_bg);
  pal.setColor(QPalette::WindowText, win_fg);
  pal.setColor(QPalette::Base, inp_bg);
  pal.setColor(QPalette::AlternateBase, surf_bg);
  pal.setColor(QPalette::ToolTipBase, surf_bg);
  pal.setColor(QPalette::ToolTipText, win_fg);
  pal.setColor(QPalette::Text, win_fg);
  pal.setColor(QPalette::Button, btn_bg);
  pal.setColor(QPalette::ButtonText, btn_fg);
  pal.setColor(QPalette::Highlight, sel_bg);
  pal.setColor(QPalette::HighlightedText, sel_fg);

  return pal;
}

ThemeManager& ThemeManager::instance() {
  static ThemeManager mgr;
  return mgr;
}

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {
  register_built_in_fallbacks();
  scan_themes();
}

void ThemeManager::register_built_in_fallbacks() {
  // Built-in basic fallbacks so the app is always safe even if themes folder is missing
  UITheme dark_ui;
  dark_ui.id = "dark";
  dark_ui.name = "Modern Dark";
  dark_ui.colors["window_bg"] = "#1E1E2E";
  dark_ui.colors["window_fg"] = "#CDD6F4";
  dark_ui.colors["surface_bg"] = "#252538";
  dark_ui.colors["surface_fg"] = "#CDD6F4";
  dark_ui.colors["border"] = "#45475A";
  dark_ui.colors["tree_bg"] = "#181825";
  dark_ui.colors["tree_fg"] = "#CDD6F4";
  dark_ui.colors["tree_border"] = "#45475A";
  dark_ui.colors["tree_selected_bg"] = "#313244";
  dark_ui.colors["tree_selected_fg"] = "#89B4FA";
  dark_ui.colors["tree_hover_bg"] = "#252538";
  dark_ui.colors["header_bg"] = "#181825";
  dark_ui.colors["header_fg"] = "#A6ADC8";
  dark_ui.colors["button_bg"] = "#313244";
  dark_ui.colors["button_fg"] = "#CDD6F4";
  dark_ui.colors["input_bg"] = "#181825";
  dark_ui.colors["input_fg"] = "#CDD6F4";
  dark_ui.colors["selection_bg"] = "#313244";
  dark_ui.colors["selection_fg"] = "#89B4FA";
  ui_themes_.push_back(dark_ui);
  current_ui_theme_ = dark_ui;

  DiagramTheme dark_diag;
  dark_diag.id = "dark";
  dark_diag.name = "Modern Dark";
  diagram_themes_.push_back(dark_diag);
  current_diagram_theme_ = dark_diag;
}

void ThemeManager::scan_themes(const QString& custom_search_path) {
  QStringList search_dirs;
  if (!custom_search_path.isEmpty()) {
    search_dirs << custom_search_path;
  }

  // Look in standard paths (Qt resources, res folder, and system install paths)
  search_dirs << ":/themes"
              << ":/res/themes"
              << "./res/themes"
              << "../res/themes"
              << "../../res/themes"
              << QDir::currentPath() + "/res/themes"
              << QCoreApplication::applicationDirPath() + "/res/themes"
              << QCoreApplication::applicationDirPath() + "/../res/themes"
              << "./themes"
              << "../themes"
              << "../../themes"
              << QDir::currentPath() + "/themes"
              << QCoreApplication::applicationDirPath() + "/themes"
              << QCoreApplication::applicationDirPath() + "/../themes"
              << QCoreApplication::applicationDirPath() + "/../share/madi/themes"
              << QCoreApplication::applicationDirPath() + "/../share/madi/res/themes"
              << "/usr/local/share/madi/themes"
              << "/usr/share/madi/themes";

  for (const QString& base_dir : search_dirs) {
    QDir dir(base_dir);
    if (!dir.exists()) continue;

    QDirIterator it(base_dir, QStringList() << "*.json", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      QString fpath = it.next();
      load_theme_file(fpath);
    }
  }
}

bool ThemeManager::load_theme_file(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }

  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return false;
  }

  QJsonObject root = doc.object();
  QString id = root.value("id").toString(QFileInfo(file_path).baseName());
  QString name = root.value("name").toString(id);
  QString type = root.value("type").toString();

  bool is_ui = (type == "ui") || root.contains("ui") || file_path.contains("/ui/");
  bool is_diag = (type == "diagram") || root.contains("diagram") || file_path.contains("/diagram/");

  if (!is_ui && !is_diag) {
    if (root.contains("colors") && root["colors"].toObject().contains("window_bg")) {
      is_ui = true;
    }
    if (root.contains("colors") && root["colors"].toObject().contains("canvas_bg")) {
      is_diag = true;
    }
  }

  // Parse UI Theme
  if (is_ui) {
    UITheme theme;
    theme.id = id.toStdString();
    theme.name = name.toStdString();
    theme.file_path = file_path.toStdString();

    QJsonObject col_obj = root.contains("ui") ? root["ui"].toObject() : root.value("colors").toObject();
    for (auto it = col_obj.begin(); it != col_obj.end(); ++it) {
      theme.colors[it.key().toStdString()] = it.value().toString();
    }

    // Replace existing if id matches, or append
    auto exist = std::find_if(ui_themes_.begin(), ui_themes_.end(),
                              [&id](const UITheme& t) { return t.id == id.toStdString(); });
    if (exist != ui_themes_.end()) {
      *exist = theme;
    } else {
      ui_themes_.push_back(theme);
    }
  }

  // Parse Diagram Theme
  if (is_diag) {
    DiagramTheme theme;
    theme.id = id.toStdString();
    theme.name = name.toStdString();
    theme.file_path = file_path.toStdString();

    QJsonObject col_obj = root.contains("diagram") ? root["diagram"].toObject() : root.value("colors").toObject();

    auto parse_c = [&col_obj](const QString& key, uint32_t def) -> uint32_t {
      if (col_obj.contains(key)) {
        return hex_to_argb(col_obj.value(key).toString(), def);
      }
      return def;
    };

    theme.canvas_bg = parse_c("canvas_bg", theme.canvas_bg);
    theme.page_bg = parse_c("page_bg", theme.page_bg);
    theme.page_border = parse_c("page_border", theme.page_border);
    theme.grid_minor = parse_c("grid_minor", theme.grid_minor);
    theme.grid_major = parse_c("grid_major", theme.grid_major);
    theme.ruler_bg = parse_c("ruler_bg", theme.ruler_bg);
    theme.ruler_fg = parse_c("ruler_fg", theme.ruler_fg);
    theme.ruler_border = parse_c("ruler_border", theme.ruler_border);
    theme.node_fill = parse_c("node_fill", theme.node_fill);
    theme.node_stroke = parse_c("node_stroke", theme.node_stroke);
    theme.node_text = parse_c("node_text", theme.node_text);
    theme.wire_color = parse_c("wire_color", theme.wire_color);
    theme.selection_color = parse_c("selection_color", theme.selection_color);
    theme.selection_border = parse_c("selection_border", theme.selection_border);
    theme.guide_color = parse_c("guide_color", theme.guide_color);
    theme.jump_bridge_color = parse_c("jump_bridge_color", theme.jump_bridge_color);
    theme.subsystem_banner = parse_c("subsystem_banner", theme.subsystem_banner);
    theme.port_fill = parse_c("port_fill", theme.port_fill);
    theme.port_stroke = parse_c("port_stroke", theme.port_stroke);

    if (col_obj.contains("node_stroke_width")) {
      theme.node_stroke_width = col_obj.value("node_stroke_width").toDouble(1.5);
    }
    if (col_obj.contains("wire_width")) {
      theme.wire_width = col_obj.value("wire_width").toDouble(1.5);
    }

    auto exist = std::find_if(diagram_themes_.begin(), diagram_themes_.end(),
                              [&id](const DiagramTheme& t) { return t.id == id.toStdString(); });
    if (exist != diagram_themes_.end()) {
      *exist = theme;
    } else {
      diagram_themes_.push_back(theme);
    }
  }

  return true;
}

const UITheme* ThemeManager::find_ui_theme(const std::string& id) const noexcept {
  for (const auto& t : ui_themes_) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

const DiagramTheme* ThemeManager::find_diagram_theme(const std::string& id) const noexcept {
  for (const auto& t : diagram_themes_) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

bool ThemeManager::set_active_ui_theme(const std::string& id) {
  const auto* t = find_ui_theme(id);
  if (!t) return false;

  current_ui_theme_ = *t;
  if (qApp) {
    qApp->setStyleSheet(current_ui_theme_.to_qss());
    qApp->setPalette(current_ui_theme_.to_palette());
  }
  emit ui_theme_changed(current_ui_theme_);
  return true;
}

bool ThemeManager::set_active_diagram_theme(const std::string& id) {
  const auto* t = find_diagram_theme(id);
  if (!t) return false;

  current_diagram_theme_ = *t;
  emit diagram_theme_changed(current_diagram_theme_);
  return true;
}

}  // namespace madi::gui

#endif // MADI_ENABLE_QT
