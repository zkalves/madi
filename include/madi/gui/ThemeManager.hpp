// Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MADI_GUI_THEME_MANAGER_HPP
#define MADI_GUI_THEME_MANAGER_HPP

#ifdef MADI_ENABLE_QT

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPalette>
#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "madi/renderer/CanvasRenderer.hpp"

namespace madi::gui {

inline uint32_t hex_to_argb(const QString& hex, uint32_t fallback = 0xFFFFFFFF) {
  QString clean = hex.trimmed();
  if (clean.startsWith('#')) {
    clean = clean.mid(1);
  }
  if (clean.length() == 3) {
    // #RGB -> #RRGGBB
    clean = QString("%1%1%2%2%3%3")
                .arg(clean[0])
                .arg(clean[1])
                .arg(clean[2]);
  }
  if (clean.length() == 6) {
    bool ok = false;
    uint32_t val = clean.toUInt(&ok, 16);
    if (ok) return 0xFF000000 | val;
  } else if (clean.length() == 8) {
    bool ok = false;
    uint32_t val = clean.toUInt(&ok, 16);
    if (ok) return val;
  }
  return fallback;
}

inline QString argb_to_hex(uint32_t argb) {
  return QString("#%1")
      .arg(argb & 0x00FFFFFF, 6, 16, QChar('0'))
      .toUpper();
}

inline QColor argb_to_qcolor(uint32_t c) {
  return QColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
}

struct UITheme {
  std::string id;
  std::string name;
  std::string file_path;
  std::unordered_map<std::string, QString> colors;

  [[nodiscard]] QString get_color(const std::string& key, const QString& def = "#000000") const {
    auto it = colors.find(key);
    return (it != colors.end()) ? it->second : def;
  }

  [[nodiscard]] QString to_qss() const;
  [[nodiscard]] QPalette to_palette() const;
};

struct DiagramTheme {
  std::string id;
  std::string name;
  std::string file_path;

  uint32_t canvas_bg{0xFFEAEEF2};
  uint32_t page_bg{0xFFFFFFFF};
  uint32_t page_border{0xFFCBD5E1};
  uint32_t grid_minor{0xFFE2E8F0};
  uint32_t grid_major{0xFFCBD5E1};
  uint32_t ruler_bg{0xFFF8F9FA};
  uint32_t ruler_fg{0xFF718096};
  uint32_t ruler_border{0xFFCBD5E1};

  uint32_t node_fill{0xFFFFFFFF};
  uint32_t node_stroke{0xFF2D3748};
  double node_stroke_width{1.5};
  uint32_t node_text{0xFF1A202C};

  uint32_t wire_color{0xFF2B6CB0};
  double wire_width{1.5};

  uint32_t selection_color{0x403182CE};
  uint32_t selection_border{0xFF3182CE};
  uint32_t guide_color{0xFF00BCD4};
  uint32_t jump_bridge_color{0xFF2B6CB0};
  uint32_t subsystem_banner{0xFF3182CE};

  uint32_t port_fill{0xFF0078D4};
  uint32_t port_stroke{0xFFFFFFFF};

  [[nodiscard]] renderer::RenderStyle to_render_style() const {
    renderer::RenderStyle style;
    style.fill_color = node_fill;
    style.stroke_color = node_stroke;
    style.stroke_width = node_stroke_width;
    style.text_color = node_text;
    style.selection_color = selection_color;
    style.selection_border = selection_border;
    style.grid_minor = grid_minor;
    style.grid_major = grid_major;
    style.wire_color = wire_color;
    style.banner_color = subsystem_banner;
    style.banner_selected = selection_border;
    return style;
  }

  [[nodiscard]] QColor canvas_bg_qcolor() const { return argb_to_qcolor(canvas_bg); }
  [[nodiscard]] QColor page_bg_qcolor() const { return argb_to_qcolor(page_bg); }
  [[nodiscard]] QColor page_border_qcolor() const { return argb_to_qcolor(page_border); }
  [[nodiscard]] QColor ruler_bg_qcolor() const { return argb_to_qcolor(ruler_bg); }
  [[nodiscard]] QColor ruler_fg_qcolor() const { return argb_to_qcolor(ruler_fg); }
  [[nodiscard]] QColor ruler_border_qcolor() const { return argb_to_qcolor(ruler_border); }
  [[nodiscard]] QColor node_fill_qcolor() const { return argb_to_qcolor(node_fill); }
  [[nodiscard]] QColor node_stroke_qcolor() const { return argb_to_qcolor(node_stroke); }
  [[nodiscard]] QColor node_text_qcolor() const { return argb_to_qcolor(node_text); }
  [[nodiscard]] QColor wire_qcolor() const { return argb_to_qcolor(wire_color); }
  [[nodiscard]] QColor selection_border_qcolor() const { return argb_to_qcolor(selection_border); }
  [[nodiscard]] QColor guide_qcolor() const { return argb_to_qcolor(guide_color); }
  [[nodiscard]] QColor jump_bridge_qcolor() const { return argb_to_qcolor(jump_bridge_color); }
  [[nodiscard]] QColor subsystem_banner_qcolor() const { return argb_to_qcolor(subsystem_banner); }
};

class ThemeManager : public QObject {
  Q_OBJECT

public:
  static ThemeManager& instance();

  void scan_themes(const QString& custom_search_path = "");
  bool load_theme_file(const QString& file_path);

  [[nodiscard]] const std::vector<UITheme>& ui_themes() const noexcept { return ui_themes_; }
  [[nodiscard]] const std::vector<DiagramTheme>& diagram_themes() const noexcept { return diagram_themes_; }

  [[nodiscard]] const UITheme* find_ui_theme(const std::string& id) const noexcept;
  [[nodiscard]] const DiagramTheme* find_diagram_theme(const std::string& id) const noexcept;

  bool set_active_ui_theme(const std::string& id);
  bool set_active_diagram_theme(const std::string& id);

  [[nodiscard]] const UITheme& current_ui_theme() const noexcept { return current_ui_theme_; }
  [[nodiscard]] const DiagramTheme& current_diagram_theme() const noexcept { return current_diagram_theme_; }

signals:
  void ui_theme_changed(const UITheme& theme);
  void diagram_theme_changed(const DiagramTheme& theme);

private:
  explicit ThemeManager(QObject* parent = nullptr);

  std::vector<UITheme> ui_themes_;
  std::vector<DiagramTheme> diagram_themes_;

  UITheme current_ui_theme_;
  DiagramTheme current_diagram_theme_;

  void register_built_in_fallbacks();
};

}  // namespace madi::gui

#endif // MADI_ENABLE_QT

#endif // MADI_GUI_THEME_MANAGER_HPP
