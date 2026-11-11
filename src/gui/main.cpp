// Madí - Model, Align & Diagram Interconnects
// Homage to Arte Madí (Buenos Aires, 1946) — Broken Canvas, Linear Grids & Articulated Spatial Structures
// Copyright (C) 2026 Madí Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/gui/MainWindow.hpp"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <string_view>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("madi");
  app.setApplicationDisplayName("Madí - Model, Align & Diagram Interconnects");
  app.setApplicationVersion("1.0.0-alpha");

  QIcon app_icon = vector::gui::MainWindow::create_app_icon();
  app.setWindowIcon(app_icon);
  app.setDesktopFileName("madi");

  vector::gui::MainWindow window;
  window.setWindowIcon(app_icon);

  if (argc > 1) {
    if (std::string_view(argv[1]) == "--self-test") {
      // Headless self-test check: verify construction and show offscreen
      window.show();
      return 0;
    }
    if (argv[1][0] != '-') {
      window.open_diagram_path(QString::fromLocal8Bit(argv[1]));
    }
  }

  window.show();
  return app.exec();
}
