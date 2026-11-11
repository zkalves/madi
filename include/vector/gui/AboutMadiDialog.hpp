// VECTOR / Madí - Model, Align & Diagram Interconnects
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_GUI_ABOUT_MADI_DIALOG_HPP
#define VECTOR_GUI_ABOUT_MADI_DIALOG_HPP

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSysInfo>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace vector::gui {

class AboutMadiDialog : public QDialog {
  Q_OBJECT

public:
  explicit AboutMadiDialog(QWidget* parent = nullptr) : QDialog(parent) {
    setWindowTitle("About Madí");
    resize(740, 620);
    setMinimumSize(660, 520);

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(18, 18, 18, 16);
    root_layout->setSpacing(14);

    // 1. Top Header Banner with Logo & Badges
    auto* banner_widget = new QWidget(this);
    auto* banner_layout = new QHBoxLayout(banner_widget);
    banner_layout->setContentsMargins(0, 0, 0, 0);
    banner_layout->setSpacing(18);

    auto* logo_lbl = new QLabel(banner_widget);
    logo_lbl->setFixedSize(88, 88);
    logo_lbl->setAlignment(Qt::AlignCenter);

    QPixmap logo_pix;
    if (QFile::exists(":/icons/icon.svg")) {
      logo_pix = QIcon(":/icons/icon.svg").pixmap(84, 84);
    } else if (QFile::exists(":/icons/madi.svg")) {
      logo_pix = QIcon(":/icons/madi.svg").pixmap(84, 84);
    } else if (QFile::exists(":/icons/icon.png")) {
      QPixmap base(":/icons/icon.png");
      if (!base.isNull()) logo_pix = base.scaled(84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (logo_pix.isNull()) {
      logo_pix = QIcon::fromTheme("madi", QIcon(":/icons/icon.svg")).pixmap(84, 84);
    }
    logo_lbl->setPixmap(logo_pix);
    banner_layout->addWidget(logo_lbl);

    auto* title_layout = new QVBoxLayout();
    title_layout->setSpacing(4);
    title_layout->setAlignment(Qt::AlignVCenter);

    auto* title_lbl = new QLabel(
      "<div style='font-size: 26px; font-weight: 800; color: #38BDF8; letter-spacing: 0.5px;'>Madí</div>"
      "<div style='font-size: 13px; font-weight: 600; color: #94A3B8; margin-top: 1px;'>Model, Align &amp; Diagram Interconnects</div>"
      "<div style='margin-top: 6px;'>"
        "<span style='background: #0284C7; color: #FFFFFF; padding: 2px 8px; border-radius: 4px; font-size: 10px; font-weight: bold;'>v1.0.0-alpha</span> &nbsp;"
        "<span style='background: #334155; color: #E2E8F0; padding: 2px 8px; border-radius: 4px; font-size: 10px; font-weight: 600;'>ISO C++20</span> &nbsp;"
        "<span style='background: #334155; color: #E2E8F0; padding: 2px 8px; border-radius: 4px; font-size: 10px; font-weight: 600;'>Qt " QT_VERSION_STR "</span> &nbsp;"
        "<span style='background: #1E293B; color: #38BDF8; border: 1px solid #0284C7; padding: 1px 7px; border-radius: 4px; font-size: 10px; font-weight: bold;'>GNU GPL-3.0</span>"
      "</div>",
      banner_widget
    );
    title_lbl->setTextFormat(Qt::RichText);
    title_layout->addWidget(title_lbl);
    banner_layout->addLayout(title_layout, 1);
    root_layout->addWidget(banner_widget);

    // 2. Tab Widget with 4 Sections
    auto* tab_widget = new QTabWidget(this);
    tab_widget->setStyleSheet(
      "QTabWidget::pane { border: 1px solid #334155; border-radius: 6px; background: #0F172A; } "
      "QTabBar::tab { background: #1E293B; color: #94A3B8; padding: 8px 16px; font-weight: bold; font-size: 11px; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; } "
      "QTabBar::tab:selected { background: #0F172A; color: #38BDF8; border-bottom: 2px solid #38BDF8; } "
      "QTabBar::tab:hover { color: #E2E8F0; background: #334155; }"
    );

    tab_widget->addTab(create_overview_tab(), "Overview & Homage");
    tab_widget->addTab(create_architecture_tab(), "Architecture & Features");
    tab_widget->addTab(create_system_tab(), "System & Build Info");
    tab_widget->addTab(create_license_tab(), "License & Warranty");

    root_layout->addWidget(tab_widget, 1);

    // 3. Dialog Bottom Button Box
    auto* btn_box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btn_box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(btn_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root_layout->addWidget(btn_box);
  }

private:
  QWidget* create_overview_tab() {
    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setStyleSheet("QTextBrowser { background: #0F172A; color: #E2E8F0; border: none; font-size: 12px; line-height: 1.5; padding: 12px; }");

    browser->setHtml(
      "<div style='font-family: sans-serif; line-height: 1.6; color: #CBD5E1;'>"
      "<h3 style='color: #38BDF8; margin-top: 0;'>What is Madí?</h3>"
      "<p><b>Madí</b> (pronounced <i>mah-DEE</i>) is a modern C++20 2D diagramming, schematic, and interconnect engine "
      "designed for electronic design automation (EDA), architectural system modeling, block interconnects, and flowcharts.</p>"

      "<hr style='border: none; border-top: 1px solid #334155; margin: 14px 0;'/>"

      "<h3 style='color: #38BDF8;'>Homage: Arte Madí (Buenos Aires, 1946)</h3>"
      "<p>Madí is named in homage to <b>Arte Madí</b>, the landmark 1946 avant-garde geometric art movement founded in Buenos Aires by "
      "<b>Carmelo Arden Quin</b>, <b>Gyula Kosice</b>, and <b>Rhod Rothfuss</b>. Renowned for intentional wordplay and debated acronyms "
      "(<i>Materialismo Dialéctico</i>, <i>Movimiento de Arte De Invención</i>, or playful phonetic sound poetry), the movement broke radically "
      "from convention and provides the conceptual foundations for our engine:</p>"

      "<ul style='margin-left: -15px;'>"
        "<li><b>Broken Canvas (<i>Marco Recortado</i>):</b> Madí artists eliminated the centuries-old dogma of the static rectangular frame "
        "in favor of polygonal, irregular cutouts. In Madí, this directly translates into an unbounded, freeform, infinite canvas where "
        "geometry and connections define space rather than arbitrary page boundaries.</li>"
        "<li><b>Articulated &amp; Kinetic Structures:</b> They pioneered modular, hinged, transformable artworks whose sub-components could move, "
        "rotate, and nest hierarchically. In Madí, this powers our hierarchical sub-sheet navigation, dual-view EDA symbol/diagram cells, and nested component groups.</li>"
        "<li><b>Linear Grids &amp; Spatial Precision:</b> Strict geometric harmony, orthogonal alignment, and spatial discipline inspire our "
        "Cartesian grid manager, selectable snapping engine, and obstacle-avoiding Manhattan wire routing physics.</li>"
      "</ul>"

      "<hr style='border: none; border-top: 1px solid #334155; margin: 14px 0;'/>"

      "<h3 style='color: #38BDF8;'>Engineering Backronym</h3>"
      "<p style='font-size: 13px; font-weight: bold; color: #F1F5F9; background: #1E293B; padding: 8px 12px; border-left: 3px solid #38BDF8; border-radius: 4px;'>"
        "M &mdash; Model<br>"
        "A &mdash; Align<br>"
        "D &mdash; Diagram<br>"
        "I &mdash; Interconnects"
      "</p>"

      "<p><b>Madí CLI &amp; Native File Format:</b><br>"
      "Command-line invocation: <code>madi</code> &nbsp;|&nbsp; Native document extension: <code>.madi</code> (deterministic human-readable YAML format)."
      "</p>"
      "</div>"
    );
    return browser;
  }

  QWidget* create_architecture_tab() {
    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setStyleSheet("QTextBrowser { background: #0F172A; color: #E2E8F0; border: none; font-size: 12px; line-height: 1.5; padding: 12px; }");

    browser->setHtml(
      "<div style='font-family: sans-serif; line-height: 1.6; color: #CBD5E1;'>"
      "<h3 style='color: #38BDF8; margin-top: 0;'>Architectural Pillars &amp; Engine Capabilities</h3>"

      "<div style='margin-bottom: 12px;'>"
        "<b style='color: #F1F5F9; font-size: 13px;'>1. Zero-Dependency Modern C++20 Core (<code>libvector-core</code> / <code>libmadi-core</code>)</b>"
        "<ul style='margin-top: 4px; margin-left: -15px;'>"
          "<li>Strict ISO C++20 implementation built with standard POSIX APIs, zero proprietary bloat, and full memory safety.</li>"
          "<li>64-bit high-precision floating-point 2D geometric primitives (<code>Vec2d</code>, <code>Rect2d</code>, <code>Polygon</code>, <code>AffineTransform</code>).</li>"
          "<li>Flat Bounding Volume Hierarchy (BVH) spatial indexing for sub-millisecond collision detection across 100,000+ primitives.</li>"
          "<li>Transactional undo/redo engine with composite commands, atomic rollback, and reactive model change listeners.</li>"
        "</ul>"
      "</div>"

      "<div style='margin-bottom: 12px;'>"
        "<b style='color: #F1F5F9; font-size: 13px;'>2. Obstacle-Avoiding Manhattan Routing (<code>libvector-routing</code> / <code>libmadi-routing</code>)</b>"
        "<ul style='margin-top: 4px; margin-left: -15px;'>"
          "<li>High-speed orthogonal A* pathfinding with obstacle inflation buffers and automatic line jump/hop bridges on wire intersections.</li>"
          "<li>Multi-point arrow connectors with dynamically adjustable inflection points and live parametric handles.</li>"
          "<li>Configurable snapping engine: Cartesian grid, object-to-object bounds/centers, and angular constraints (15°, 30°, 45°, 90°).</li>"
        "</ul>"
      "</div>"

      "<div style='margin-bottom: 12px;'>"
        "<b style='color: #F1F5F9; font-size: 13px;'>3. Hierarchical EDA Sub-Sheets &amp; Stencil System</b>"
        "<ul style='margin-top: 4px; margin-left: -15px;'>"
          "<li>Dual-view cell architecture: abstract schematic <code>SymbolView</code> (perimeter pins) linked to internal <code>DiagramView</code>.</li>"
          "<li>Infinite push/pop hierarchical scope navigation stack with breadcrumb trail and status bar scope tracking.</li>"
          "<li>Dynamic typed port attachments (Input, Output, Bidirectional, Bus) with distinct visual representations.</li>"
          "<li>Extensible Stencil Library dock supporting drag-and-drop addition of custom figures and user-defined shape palettes.</li>"
        "</ul>"
      "</div>"

      "<div style='margin-bottom: 12px;'>"
        "<b style='color: #F1F5F9; font-size: 13px;'>4. 2D CSG Boolean Geometry &amp; Parametric Deformation</b>"
        "<ul style='margin-top: 4px; margin-left: -15px;'>"
          "<li>Constructive Solid Geometry (CSG) 2D boolean operations: Union, Intersection, Difference (Subtraction), and Symmetric Difference (XOR).</li>"
          "<li>Parametric control points enabling interactive real-time reshaping of polygons and arrow heads directly on the canvas.</li>"
        "</ul>"
      "</div>"

      "<div style='margin-bottom: 12px;'>"
        "<b style='color: #F1F5F9; font-size: 13px;'>5. Deterministic Persistence &amp; Draw.io Interoperability (<code>libvector-io</code> / <code>libmadi-io</code>)</b>"
        "<ul style='margin-top: 4px; margin-left: -15px;'>"
          "<li>Deterministic human-readable YAML file format (<code>.madi</code>) optimized for clean git diffs and version control.</li>"
          "<li>Bidirectional lossless Draw.io XML parser and exporter preserving styling, hierarchy, and geometric connectivity.</li>"
        "</ul>"
      "</div>"

      "<div style='margin-bottom: 8px;'>"
        "<b style='color: #F1F5F9; font-size: 13px;'>6. Viewport Engine &amp; Ergonomics (<code>madi-gui</code>)</b>"
        "<ul style='margin-top: 4px; margin-left: -15px;'>"
          "<li>Hardware-accelerated viewport with Qt 6 fallback, sub-pixel antialiasing, and smooth zooming from 10% to 400%.</li>"
          "<li>Comprehensive theme suite: Solarized Dark/Light, Monokai, Nord, Dracula, High-Contrast Dark, and Arte Madí.</li>"
          "<li>Interactive Command Console dock for live programmatic geometry manipulation and shape scripting.</li>"
          "<li>Notification area system tray integration with instant window restoration and quick actions.</li>"
        "</ul>"
      "</div>"
      "</div>"
    );
    return browser;
  }

  QWidget* create_system_tab() {
    auto* table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"System / Build Property", "Configuration Value"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet(
      "QTableWidget { background: #0F172A; alternate-background-color: #1E293B; color: #E2E8F0; border: none; font-size: 12px; } "
      "QTableWidget::item { padding: 6px 10px; border-bottom: 1px solid #1E293B; } "
      "QHeaderView::section { background: #1E293B; color: #94A3B8; font-weight: bold; border: none; padding: 6px 10px; }"
    );

    auto add_row = [table](const QString& prop, const QString& val) {
      int row = table->rowCount();
      table->insertRow(row);
      auto* item_prop = new QTableWidgetItem(prop);
      item_prop->setForeground(QColor("#94A3B8"));
      auto font = item_prop->font();
      font.setBold(true);
      item_prop->setFont(font);

      auto* item_val = new QTableWidgetItem(val);
      item_val->setForeground(QColor("#38BDF8"));
      table->setItem(row, 0, item_prop);
      table->setItem(row, 1, item_val);
    };

    add_row("Application Name", "Madí (madi-gui)");
    add_row("CLI Binary Target", "madi");
    add_row("Release Version", "1.0.0-alpha");
    add_row("C++ Language Standard", QString("ISO C++20 (__cplusplus = %1)").arg(__cplusplus));
    add_row("Qt Version (Compiled)", QT_VERSION_STR);
    add_row("Qt Version (Runtime)", qVersion());
    add_row("Operating System", QSysInfo::prettyProductName());
    add_row("Kernel Type & Version", QString("%1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion()));
    add_row("CPU Architecture", QSysInfo::buildCpuArchitecture());
    add_row("Target System ABI", QSysInfo::buildAbi());
    add_row("System Hostname", QSysInfo::machineHostName());
    add_row("Windowing Platform", QGuiApplication::platformName());
    add_row("Process Identifier (PID)", QString::number(QCoreApplication::applicationPid()));
    add_row("Spatial Indexing Backend", "Flat Bounding Volume Hierarchy (FlatBvh)");
    add_row("Routing Engine", "Autonomous Obstacle-Avoiding Manhattan A*");
    add_row("Theme Engine", "ThemeManager (Solarized Dark default)");
    add_row("Desktop Integration", "madi.desktop (StartupWMClass=madi-gui)");
    add_row("License", "GNU General Public License v3.0 (GPL-3.0)");

    return table;
  }

  QWidget* create_license_tab() {
    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setStyleSheet("QTextBrowser { background: #0F172A; color: #CBD5E1; border: none; font-size: 11px; font-family: monospace; line-height: 1.5; padding: 12px; }");

    browser->setHtml(
      "<div style='font-family: monospace; white-space: pre-wrap;'>"
      "<b>Madí - Model, Align &amp; Diagram Interconnects</b><br>"
      "Copyright (C) 2026 Madí Contributors. All rights reserved.<br><br>"
      "This program is free software: you can redistribute it and/or modify "
      "it under the terms of the <b>GNU General Public License</b> as published by "
      "the Free Software Foundation, either version 3 of the License, or "
      "(at your option) any later version.<br><br>"
      "This program is distributed in the hope that it will be useful, "
      "but <b>WITHOUT ANY WARRANTY</b>; without even the implied warranty of "
      "<b>MERCHANTABILITY</b> or <b>FITNESS FOR A PARTICULAR PURPOSE</b>. See the "
      "GNU General Public License for more details.<br><br>"
      "You should have received a copy of the GNU General Public License "
      "along with this program. If not, see &lt;<a style='color: #38BDF8;' href='https://www.gnu.org/licenses/gpl-3.0.html'>https://www.gnu.org/licenses/gpl-3.0.html</a>&gt;."
      "</div>"
    );
    return browser;
  }
};

} // namespace vector::gui

#endif // VECTOR_GUI_ABOUT_MADI_DIALOG_HPP
