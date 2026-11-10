// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vector/gui/MainWindow.hpp"

#include <QApplication>
#include <string_view>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("VECTOR");
  app.setApplicationDisplayName("VECTOR - Viewport Engine");

  vector::gui::MainWindow window;

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
