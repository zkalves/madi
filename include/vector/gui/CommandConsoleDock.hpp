// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_GUI_COMMAND_CONSOLE_DOCK_HPP
#define VECTOR_GUI_COMMAND_CONSOLE_DOCK_HPP

#include <QCompleter>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStringListModel>
#include <QTextEdit>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "vector/core/Geometry.hpp"
#include "vector/core/GraphModel.hpp"
#include "vector/core/ParametricShape.hpp"
#include "vector/core/SymbolGenerator.hpp"
#include "vector/renderer/CanvasWidget.hpp"

namespace vector::gui {

class CommandLineEdit : public QLineEdit {
  Q_OBJECT

public:
  explicit CommandLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {}

  void set_history(std::vector<QString>* history) {
    history_ = history;
    history_pos_ = history ? static_cast<int>(history->size()) : 0;
  }

  void reset_history_pos() {
    if (history_) {
      history_pos_ = static_cast<int>(history_->size());
    }
  }

signals:
  void escape_pressed();
  void tab_completion_requested();

protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Up) {
      if (history_ && !history_->empty()) {
        if (history_pos_ > 0) {
          history_pos_--;
          setText((*history_)[static_cast<size_t>(history_pos_)]);
        }
      }
      event->accept();
      return;
    } else if (event->key() == Qt::Key_Down) {
      if (history_ && !history_->empty()) {
        if (history_pos_ + 1 < static_cast<int>(history_->size())) {
          history_pos_++;
          setText((*history_)[static_cast<size_t>(history_pos_)]);
        } else {
          history_pos_ = static_cast<int>(history_->size());
          clear();
        }
      }
      event->accept();
      return;
    } else if (event->key() == Qt::Key_Escape) {
      emit escape_pressed();
      event->accept();
      return;
    } else if (event->key() == Qt::Key_Tab) {
      emit tab_completion_requested();
      event->accept();
      return;
    }
    QLineEdit::keyPressEvent(event);
  }

private:
  std::vector<QString>* history_{nullptr};
  int history_pos_{0};
};

class CommandConsoleDock : public QDockWidget {
  Q_OBJECT

public:
  using FileActionCallback = std::function<void(const QString&)>;
  using SettingsActionCallback = std::function<void(int)>;
  using NewActionCallback = std::function<void()>;
  using SymbolEditActionCallback = std::function<void(const QString&)>;

  explicit CommandConsoleDock(QWidget* parent = nullptr)
      : QDockWidget("Command Console", parent) {
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    auto* container = new QWidget(this);
    container->setObjectName("consoleContainer");
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // 1. Output History Text View
    history_view_ = new QTextEdit(container);
    history_view_->setReadOnly(true);
    history_view_->setStyleSheet(
        "QTextEdit {"
        "  background-color: #0E0F17;"
        "  color: #D4D4D8;"
        "  font-family: 'JetBrains Mono', 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace;"
        "  font-size: 11px;"
        "  border: 1px solid #27272A;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "}");
    history_view_->setMinimumHeight(70);
    layout->addWidget(history_view_);

    // 2. Command Prompt & Input Line
    auto* input_row = new QHBoxLayout();
    input_row->setContentsMargins(0, 0, 0, 0);
    input_row->setSpacing(6);

    prompt_label_ = new QLabel("Command:", container);
    prompt_label_->setStyleSheet(
        "QLabel {"
        "  color: #38BDF8;"
        "  font-family: 'JetBrains Mono', 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace;"
        "  font-weight: bold;"
        "  font-size: 12px;"
        "}");
    input_row->addWidget(prompt_label_);

    input_line_ = new CommandLineEdit(container);
    input_line_->setObjectName("commandInputLine");
    input_line_->setStyleSheet(
        "QLineEdit {"
        "  background-color: #1A1B26;"
        "  color: #F8FAFC;"
        "  font-family: 'JetBrains Mono', 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  border: 1px solid #3B82F6;"
        "  border-radius: 4px;"
        "  padding: 3px 6px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1.5px solid #60A5FA;"
        "  background-color: #1E2030;"
        "}");
    input_line_->setPlaceholderText("Type a command (line, rect, poly, block, down, up, help)...");
    input_line_->set_history(&command_history_);
    input_row->addWidget(input_line_);

    layout->addLayout(input_row);
    setWidget(container);

    // Setup autocomplete keywords
    setup_autocomplete();

    connect(input_line_, &QLineEdit::returnPressed, this, &CommandConsoleDock::handle_return_pressed);
    connect(input_line_, &CommandLineEdit::escape_pressed, this, &CommandConsoleDock::cancel_command);
    connect(input_line_, &CommandLineEdit::tab_completion_requested, this, &CommandConsoleDock::handle_tab_completion);

    print_welcome_banner();
  }

  void set_canvas(vector::renderer::CanvasWidget* canvas) noexcept {
    canvas_ = canvas;
  }

  void set_grid_manager(vector::grid::GridManager* grid) noexcept {
    grid_manager_ = grid;
  }

  void set_new_callback(NewActionCallback cb) { new_cb_ = std::move(cb); }
  void set_save_callback(FileActionCallback cb) { save_cb_ = std::move(cb); }
  void set_open_callback(FileActionCallback cb) { open_cb_ = std::move(cb); }
  void set_settings_callback(SettingsActionCallback cb) { settings_cb_ = std::move(cb); }
  void set_symbol_edit_callback(SymbolEditActionCallback cb) { symbol_edit_cb_ = std::move(cb); }

  using ExportActionCallback = std::function<void(const QString& format, const QString& path)>;
  void set_export_callback(ExportActionCallback cb) { export_cb_ = std::move(cb); }

  using PersistenceGetCallback = std::function<bool()>;
  using PersistenceSetCallback = std::function<void(bool)>;
  using ResetLayoutCallback = std::function<void()>;

  void set_persistence_callbacks(PersistenceGetCallback get_cb,
                                 PersistenceSetCallback set_cb,
                                 ResetLayoutCallback reset_cb) {
    persist_get_cb_ = std::move(get_cb);
    persist_set_cb_ = std::move(set_cb);
    reset_layout_cb_ = std::move(reset_cb);
  }

  void focus_input() {
    input_line_->setFocus();
    input_line_->selectAll();
  }

  void execute_command(const QString& cmd) {
    log_cmd(cmd);
    command_history_.push_back(cmd);
    process_single_command(cmd);
  }

  void log_info(const QString& text) {
    history_view_->append(QString("<span style='color: #4ADE80;'>%1</span>").arg(text.toHtmlEscaped()));
    scroll_to_bottom();
  }

  void log_warn(const QString& text) {
    history_view_->append(QString("<span style='color: #FBBF24;'>%1</span>").arg(text.toHtmlEscaped()));
    scroll_to_bottom();
  }

  void log_error(const QString& text) {
    history_view_->append(QString("<span style='color: #F87171; font-weight: bold;'>%1</span>").arg(text.toHtmlEscaped()));
    scroll_to_bottom();
  }

  void log_cmd(const QString& text) {
    history_view_->append(QString("<span style='color: #38BDF8;'><b>&gt;</b> %1</span>").arg(text.toHtmlEscaped()));
    scroll_to_bottom();
  }

private:
  void print_welcome_banner() {
    history_view_->append("<span style='color: #A78BFA;'><b>Madí Command Console</b></span>");
    history_view_->append("<span style='color: #94A3B8;'>Type <b>help</b> for command reference. Supports <i>line, rect, poly, block, down, up, move, delete, zoom</i>.</span>");
    history_view_->append("<span style='color: #64748B;'>Coordinates support absolute <b>x,y</b> and relative <b>@dx,dy</b> (e.g. <i>line 100,100 @150,0</i>).</span>");
  }

  void setup_autocomplete() {
    keywords_ = {
      "new",
      "add",
      "modify", "set", "prop", "property",
      "point", "pt", "controlpoint",
      "segmove", "segmentmove", "seg",
      "line", "l", "wire", "w",
      "rect", "rec", "rectangle", "box",
      "circle", "c", "ellipse",
      "poly", "polygon",
      "inport", "in", "outport", "out",
      "subsystem", "sub", "block",
      "symbol", "sym", "drift",
      "down", "push", "drill",
      "up", "pop",
      "merge", "union", "or", "group", "g",
      "intersect", "intersection", "and",
      "subtract", "diff", "difference",
      "xor", "symdiff", "bool", "boolean",
      "split", "ungroup", "u",
      "front", "bringtofront", "back", "sendtoback",
      "forward", "bringforward", "backward", "sendbackward", "order",
      "move", "m",
      "delete", "del", "erase", "e",
      "select", "sel",
      "zoom", "z",
      "pan", "p",
      "grid", "snap", "snapunit", "dsettings", "settings",
      "pagesize", "page", "pagesetup", "pagesheet", "pagemargin",
      "remember", "rememberlayout", "resetlayout",
      "save", "open", "export",
      "font", "fill", "stroke", "textcolor", "fontcolor", "style",
      "clear", "cls",
      "help", "?"
    };
  }

  void scroll_to_bottom() {
    auto cursor = history_view_->textCursor();
    cursor.movePosition(QTextCursor::End);
    history_view_->setTextCursor(cursor);
  }

  void cancel_command() {
    active_command_.clear();
    command_step_ = 0;
    step_points_.clear();
    prompt_label_->setText("Command:");
    input_line_->clear();
    log_info("*Cancel*");
  }

  void handle_tab_completion() {
    const QString current = input_line_->text().trimmed();
    if (current.isEmpty()) return;

    for (const auto& kw : keywords_) {
      if (kw.startsWith(current, Qt::CaseInsensitive) && kw != current) {
        input_line_->setText(kw + " ");
        input_line_->setCursorPosition(kw.length() + 1);
        return;
      }
    }
  }

  void handle_return_pressed() {
    QString raw_text = input_line_->text().trimmed();
    input_line_->clear();

    if (raw_text.isEmpty()) {
      if (!last_executed_command_.isEmpty() && active_command_.isEmpty()) {
        raw_text = last_executed_command_;
      } else if (!active_command_.isEmpty()) {
        // Empty enter during multi-step command finishes/cancels
        cancel_command();
        return;
      } else {
        return;
      }
    }

    if (active_command_.isEmpty()) {
      log_cmd(raw_text);
      command_history_.push_back(raw_text);
      input_line_->reset_history_pos();
      last_executed_command_ = raw_text;
      process_single_command(raw_text);
    } else {
      log_cmd(prompt_label_->text() + " " + raw_text);
      process_interactive_step(raw_text);
    }
  }

  std::vector<QString> tokenize(const QString& str) {
    std::vector<QString> tokens;
    QString cur;
    bool in_quote = false;

    for (int i = 0; i < str.length(); ++i) {
      const QChar c = str[i];
      if (c == '"' || c == '\'') {
        in_quote = !in_quote;
      } else if (c.isSpace() && !in_quote) {
        if (!cur.isEmpty()) {
          tokens.push_back(cur);
          cur.clear();
        }
      } else {
        cur.append(c);
      }
    }
    if (!cur.isEmpty()) {
      tokens.push_back(cur);
    }
    return tokens;
  }

  std::optional<core::Vec2d> parse_point(const QString& token, const core::Vec2d& ref = {0.0, 0.0}) {
    QString s = token;
    bool relative = false;
    if (s.startsWith('@')) {
      relative = true;
      s = s.mid(1);
    }

    auto parts = s.split(',');
    if (parts.size() != 2) {
      return std::nullopt;
    }
    bool ok1 = false, ok2 = false;
    double x = parts[0].trimmed().toDouble(&ok1);
    double y = parts[1].trimmed().toDouble(&ok2);
    if (!ok1 || !ok2) return std::nullopt;

    if (relative) {
      return core::Vec2d{ref.x + x, ref.y + y};
    }
    return core::Vec2d{x, y};
  }

  void process_single_command(const QString& full_cmd) {
    auto tokens = tokenize(full_cmd);
    if (tokens.empty()) return;

    const QString cmd = tokens[0].toLower();

    // 1. HELP / ?
    if (cmd == "help" || cmd == "?") {
      history_view_->append("<span style='color: #FDE047;'><b>--- Command Reference ---</b></span>");
      history_view_->append("<b>line | l</b> &lt;x1,y1&gt; &lt;x2,y2&gt;  -  Create wire or connector between points");
      history_view_->append("<b>rect | rec</b> &lt;x,y&gt; &lt;w,h&gt; [name]  -  Create rectangle block");
      history_view_->append("<b>circle | c</b> &lt;x,y&gt; &lt;r&gt; [name]  -  Create ellipse/circle figure");
      history_view_->append("<b>poly</b> &lt;sides&gt; &lt;x,y&gt; &lt;r&gt; [name]  -  Create regular polygon (3=tri, 4=diamond, 6=hex, 8=oct)");
      history_view_->append("<b>subsystem | block</b> &lt;name&gt; [--file path] [x,y] [w,h]  -  Create built-in or file subsystem");
      history_view_->append("<b>inport | in</b> [name] [x,y]  -  Add hierarchical input port");
      history_view_->append("<b>outport | out</b> [name] [x,y]  -  Add hierarchical output port");
      history_view_->append("<b>down | push</b> [name]  -  Drill down into subsystem block internal diagram");
      history_view_->append("<b>up | pop</b>  -  Return up to parent diagram level");
      history_view_->append("<b>symbol | sym</b> &lt;edit|sync|fit&gt; [name]  -  Interactive symbol editor, pin sync, dimension auto-fit");
      history_view_->append("<b>drift</b> &lt;on|off|status&gt; [name]  -  Configure definition drift mode (allow drift vs follow primary)");
      history_view_->append("<b>merge | union | or</b>  -  Boolean OR: Combine selected figures into a unified polygon");
      history_view_->append("<b>intersect | and</b>  -  Boolean AND: Keep overlapping region of figures");
      history_view_->append("<b>subtract | diff</b>  -  Boolean Difference: Cut subsequent figures out of first figure");
      history_view_->append("<b>xor</b>  -  Boolean XOR: Keep non-overlapping regions");
      history_view_->append("<b>bool</b> &lt;union|intersect|subtract|xor&gt;  -  Execute specified boolean logic operation");
      history_view_->append("<b>split | u</b>  -  Split selected composite figure");
      history_view_->append("<b>front | bring to front</b>  -  Move selected figure(s) to the very top");
      history_view_->append("<b>back | send to back</b>  -  Move selected figure(s) to the very bottom");
      history_view_->append("<b>forward | bring forward</b>  -  Move selected figure(s) one step up");
      history_view_->append("<b>backward | send backward</b>  -  Move selected figure(s) one step down");
      history_view_->append("<b>order</b> &lt;front|back|forward|backward&gt;  -  Change stacking order of selection");
      history_view_->append("<b>move | m</b> &lt;dx,dy&gt;  -  Translate selected elements");
      history_view_->append("<b>delete | del</b> [name]  -  Delete selection or named figure");
      history_view_->append("<b>select | sel</b> &lt;all|none|name&gt;  -  Select objects");
      history_view_->append("<b>zoom | z</b> &lt;in|out|extents|factor&gt;  -  Adjust zoom level");
      history_view_->append("<b>pan | p</b> &lt;dx,dy&gt;  -  Pan camera");
      history_view_->append("<b>grid</b> &lt;on|off&gt;  -  Toggle grid display");
      history_view_->append("<b>snap</b> &lt;off|weak|normal|strong|pixels&gt;  -  Configure snap sensitivity");
      history_view_->append("<b>pagesize</b> &lt;preset|w h&gt; [portrait|landscape]  -  Set page dimensions");
      history_view_->append("<b>remember</b> &lt;on|off&gt;  -  Configure window positions & settings persistence");
      history_view_->append("<b>resetlayout</b>  -  Reset window size and dock layout to default");
      history_view_->append("<b>save | open</b> [path]  -  Save or load diagram file");
      history_view_->append("<b>export</b> &lt;svg|png|jpg|xml|drawio&gt; [path]  -  Export diagram to image or XML");
      history_view_->append("<b>font</b> &lt;size|family|bold|italic|underline|regular&gt; [val]  -  Set typography on selection");
      history_view_->append("<b>fill</b> &lt;#hex|color&gt;  -  Set shape fill color on selection");
      history_view_->append("<b>stroke</b> &lt;#hex|color|width&gt;  -  Set stroke color or line width");
      history_view_->append("<b>textcolor</b> &lt;#hex|color&gt;  -  Set label text color on selection");
      history_view_->append("<b>style reset</b>  -  Reset selected nodes appearance to theme defaults");
      history_view_->append("<b>branchalign</b> &lt;on|off|toggle&gt;  -  Specify if lines on selected multipoint arrow are aligned");
      history_view_->append("<b>add</b> &lt;rect|circle|poly|arrow|line|block|inport|outport|point&gt; [args...]  -  Create shape, net, or control point via code");
      history_view_->append("<b>modify | set</b> [name] &lt;x|y|pos|w|h|size|rot|name|text|fill|stroke|textcolor&gt; &lt;val&gt;  -  Modify shape properties via code");
      history_view_->append("<b>point | pt</b> &lt;list|add &lt;x,y&gt;|del &lt;index&gt;|set &lt;index&gt; &lt;x,y&gt;&gt;  -  Inspect or manipulate control points with code");
      history_view_->append("<b>segmove</b> &lt;on|off|toggle&gt;  -  Enable/disable edge segment moving (disabled by default; hold Shift to drag segments)");
      history_view_->append("<b>clear | cls</b>  -  Clear console history");
      scroll_to_bottom();
      return;
    }

    // 2. CLEAR / CLS
    if (cmd == "clear" || cmd == "cls") {
      history_view_->clear();
      print_welcome_banner();
      return;
    }

    if (!canvas_) {
      log_error("Canvas engine not connected.");
      return;
    }

    // 0a. ADD (Code instantiation: add <rect|circle|poly|arrow|line|block|inport|outport|point>)
    if (cmd == "add") {
      if (tokens.size() >= 2) {
        const QString sub = tokens[1].toLower();
        if (sub == "point" || sub == "pt" || sub == "controlpoint") {
          std::vector<QString> sub_tokens(tokens.begin() + 1, tokens.end());
          process_point_command(sub_tokens);
          return;
        } else if (sub == "arrow" || sub == "chevron" || sub == "arrowblock") {
          std::vector<QString> sub_tokens(tokens.begin() + 1, tokens.end());
          process_arrow_command(sub_tokens);
          return;
        } else {
          QString rest_cmd;
          for (size_t i = 1; i < tokens.size(); ++i) {
            if (i > 1) rest_cmd.append(" ");
            rest_cmd.append(tokens[i]);
          }
          process_single_command(rest_cmd);
          return;
        }
      } else {
        log_error("Usage: add <rect|circle|poly|arrow|line|block|inport|outport|point> [args...]");
        return;
      }
    }

    // 0b. MODIFY / SET / PROP (Modify shape properties via code)
    if (cmd == "modify" || cmd == "set" || cmd == "prop") {
      process_modify_command(tokens);
      return;
    }

    // 0c. POINT / PT / CONTROLPOINT (Inspect or manipulate control points via code)
    if (cmd == "point" || cmd == "pt" || cmd == "controlpoint") {
      process_point_command(tokens);
      return;
    }

    // 0d. SEGMOVE / SEGMENTMOVE / SEG (Control edge segment moving behavior)
    if (cmd == "segmove" || cmd == "segmentmove" || cmd == "seg") {
      process_segmove_command(tokens);
      return;
    }

    // WIRESTYLE / CROSSINGSTYLE
    if (cmd == "wirestyle" || cmd == "crossingstyle") {
      if (tokens.size() >= 2) {
        const QString sub = tokens[1].toLower();
        if (sub == "dot" || sub == "modern" || sub == "1") {
          canvas_->set_wire_crossing_style(vector::renderer::WireCrossingStyle::DotPriority);
          log_info("Wire crossing style set to: Style 1 (Modern Dot Priority - crossings pass over unless dot present)");
          return;
        } else if (sub == "loop" || sub == "bridge" || sub == "legacy" || sub == "2") {
          canvas_->set_wire_crossing_style(vector::renderer::WireCrossingStyle::BridgeLoopPriority);
          log_info("Wire crossing style set to: Style 2 (Legacy Bridge Loop Priority - crossings connect unless loop jumps)");
          return;
        }
      }
      log_info(QString("Current wire crossing style: %1. Options: wirestyle <dot|loop>")
          .arg(canvas_->wire_crossing_style() == vector::renderer::WireCrossingStyle::DotPriority ? "Style 1 (Dot Priority)" : "Style 2 (Bridge Loop Priority)"));
      return;
    }

    // ARROW / CHEVRON / ARROWBLOCK (Arrow Shape Figure OR Auto-routed Arrow Net)
    if (cmd == "arrow" || cmd == "chevron" || cmd == "arrowblock") {
      process_arrow_command(tokens);
      return;
    }

    // BRANCH (Multipoint arrow branch)
    if (cmd == "branch") {
      if (tokens.size() >= 2) {
        auto* gm = canvas_->graph_model();
        if (gm) {
          const auto* n = gm->find_node_by_name(tokens[1].toStdString());
          core::NodeId nid = n ? n->id : tokens[1].toULongLong();
          if (gm->find_node(nid)) {
            core::PortId pid = (tokens.size() >= 3) ? tokens[2].toULongLong() : core::InvalidPortId;
            if (canvas_->add_arrow_branch_to_selected(nid, pid)) {
              log_info(QString("Added multipoint arrow branch to node %1").arg(nid));
              return;
            } else {
              log_error("Select an arrow first using mouse or 'select Net <id>'");
              return;
            }
          }
        }
      }
      log_error("Usage: branch <target_node> [target_port]");
      return;
    }

    // BRANCHALIGN / ALIGNBRANCHES
    if (cmd == "branchalign" || cmd == "alignbranches") {
      core::EdgeId eid = canvas_->selected_edge();
      if (eid == core::InvalidEdgeId) {
        log_error("No arrow or wire currently selected. Select one first.");
        return;
      }
      if (tokens.size() >= 2) {
        const QString opt = tokens[1].toLower();
        bool align = (opt == "on" || opt == "true" || opt == "1" || opt == "aligned" || opt == "yes");
        canvas_->set_edge_branches_aligned(eid, align);
        log_info(QString("Multipoint arrow Net %1 branch alignment set to: %2")
            .arg(eid)
            .arg(align ? "ALIGNED (common bus spine)" : "INDEPENDENT (closest tap)"));
        return;
      } else {
        bool cur = canvas_->is_edge_branches_aligned(eid);
        canvas_->set_edge_branches_aligned(eid, !cur);
        log_info(QString("Toggled multipoint arrow Net %1 branch alignment to: %2")
            .arg(eid)
            .arg(!cur ? "ALIGNED (common bus spine)" : "INDEPENDENT (closest tap)"));
        return;
      }
    }

    // 3. LINE / L / WIRE / W
    if (cmd == "line" || cmd == "l" || cmd == "wire" || cmd == "w") {
      if (tokens.size() >= 3) {
        auto p1 = parse_point(tokens[1]);
        if (!p1) {
          // Check if connecting by node names
          auto* gm = canvas_->graph_model();
          if (gm) {
            auto* n1 = gm->find_node_by_name(tokens[1].toStdString());
            auto* n2 = gm->find_node_by_name(tokens[2].toStdString());
            if (n1 && n2) {
              canvas_->add_wire_by_nodes(n1->id, n2->id);
              log_info(QString("Connected node '%1' to '%2'").arg(tokens[1], tokens[2]));
              return;
            }
          }
          log_error("Invalid point coordinate format: " + tokens[1] + ". Use x,y (e.g. 100,200).");
          return;
        }
        auto p2 = parse_point(tokens[2], *p1);
        if (!p2) {
          log_error("Invalid second point format: " + tokens[2]);
          return;
        }
        canvas_->add_wire_by_points(*p1, *p2);
        log_info(QString("Line created from (%1, %2) to (%3, %4)").arg(p1->x).arg(p1->y).arg(p2->x).arg(p2->y));
      } else {
        // Start interactive line mode
        active_command_ = "line";
        command_step_ = 1;
        prompt_label_->setText("Specify start point (x,y):");
      }
      return;
    }

    // 4. RECT / REC / RECTANGLE / BOX
    if (cmd == "rect" || cmd == "rec" || cmd == "rectangle" || cmd == "box") {
      if (tokens.size() >= 3) {
        auto pt = parse_point(tokens[1]);
        if (!pt) {
          log_error("Invalid corner point format: " + tokens[1]);
          return;
        }
        auto dims = parse_point(tokens[2]);
        if (!dims) {
          log_error("Invalid dimension format: " + tokens[2] + ". Use w,h (e.g. 180,90).");
          return;
        }
        const std::string name = (tokens.size() >= 4) ? tokens[3].toStdString() : "";
        core::NodeId nid = canvas_->add_shape(core::ShapeType::Rectangle, {pt->x + dims->x * 0.5, pt->y + dims->y * 0.5}, name);
        if (auto* n = canvas_->graph_model()->find_node(nid)) {
          n->bounds = {pt->x, pt->y, std::abs(dims->x), std::abs(dims->y)};
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
        log_info(QString("Rectangle created at (%1, %2) size %3x%4").arg(pt->x).arg(pt->y).arg(dims->x).arg(dims->y));
      } else {
        active_command_ = "rect";
        command_step_ = 1;
        prompt_label_->setText("Specify first corner (x,y):");
      }
      return;
    }

    // 5. CIRCLE / C / ELLIPSE
    if (cmd == "circle" || cmd == "c" || cmd == "ellipse") {
      if (tokens.size() >= 3) {
        auto pt = parse_point(tokens[1]);
        if (!pt) {
          log_error("Invalid center coordinate: " + tokens[1]);
          return;
        }
        double r = tokens[2].toDouble();
        if (r <= 0) r = 40.0;
        const std::string name = (tokens.size() >= 4) ? tokens[3].toStdString() : "";
        core::NodeId nid = canvas_->add_shape(core::ShapeType::Ellipse, *pt, name);
        if (auto* n = canvas_->graph_model()->find_node(nid)) {
          n->bounds = {pt->x - r, pt->y - r, r * 2.0, r * 2.0};
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
        log_info(QString("Circle created at (%1, %2) radius %3").arg(pt->x).arg(pt->y).arg(r));
      } else {
        active_command_ = "circle";
        command_step_ = 1;
        prompt_label_->setText("Specify center point (x,y):");
      }
      return;
    }

    // 6. POLY / POLYGON
    if (cmd == "poly" || cmd == "polygon") {
      if (tokens.size() >= 4) {
        int sides = tokens[1].toInt();
        if (sides < 3) sides = 6;
        auto pt = parse_point(tokens[2]);
        if (!pt) {
          log_error("Invalid center point: " + tokens[2]);
          return;
        }
        double r = tokens[3].toDouble();
        if (r <= 0) r = 50.0;
        const std::string name = (tokens.size() >= 5) ? tokens[4].toStdString() : "";
        core::NodeId nid = canvas_->add_polygon(*pt, sides, name);
        if (auto* n = canvas_->graph_model()->find_node(nid)) {
          n->bounds = {pt->x - r, pt->y - r, r * 2.0, r * 2.0};
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
        log_info(QString("Polygon (%1 sides) created at (%2, %3) radius %4").arg(sides).arg(pt->x).arg(pt->y).arg(r));
      } else {
        active_command_ = "poly";
        command_step_ = 1;
        prompt_label_->setText("Enter number of sides <6>:");
      }
      return;
    }

    // 7. SUBSYSTEM / BLOCK / SUB
    if (cmd == "subsystem" || cmd == "block" || cmd == "sub") {
      QString sname = "Subsystem";
      bool is_file = false;
      QString fpath = "";
      core::Vec2d center = canvas_->visible_world_frustum().center();

      for (size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i] == "--file" || tokens[i] == "-f") {
          is_file = true;
          if (i + 1 < tokens.size()) {
            fpath = tokens[++i];
          }
        } else if (auto pt = parse_point(tokens[i])) {
          center = *pt;
        } else {
          sname = tokens[i];
        }
      }

      if (is_file && fpath.isEmpty()) {
        fpath = sname + ".yaml";
      }

      core::NodeId nid = canvas_->add_subsystem(center, sname.toStdString(), is_file, fpath.toStdString());
      (void)nid;
      log_info(QString("Subsystem '%1' created at (%2, %3) [%4]")
          .arg(sname).arg(static_cast<int>(center.x)).arg(static_cast<int>(center.y))
          .arg(is_file ? QString("File: %1").arg(fpath) : "Built-in"));
      return;
    }

    // 7b. INPORT / IN
    if (cmd == "inport" || cmd == "in") {
      QString pname = (tokens.size() >= 2) ? tokens[1] : "";
      core::Vec2d pos = canvas_->visible_world_frustum().center();
      if (tokens.size() >= 3) {
        auto coords = tokens[2].split(',');
        if (coords.size() == 2) {
          pos.x = coords[0].toDouble();
          pos.y = coords[1].toDouble();
        }
      }
      core::NodeId nid = canvas_->add_hierarchical_port(pos, true, pname.toStdString());
      (void)nid;
      log_info("Created Input Port: " + (pname.isEmpty() ? "in_0" : pname));
      return;
    }

    // 7c. OUTPORT / OUT
    if (cmd == "outport" || cmd == "out") {
      QString pname = (tokens.size() >= 2) ? tokens[1] : "";
      core::Vec2d pos = canvas_->visible_world_frustum().center();
      if (tokens.size() >= 3) {
        auto coords = tokens[2].split(',');
        if (coords.size() == 2) {
          pos.x = coords[0].toDouble();
          pos.y = coords[1].toDouble();
        }
      }
      core::NodeId nid = canvas_->add_hierarchical_port(pos, false, pname.toStdString());
      (void)nid;
      log_info("Created Output Port: " + (pname.isEmpty() ? "out_0" : pname));
      return;
    }

    // 8. DOWN / PUSH / DRILL
    if (cmd == "down" || cmd == "push" || cmd == "drill") {
      if (tokens.size() >= 2) {
        std::string target = tokens[1].toStdString();
        auto* gm = canvas_->graph_model();
        if (gm) {
          if (auto* n = gm->find_node_by_name(target)) {
            canvas_->select_node(n->id, true);
            canvas_->drill_down_node(n->id);
            log_info("Drilled down into subsystem: " + tokens[1]);
            return;
          }
        }
        log_error("Subsystem node not found: " + tokens[1]);
        return;
      }
      if (!canvas_->drill_down_selected()) {
        log_warn("Select a subsystem block to drill down into.");
      }
      return;
    }

    // 9. UP / POP
    if (cmd == "up" || cmd == "pop") {
      if (!canvas_->pop_hierarchy_scope()) {
        log_warn("Already at top root diagram level.");
      }
      return;
    }

    // 10. MERGE / UNION / OR / GROUP
    if (cmd == "merge" || cmd == "union" || cmd == "or" || cmd == "group" || cmd == "g") {
      if (!canvas_->boolean_combine_selected(vector::core::BooleanOp::Union)) {
        log_warn("Select 2 or more figures to combine (Union / OR).");
      }
      return;
    }

    // 10b. INTERSECT / AND
    if (cmd == "intersect" || cmd == "intersection" || cmd == "and") {
      if (!canvas_->boolean_combine_selected(vector::core::BooleanOp::Intersection)) {
        log_warn("Select 2 or more figures to intersect (AND). Ensure figures overlap.");
      }
      return;
    }

    // 10c. SUBTRACT / DIFFERENCE / NOT
    if (cmd == "subtract" || cmd == "diff" || cmd == "difference" || cmd == "sub") {
      if (!canvas_->boolean_combine_selected(vector::core::BooleanOp::Difference)) {
        log_warn("Select 2 or more figures to subtract (Difference).");
      }
      return;
    }

    // 10d. XOR / SYMMETRIC DIFFERENCE
    if (cmd == "xor" || cmd == "symdiff") {
      if (!canvas_->boolean_combine_selected(vector::core::BooleanOp::Xor)) {
        log_warn("Select 2 or more figures to XOR (Symmetric Difference).");
      }
      return;
    }

    // 10e. BOOL <union|intersect|subtract|xor>
    if (cmd == "bool" || cmd == "boolean") {
      if (tokens.size() < 2) {
        log_warn("Usage: bool <union|intersect|subtract|xor|or|and|diff>");
        return;
      }
      QString op = tokens[1].toLower();
      if (op == "union" || op == "or" || op == "merge") {
        canvas_->boolean_combine_selected(vector::core::BooleanOp::Union);
      } else if (op == "intersect" || op == "intersection" || op == "and") {
        canvas_->boolean_combine_selected(vector::core::BooleanOp::Intersection);
      } else if (op == "subtract" || op == "difference" || op == "diff" || op == "sub") {
        canvas_->boolean_combine_selected(vector::core::BooleanOp::Difference);
      } else if (op == "xor" || op == "symdiff") {
        canvas_->boolean_combine_selected(vector::core::BooleanOp::Xor);
      } else {
        log_error("Unknown boolean operation '" + tokens[1] + "'. Valid operations: union, intersect, subtract, xor.");
      }
      return;
    }

    // 11. SPLIT / UNGROUP / U
    if (cmd == "split" || cmd == "ungroup" || cmd == "u") {
      if (!canvas_->split_selected_figure()) {
        log_warn("Select a merged composite figure to split.");
      }
      return;
    }

    // 11b. BRING TO FRONT / SEND TO BACK / BRING FORWARD / SEND BACKWARD / ORDER
    const bool is_front = (cmd == "front" || cmd == "bringtofront" ||
                           (cmd == "bring" && tokens.size() >= 2 && (tokens[1].toLower() == "front" || (tokens.size() >= 3 && tokens[1].toLower() == "to" && tokens[2].toLower() == "front"))));
    const bool is_back = (cmd == "back" || cmd == "sendtoback" ||
                          (cmd == "send" && tokens.size() >= 2 && (tokens[1].toLower() == "back" || (tokens.size() >= 3 && tokens[1].toLower() == "to" && tokens[2].toLower() == "back"))));
    const bool is_forward = (cmd == "forward" || cmd == "bringforward" ||
                             (cmd == "bring" && tokens.size() >= 2 && tokens[1].toLower() == "forward"));
    const bool is_backward = (cmd == "backward" || cmd == "sendbackward" ||
                              (cmd == "send" && tokens.size() >= 2 && tokens[1].toLower() == "backward"));

    if (is_front) {
      if (canvas_->selected_nodes().empty()) {
        log_warn("No figures selected. Select one or more figures first.");
        return;
      }
      canvas_->bring_to_front_selected();
      log_info("Brought selected figure(s) to front.");
      return;
    }
    if (is_back) {
      if (canvas_->selected_nodes().empty()) {
        log_warn("No figures selected. Select one or more figures first.");
        return;
      }
      canvas_->send_to_back_selected();
      log_info("Sent selected figure(s) to back.");
      return;
    }
    if (is_forward) {
      if (canvas_->selected_nodes().empty()) {
        log_warn("No figures selected. Select one or more figures first.");
        return;
      }
      canvas_->bring_forward_selected();
      log_info("Brought selected figure(s) forward.");
      return;
    }
    if (is_backward) {
      if (canvas_->selected_nodes().empty()) {
        log_warn("No figures selected. Select one or more figures first.");
        return;
      }
      canvas_->send_backward_selected();
      log_info("Sent selected figure(s) backward.");
      return;
    }
    if (cmd == "order") {
      if (tokens.size() >= 2) {
        const QString sub = tokens[1].toLower();
        if (sub == "front" || sub == "top") {
          if (canvas_->selected_nodes().empty()) { log_warn("No figures selected."); return; }
          canvas_->bring_to_front_selected();
          log_info("Brought selected figure(s) to front.");
          return;
        } else if (sub == "back" || sub == "bottom") {
          if (canvas_->selected_nodes().empty()) { log_warn("No figures selected."); return; }
          canvas_->send_to_back_selected();
          log_info("Sent selected figure(s) to back.");
          return;
        } else if (sub == "forward" || sub == "up") {
          if (canvas_->selected_nodes().empty()) { log_warn("No figures selected."); return; }
          canvas_->bring_forward_selected();
          log_info("Brought selected figure(s) forward.");
          return;
        } else if (sub == "backward" || sub == "down") {
          if (canvas_->selected_nodes().empty()) { log_warn("No figures selected."); return; }
          canvas_->send_backward_selected();
          log_info("Sent selected figure(s) backward.");
          return;
        }
      }
      log_error("Syntax: order <front|back|forward|backward>");
      return;
    }

    // 12. MOVE / M
    if (cmd == "move" || cmd == "m") {
      if (tokens.size() >= 2) {
        auto delta = parse_point(tokens[1]);
        if (delta) {
          canvas_->move_selected(*delta);
          log_info(QString("Moved selection by (%1, %2)").arg(delta->x).arg(delta->y));
          return;
        } else if (tokens.size() >= 3) {
          // move <name> <dx,dy>
          auto delta2 = parse_point(tokens[2]);
          if (delta2 && canvas_->graph_model()) {
            if (auto* n = canvas_->graph_model()->find_node_by_name(tokens[1].toStdString())) {
              canvas_->move_node(n->id, *delta2);
              log_info(QString("Moved '%1' by (%2, %3)").arg(tokens[1]).arg(delta2->x).arg(delta2->y));
              return;
            }
          }
        }
      }
      log_error("Syntax: move <dx,dy> or move <name> <dx,dy>");
      return;
    }

    // 13. DELETE / DEL / ERASE / E
    if (cmd == "delete" || cmd == "del" || cmd == "erase" || cmd == "e") {
      if (tokens.size() >= 2) {
        if (canvas_->delete_node_by_name(tokens[1].toStdString())) {
          log_info("Deleted: " + tokens[1]);
        } else {
          log_warn("Node not found: " + tokens[1]);
        }
      } else {
        canvas_->delete_selected();
      }
      return;
    }

    // 14. SELECT / SEL
    if (cmd == "select" || cmd == "sel") {
      if (tokens.size() >= 2) {
        const QString arg = tokens[1].toLower();
        if (arg == "all" || arg == "a") {
          canvas_->select_all();
        } else if (arg == "none" || arg == "clear") {
          canvas_->clear_selection();
        } else {
          if (!canvas_->select_node_by_name(tokens[1].toStdString())) {
            log_warn("Node not found: " + tokens[1]);
          }
        }
      }
      return;
    }

    // 15. ZOOM / Z
    if (cmd == "zoom" || cmd == "z") {
      if (tokens.size() >= 2) {
        const QString mode = tokens[1].toLower();
        if (mode == "in" || mode == "i") {
          canvas_->zoom_at({canvas_->width() * 0.5, canvas_->height() * 0.5}, 1.25);
        } else if (mode == "out" || mode == "o") {
          canvas_->zoom_at({canvas_->width() * 0.5, canvas_->height() * 0.5}, 0.8);
        } else if (mode == "all" || mode == "extents" || mode == "e" || mode == "a") {
          canvas_->fit_content();
        } else {
          double factor = tokens[1].toDouble();
          if (factor > 0) {
            canvas_->zoom_at({canvas_->width() * 0.5, canvas_->height() * 0.5}, factor);
          }
        }
      } else {
        canvas_->fit_content();
      }
      canvas_->update();
      return;
    }

    // 16. PAN / P
    if (cmd == "pan" || cmd == "p") {
      if (tokens.size() >= 2) {
        auto delta = parse_point(tokens[1]);
        if (delta) {
          canvas_->pan_by(*delta);
          canvas_->update();
          log_info(QString("Panned by (%1, %2)").arg(delta->x).arg(delta->y));
          return;
        }
      }
      log_error("Syntax: pan <dx,dy>");
      return;
    }

    // 17. GRID / SNAP
    if (cmd == "grid") {
      if (grid_manager_) {
        auto cfg = grid_manager_->config();
        if (tokens.size() >= 2 && tokens[1].toLower() == "off") {
          cfg.visible = false;
          grid_manager_->set_config(cfg);
          log_info("Grid display: OFF");
        } else if (tokens.size() >= 2 && tokens[1].toLower() == "on") {
          cfg.visible = true;
          grid_manager_->set_config(cfg);
          log_info("Grid display: ON");
        } else {
          cfg.visible = !cfg.visible;
          grid_manager_->set_config(cfg);
          log_info(QString("Grid display: %1").arg(cfg.visible ? "ON" : "OFF"));
        }
        canvas_->update();
      }
      return;
    }
    if (cmd == "snap") {
      if (grid_manager_) {
        if (tokens.size() >= 2) {
          QString arg = tokens[1].toLower();
          if (arg == "off" || arg == "none") {
            grid_manager_->set_snap_strength(vector::grid::SnapStrength::Off);
          } else if (arg == "weak" || arg == "low") {
            grid_manager_->set_snap_strength(vector::grid::SnapStrength::Weak);
          } else if (arg == "normal" || arg == "med" || arg == "medium") {
            grid_manager_->set_snap_strength(vector::grid::SnapStrength::Normal);
          } else if (arg == "strong" || arg == "high") {
            grid_manager_->set_snap_strength(vector::grid::SnapStrength::Strong);
          } else if (arg == "on") {
            grid_manager_->set_snap_strength(vector::grid::SnapStrength::Normal);
          } else {
            bool ok = false;
            double spacing_or_tol = arg.toDouble(&ok);
            if (ok) {
              if (spacing_or_tol > 0.0) {
                grid_manager_->set_snap_spacing(spacing_or_tol, spacing_or_tol);
                if (!grid_manager_->is_snap_enabled()) {
                  grid_manager_->set_snap_enabled(true);
                }
                log_info(QString("Snap spacing set to %1 mm (Snap: ON)").arg(spacing_or_tol));
              } else {
                grid_manager_->set_snap_enabled(false);
              }
            }
          }
        } else {
          grid_manager_->set_snap_enabled(!grid_manager_->is_snap_enabled());
        }
        log_info(QString("Snap: %1 (tolerance: %2 px, spacing: %3 mm)")
            .arg(vector::grid::to_string(grid_manager_->snap_strength()))
            .arg(grid_manager_->snap_tolerance())
            .arg(grid_manager_->snap_spacing_x()));
        if (canvas_) canvas_->update();
      }
      return;
    }

    if (cmd == "snapunit") {
      if (grid_manager_) {
        if (tokens.size() >= 3) {
          bool ok_x = false, ok_y = false;
          double sx = tokens[1].toDouble(&ok_x);
          double sy = tokens[2].toDouble(&ok_y);
          if (ok_x && ok_y && sx > 0 && sy > 0) {
            grid_manager_->set_snap_spacing(sx, sy);
            log_info(QString("Snap spacing configured: X = %1 mm, Y = %2 mm").arg(sx).arg(sy));
          } else {
            log_error("Syntax: snapunit <x_spacing> <y_spacing>");
          }
        } else if (tokens.size() == 2) {
          bool ok = false;
          double s = tokens[1].toDouble(&ok);
          if (ok && s > 0) {
            grid_manager_->set_snap_spacing(s, s);
            log_info(QString("Snap spacing configured: X = %1 mm, Y = %2 mm").arg(s).arg(s));
          } else {
            log_error("Syntax: snapunit <x_spacing> <y_spacing>");
          }
        } else {
          log_info(QString("Current snap spacing: X = %1 mm, Y = %2 mm (Usage: snapunit <x> <y>)")
              .arg(grid_manager_->snap_spacing_x())
              .arg(grid_manager_->snap_spacing_y()));
        }
        if (canvas_) canvas_->update();
      }
      return;
    }

    if (cmd == "dsettings" || cmd == "settings" || cmd == "snapsettings" || cmd == "gridsettings") {
      if (settings_cb_) {
        settings_cb_(2); // Opens to Snap and Grid tab
        log_info("Opened Snap & Grid Settings dialog.");
      }
      return;
    }

    if (cmd == "pagesize" || cmd == "page" || cmd == "pagesetup") {
      if (!canvas_) return;
      if (tokens.size() == 1) {
        log_info(QString("Current Page Size: %1 %2 (%3 × %4), Margins: %5, Sheet: %6")
            .arg(vector::renderer::to_string(canvas_->page_preset()))
            .arg(vector::renderer::to_string(canvas_->page_orientation()))
            .arg(canvas_->page_bounds().width)
            .arg(canvas_->page_bounds().height)
            .arg(canvas_->page_margin())
            .arg(canvas_->show_page_sheet() ? "visible" : "hidden"));
        log_info("Usage: pagesize <preset|width height> [portrait|landscape]");
        return;
      }

      if (tokens.size() == 2 && (tokens[1].toLower() == "setup" || tokens[1].toLower() == "settings")) {
        if (settings_cb_) {
          settings_cb_(3); // Open Page Setup tab
          log_info("Opened Page Setup dialog.");
        }
        return;
      }

      bool ok_w = false;
      double w = tokens[1].toDouble(&ok_w);
      if (ok_w && tokens.size() >= 3) {
        bool ok_h = false;
        double h = tokens[2].toDouble(&ok_h);
        if (ok_h && w > 0 && h > 0) {
          canvas_->set_page_size(w, h);
          log_info(QString("Page size set to Custom %1 × %2").arg(w).arg(h));
          return;
        }
      }

      QString preset_str = tokens[1].toLower();
      vector::renderer::PagePreset preset = vector::renderer::PagePreset::Custom;
      if (preset_str == "letter") preset = vector::renderer::PagePreset::Letter;
      else if (preset_str == "legal") preset = vector::renderer::PagePreset::Legal;
      else if (preset_str == "tabloid" || preset_str == "ledger") preset = vector::renderer::PagePreset::Tabloid;
      else if (preset_str == "a4") preset = vector::renderer::PagePreset::A4;
      else if (preset_str == "a3") preset = vector::renderer::PagePreset::A3;
      else if (preset_str == "a2") preset = vector::renderer::PagePreset::A2;
      else if (preset_str == "a1") preset = vector::renderer::PagePreset::A1;
      else if (preset_str == "a0") preset = vector::renderer::PagePreset::A0;
      else {
        log_error("Unknown page preset: " + tokens[1] + ". Valid presets: letter, legal, tabloid, a4, a3, a2, a1, a0, or <width> <height>.");
        return;
      }

      vector::renderer::PageOrientation orient = canvas_->page_orientation();
      if (tokens.size() >= 3) {
        QString orient_str = tokens[2].toLower();
        if (orient_str == "portrait" || orient_str == "port" || orient_str == "p") {
          orient = vector::renderer::PageOrientation::Portrait;
        } else if (orient_str == "landscape" || orient_str == "land" || orient_str == "l") {
          orient = vector::renderer::PageOrientation::Landscape;
        }
      }

      canvas_->set_page_preset(preset, orient);
      log_info(QString("Page size set to %1 %2 (%3 × %4)")
          .arg(vector::renderer::to_string(preset))
          .arg(vector::renderer::to_string(orient))
          .arg(canvas_->page_bounds().width)
          .arg(canvas_->page_bounds().height));
      return;
    }

    if (cmd == "pagesheet") {
      if (!canvas_) return;
      if (tokens.size() >= 2) {
        QString arg = tokens[1].toLower();
        bool show = (arg == "on" || arg == "true" || arg == "1" || arg == "yes");
        canvas_->set_show_page_sheet(show);
        log_info(QString("Page sheet display: %1").arg(show ? "enabled" : "disabled"));
      } else {
        bool show = !canvas_->show_page_sheet();
        canvas_->set_show_page_sheet(show);
        log_info(QString("Toggled page sheet display: %1").arg(show ? "enabled" : "disabled"));
      }
      return;
    }

    if (cmd == "pagemargin" || cmd == "margin") {
      if (!canvas_) return;
      if (tokens.size() >= 2) {
        bool ok = false;
        double m = tokens[1].toDouble(&ok);
        if (ok && m >= 0) {
          canvas_->set_page_margin(m);
          log_info(QString("Page print margin set to %1").arg(m));
        } else {
          log_error("Syntax: pagemargin <margin_value>");
        }
      } else {
        log_info(QString("Current page print margin: %1").arg(canvas_->page_margin()));
      }
      return;
    }

    // 18. REMEMBER / RESETLAYOUT
    if (cmd == "remember" || cmd == "rememberlayout" || cmd == "persist") {
      if (tokens.size() >= 2) {
        const QString arg = tokens[1].toLower();
        if (arg == "on" || arg == "yes" || arg == "true" || arg == "1" || arg == "enable") {
          if (persist_set_cb_) persist_set_cb_(true);
          log_info("Session persistence ENABLED: Window positions, layout, colors, and themes will be remembered across runs.");
        } else if (arg == "off" || arg == "no" || arg == "false" || arg == "0" || arg == "disable") {
          if (persist_set_cb_) persist_set_cb_(false);
          log_info("Session persistence DISABLED: Settings will not be saved on exit.");
        } else {
          log_error("Syntax: remember <on|off>");
        }
      } else {
        const bool active = persist_get_cb_ ? persist_get_cb_() : false;
        log_info(QString("Session persistence is currently %1. Usage: remember <on|off>")
            .arg(active ? "ENABLED (active)" : "DISABLED (default)"));
      }
      return;
    }

    if (cmd == "resetlayout" || cmd == "defaultlayout") {
      if (reset_layout_cb_) {
        reset_layout_cb_();
        log_info("Window layout reset to default (1360 × 860).");
      }
      return;
    }

    // 19. NEW / SAVE / OPEN
    if (cmd == "new") {
      if (new_cb_) new_cb_();
      return;
    }
    if (cmd == "save") {
      QString fpath = (tokens.size() >= 2) ? tokens[1] : "";
      if (save_cb_) save_cb_(fpath);
      return;
    }
    if (cmd == "open") {
      QString fpath = (tokens.size() >= 2) ? tokens[1] : "";
      if (open_cb_) open_cb_(fpath);
      return;
    }

    // 19b. EXPORT
    if (cmd == "export") {
      QString fmt = (tokens.size() >= 2) ? tokens[1].toLower() : "xml";
      QString p = (tokens.size() >= 3) ? tokens[2] : "";
      if (export_cb_) {
        export_cb_(fmt, p);
      } else {
        log_info(QString("Diagram export (%1): %2").arg(fmt, p.isEmpty() ? "default" : p));
      }
      return;
    }

    // 19c. FONT & TYPOGRAPHY
    if (cmd == "font") {
      if (tokens.size() < 2) {
        log_info("Usage: font <size|family|bold|italic|underline|regular> [val]");
        return;
      }
      const QString sub = tokens[1].toLower();
      bool is_num = false;
      int size_val = sub.toInt(&is_num);
      if (is_num && size_val > 0) {
        canvas_->set_selected_nodes_font_size(size_val);
        log_info(QString("Set font size to %1 pt for selection").arg(size_val));
        return;
      }
      if (sub == "bold") {
        bool enable = (tokens.size() < 3) ? true : (tokens[2].toLower() == "on" || tokens[2].toLower() == "true" || tokens[2] == "1");
        canvas_->set_selected_nodes_font_bold(enable);
        log_info(QString("Set font bold: %1").arg(enable ? "true" : "false"));
        return;
      }
      if (sub == "italic") {
        bool enable = (tokens.size() < 3) ? true : (tokens[2].toLower() == "on" || tokens[2].toLower() == "true" || tokens[2] == "1");
        canvas_->set_selected_nodes_font_italic(enable);
        log_info(QString("Set font italic: %1").arg(enable ? "true" : "false"));
        return;
      }
      if (sub == "underline") {
        bool enable = (tokens.size() < 3) ? true : (tokens[2].toLower() == "on" || tokens[2].toLower() == "true" || tokens[2] == "1");
        canvas_->set_selected_nodes_font_underline(enable);
        log_info(QString("Set font underline: %1").arg(enable ? "true" : "false"));
        return;
      }
      if (sub == "regular") {
        canvas_->set_selected_nodes_font_bold(false);
        canvas_->set_selected_nodes_font_italic(false);
        canvas_->set_selected_nodes_font_underline(false);
        log_info("Set font to regular style");
        return;
      }
      if (sub == "size" && tokens.size() >= 3) {
        int sz = tokens[2].toInt(&is_num);
        if (is_num && sz > 0) {
          canvas_->set_selected_nodes_font_size(sz);
          log_info(QString("Set font size to %1 pt").arg(sz));
          return;
        }
      }
      if (sub == "family" && tokens.size() >= 3) {
        QString fam = full_cmd.section(' ', 2).trimmed();
        canvas_->set_selected_nodes_font_family(fam);
        log_info(QString("Set font family to '%1'").arg(fam));
        return;
      }
      QString fam = full_cmd.section(' ', 1).trimmed();
      canvas_->set_selected_nodes_font_family(fam);
      log_info(QString("Set font family to '%1'").arg(fam));
      return;
    }

    // 19d. FILL COLOR
    if (cmd == "fill") {
      if (tokens.size() < 2) {
        log_info("Usage: fill <#hex_color|color_name>");
        return;
      }
      QString c = tokens[1];
      canvas_->set_selected_nodes_fill_color(c);
      log_info(QString("Set fill color to %1 for selection").arg(c));
      return;
    }

    // 19e. STROKE COLOR & WIDTH
    if (cmd == "stroke") {
      if (tokens.size() < 2) {
        log_info("Usage: stroke <#hex_color|color_name|width_px>");
        return;
      }
      bool is_num = false;
      double w = tokens[1].toDouble(&is_num);
      if (is_num && w > 0.0) {
        canvas_->set_selected_nodes_stroke_width(w);
        log_info(QString("Set stroke width to %1 px for selection").arg(w));
      } else {
        canvas_->set_selected_nodes_stroke_color(tokens[1]);
        log_info(QString("Set stroke color to %1 for selection").arg(tokens[1]));
      }
      return;
    }

    // 19f. TEXTCOLOR / FONTCOLOR
    if (cmd == "textcolor" || cmd == "text_color" || cmd == "fontcolor") {
      if (tokens.size() < 2) {
        log_info("Usage: textcolor <#hex_color|color_name>");
        return;
      }
      canvas_->set_selected_nodes_text_color(tokens[1]);
      log_info(QString("Set text color to %1 for selection").arg(tokens[1]));
      return;
    }

    // 19g. STYLE RESET
    if (cmd == "style") {
      if (tokens.size() >= 2 && tokens[1].toLower() == "reset") {
        canvas_->reset_selected_nodes_style();
        log_info("Reset style of selected nodes to theme defaults");
        return;
      }
      log_info("Usage: style reset");
      return;
    }

    // 20. SYMBOL / SYM
    if (cmd == "symbol" || cmd == "sym") {
      if (!canvas_ || !canvas_->graph_model()) {
        log_error("Diagram model not available.");
        return;
      }
      auto* g = canvas_->graph_model();
      QString subcmd = (tokens.size() >= 2) ? tokens[1].toLower() : "edit";
      QString target_name = (tokens.size() >= 3) ? tokens[2] : "";

      vector::core::Node* target_node = nullptr;
      if (!target_name.isEmpty()) {
        for (const auto& [nid, n] : g->nodes()) {
          if (QString::fromStdString(n.name).compare(target_name, Qt::CaseInsensitive) == 0) {
            target_node = g->find_node(nid);
            break;
          }
        }
      } else {
        for (const auto& [nid, n] : g->nodes()) {
          if (n.selected) { target_node = g->find_node(nid); break; }
        }
      }

      if (!target_node) {
        log_error("No block selected or found with name: " + target_name);
        return;
      }

      if (subcmd == "edit") {
        if (symbol_edit_cb_) {
          symbol_edit_cb_(QString::fromStdString(target_node->name));
        } else {
          log_info("Opening Symbol Editor for '" + QString::fromStdString(target_node->name) + "'");
        }
      } else if (subcmd == "sync") {
        auto* child_g = canvas_->ensure_subsystem_graph(target_node->id);
        if (child_g) {
          vector::core::SymbolGenerator::sync_symbol_from_diagram(*target_node, *g, *child_g);
          g->update_all_edge_routes();
          canvas_->update();
          log_info(QString("Synchronized symbol pins for '%1' with primary definition.").arg(QString::fromStdString(target_node->name)));
        } else {
          log_warn(QString("No child diagram found to sync for '%1'.").arg(QString::fromStdString(target_node->name)));
        }
      } else if (subcmd == "fit") {
        vector::core::SymbolGenerator::auto_fit_dimensions(*target_node, *g);
        canvas_->update();
        log_info(QString("Fitted symbol dimensions to pins for '%1': %2 × %3")
            .arg(QString::fromStdString(target_node->name))
            .arg(target_node->bounds.width).arg(target_node->bounds.height));
      } else {
        log_info("Usage: symbol <edit|sync|fit> [block_name]");
      }
      return;
    }

    // 21. DRIFT
    if (cmd == "drift") {
      if (!canvas_ || !canvas_->graph_model()) {
        log_error("Diagram model not available.");
        return;
      }
      auto* g = canvas_->graph_model();
      QString arg = (tokens.size() >= 2) ? tokens[1].toLower() : "status";
      QString target_name = (tokens.size() >= 3) ? tokens[2] : "";

      vector::core::Node* target_node = nullptr;
      if (!target_name.isEmpty()) {
        for (const auto& [nid, n] : g->nodes()) {
          if (QString::fromStdString(n.name).compare(target_name, Qt::CaseInsensitive) == 0) {
            target_node = g->find_node(nid);
            break;
          }
        }
      } else {
        for (const auto& [nid, n] : g->nodes()) {
          if (n.selected) { target_node = g->find_node(nid); break; }
        }
      }

      if (!target_node) {
        log_error("No block selected or found with name: " + target_name);
        return;
      }

      if (arg == "on" || arg == "allow" || arg == "true" || arg == "enable") {
        target_node->properties["definition_sync_mode"] = "allow_drift";
        log_info(QString("Instance '%1' definition synchronization set to: ALLOW DRIFT (Independent instance override).")
            .arg(QString::fromStdString(target_node->name)));
      } else if (arg == "off" || arg == "follow" || arg == "false" || arg == "disable") {
        target_node->properties["definition_sync_mode"] = "follow_primary";
        log_info(QString("Instance '%1' definition synchronization set to: FOLLOW PRIMARY DEFINITION (Synchronized).")
            .arg(QString::fromStdString(target_node->name)));
      } else {
        const std::string m = target_node->properties.contains("definition_sync_mode") ? target_node->properties.at("definition_sync_mode") : "follow_primary";
        log_info(QString("Instance '%1' sync mode: %2. Usage: drift <on|off> [name]")
            .arg(QString::fromStdString(target_node->name))
            .arg(m == "allow_drift" ? "Allow Drift (Independent)" : "Follow Primary Definition (Synchronized)"));
      }
      return;
    }

    log_warn("Unknown command: '" + cmd + "'. Type 'help' for command reference.");
  }

  void process_interactive_step(const QString& input) {
    if (active_command_ == "line") {
      if (command_step_ == 1) {
        auto pt = parse_point(input);
        if (!pt) {
          log_error("Invalid point format. Please enter x,y:");
          return;
        }
        step_points_.push_back(*pt);
        command_step_ = 2;
        prompt_label_->setText("Specify next point (x,y):");
      } else if (command_step_ == 2) {
        auto pt = parse_point(input, step_points_.back());
        if (!pt) {
          log_error("Invalid point format. Please enter x,y:");
          return;
        }
        canvas_->add_wire_by_points(step_points_.back(), *pt);
        log_info(QString("Line segment added from (%1, %2) to (%3, %4)")
            .arg(step_points_.back().x).arg(step_points_.back().y).arg(pt->x).arg(pt->y));
        step_points_.push_back(*pt);
        prompt_label_->setText("Specify next point or [Enter to finish]:");
      }
    } else if (active_command_ == "rect") {
      if (command_step_ == 1) {
        auto pt = parse_point(input);
        if (!pt) {
          log_error("Invalid corner point. Enter x,y:");
          return;
        }
        step_points_.push_back(*pt);
        command_step_ = 2;
        prompt_label_->setText("Specify other corner or [w,h]:");
      } else if (command_step_ == 2) {
        auto dims = parse_point(input);
        if (!dims) {
          log_error("Invalid dimension. Enter w,h:");
          return;
        }
        core::Vec2d p1 = step_points_[0];
        core::NodeId nid = canvas_->add_shape(core::ShapeType::Rectangle, {p1.x + dims->x * 0.5, p1.y + dims->y * 0.5});
        if (auto* n = canvas_->graph_model()->find_node(nid)) {
          n->bounds = {p1.x, p1.y, std::abs(dims->x), std::abs(dims->y)};
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
        log_info(QString("Rectangle created size %1x%2").arg(dims->x).arg(dims->y));
        cancel_command();
      }
    } else if (active_command_ == "circle") {
      if (command_step_ == 1) {
        auto pt = parse_point(input);
        if (!pt) {
          log_error("Invalid center. Enter x,y:");
          return;
        }
        step_points_.push_back(*pt);
        command_step_ = 2;
        prompt_label_->setText("Specify radius <50>:");
      } else if (command_step_ == 2) {
        double r = input.toDouble();
        if (r <= 0) r = 50.0;
        core::Vec2d c = step_points_[0];
        core::NodeId nid = canvas_->add_shape(core::ShapeType::Ellipse, c);
        if (auto* n = canvas_->graph_model()->find_node(nid)) {
          n->bounds = {c.x - r, c.y - r, r * 2.0, r * 2.0};
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
        log_info(QString("Circle created radius %1").arg(r));
        cancel_command();
      }
    } else if (active_command_ == "poly") {
      if (command_step_ == 1) {
        int sides = input.toInt();
        if (sides < 3) sides = 6;
        interactive_sides_ = sides;
        command_step_ = 2;
        prompt_label_->setText("Specify center point (x,y):");
      } else if (command_step_ == 2) {
        auto pt = parse_point(input);
        if (!pt) {
          log_error("Invalid center. Enter x,y:");
          return;
        }
        step_points_.push_back(*pt);
        command_step_ = 3;
        prompt_label_->setText("Specify radius <50>:");
      } else if (command_step_ == 3) {
        double r = input.toDouble();
        if (r <= 0) r = 50.0;
        core::Vec2d c = step_points_[0];
        core::NodeId nid = canvas_->add_polygon(c, interactive_sides_);
        if (auto* n = canvas_->graph_model()->find_node(nid)) {
          n->bounds = {c.x - r, c.y - r, r * 2.0, r * 2.0};
          canvas_->refresh_spatial_index();
          canvas_->update();
        }
        log_info(QString("Polygon (%1 sides) created radius %2").arg(interactive_sides_).arg(r));
        cancel_command();
      }
    }
  }

  void process_arrow_command(const std::vector<QString>& tokens) {
    if (!canvas_) {
      log_error("Canvas engine not connected.");
      return;
    }
    auto pt = (tokens.size() >= 2) ? parse_point(tokens[1]) : std::nullopt;
    if (pt) {
      double w = 130.0;
      double h = 80.0;
      std::string name;
      if (tokens.size() >= 3) {
        auto dims = parse_point(tokens[2]);
        if (dims) {
          w = std::abs(dims->x);
          h = std::abs(dims->y);
          if (tokens.size() >= 4) name = tokens[3].toStdString();
        } else {
          name = tokens[2].toStdString();
        }
      }
      core::NodeId nid = canvas_->add_arrow_shape(*pt, w, h, name);
      log_info(QString("Arrow Block '%1' (ID %2) created at (%3, %4) size %5x%6")
          .arg(QString::fromStdString(name.empty() ? ("Arrow " + std::to_string(nid)) : name))
          .arg(nid).arg(pt->x).arg(pt->y).arg(w).arg(h));
      return;
    }

    if (tokens.size() >= 3) {
      auto* gm = canvas_->graph_model();
      if (gm) {
        const auto* n1 = gm->find_node_by_name(tokens[1].toStdString());
        const auto* n2 = gm->find_node_by_name(tokens[2].toStdString());
        core::NodeId id1 = n1 ? n1->id : tokens[1].toULongLong();
        core::NodeId id2 = n2 ? n2->id : tokens[2].toULongLong();
        if (gm->find_node(id1) && gm->find_node(id2)) {
          core::PortId p1 = (tokens.size() >= 4) ? tokens[3].toULongLong() : core::InvalidPortId;
          core::PortId p2 = (tokens.size() >= 5) ? tokens[4].toULongLong() : core::InvalidPortId;
          core::EdgeId eid = canvas_->auto_route_arrow(id1, p1, id2, p2);
          log_info(QString("Created auto-routed arrow Net %1 from node %2 to %3").arg(eid).arg(id1).arg(id2));
          return;
        }
      }
    }
    log_error("Usage: arrow <x,y> [w,h] [name] OR arrow <source_node> <target_node> [source_port] [target_port]");
  }

  void process_segmove_command(const std::vector<QString>& tokens) {
    if (!canvas_) {
      log_error("Canvas engine not connected.");
      return;
    }
    if (tokens.size() >= 2) {
      const QString sub = tokens[1].toLower();
      if (sub == "on" || sub == "true" || sub == "1" || sub == "enable") {
        canvas_->set_segment_moving_enabled(true);
        log_info("Edge segment moving ENABLED (Segments can be dragged directly without holding Shift).");
        return;
      } else if (sub == "off" || sub == "false" || sub == "0" || sub == "disable") {
        canvas_->set_segment_moving_enabled(false);
        log_info("Edge segment moving DISABLED (default). Hold Shift to move edge segments on the fly, or Alt+S to toggle.");
        return;
      } else if (sub == "toggle" || sub == "t") {
        const bool cur = canvas_->is_segment_moving_enabled();
        canvas_->set_segment_moving_enabled(!cur);
        log_info(QString("Edge segment moving %1 (Hold Shift to move when disabled).")
            .arg(!cur ? "ENABLED" : "DISABLED"));
        return;
      }
    }
    const bool cur = canvas_->is_segment_moving_enabled();
    log_info(QString("Edge segment moving is currently %1.\n"
                     "When disabled (default), hold Shift to drag edge segments. Run 'segmove on' or press Alt+S to toggle.")
        .arg(cur ? "ENABLED" : "DISABLED (Hold Shift to move)"));
  }

  void process_modify_command(const std::vector<QString>& tokens) {
    if (!canvas_ || !canvas_->graph_model()) {
      log_error("No canvas or graph model connected.");
      return;
    }
    if (tokens.size() < 3) {
      log_error("Usage: modify [shape_name] <property> <value>\n"
                "Properties: x, y, pos (x,y), w/width, h/height, size (w,h), rot (deg), name, text, fill, stroke, strokewidth, textcolor, prop <key> <val>");
      return;
    }

    QString target_str;
    QString prop_str;
    size_t val_start_idx = 2;

    static const std::unordered_set<std::string> known_props = {
      "x", "y", "pos", "position", "w", "width", "h", "height", "size",
      "rot", "rotation", "angle", "name", "text", "label",
      "fill", "fill_color", "stroke", "stroke_color", "strokewidth", "sw", "stroke_width",
      "textcolor", "text_color", "fontcolor", "fontsize", "fontfamily", "prop", "property"
    };

    const std::string t1 = tokens[1].toLower().toStdString();
    if (known_props.count(t1)) {
      prop_str = tokens[1].toLower();
      val_start_idx = 2;
    } else {
      target_str = tokens[1];
      if (tokens.size() < 4) {
        log_error("Usage: modify " + target_str + " <property> <value>");
        return;
      }
      prop_str = tokens[2].toLower();
      val_start_idx = 3;
    }

    auto* gm = canvas_->graph_model();
    std::vector<core::NodeId> target_ids;
    if (!target_str.isEmpty()) {
      if (const auto* n = gm->find_node_by_name(target_str.toStdString())) {
        target_ids.push_back(n->id);
      } else {
        bool ok = false;
        core::NodeId nid = target_str.toULongLong(&ok);
        if (ok && gm->find_node(nid)) {
          target_ids.push_back(nid);
        } else {
          log_error("Could not find shape named or with ID: " + target_str);
          return;
        }
      }
    } else {
      target_ids = canvas_->selected_nodes();
      if (target_ids.empty()) {
        log_error("No shape selected. Select a shape or specify its name: modify <shape_name> <prop> <val>");
        return;
      }
    }

    QString val_str;
    for (size_t i = val_start_idx; i < tokens.size(); ++i) {
      if (i > val_start_idx) val_str.append(" ");
      val_str.append(tokens[i]);
    }

    for (core::NodeId nid : target_ids) {
      auto* node = gm->find_node(nid);
      if (!node) continue;

      if (prop_str == "x") {
        bool ok = false;
        double v = val_str.toDouble(&ok);
        if (ok) node->bounds.x = v;
      } else if (prop_str == "y") {
        bool ok = false;
        double v = val_str.toDouble(&ok);
        if (ok) node->bounds.y = v;
      } else if (prop_str == "pos" || prop_str == "position") {
        auto pt = parse_point(val_str);
        if (pt) {
          node->bounds.x = pt->x;
          node->bounds.y = pt->y;
        } else {
          log_error("Invalid position format. Use x,y (e.g. 150,200)");
          return;
        }
      } else if (prop_str == "w" || prop_str == "width") {
        bool ok = false;
        double v = val_str.toDouble(&ok);
        if (ok && v > 0.0) node->bounds.width = v;
      } else if (prop_str == "h" || prop_str == "height") {
        bool ok = false;
        double v = val_str.toDouble(&ok);
        if (ok && v > 0.0) node->bounds.height = v;
      } else if (prop_str == "size") {
        auto pt = parse_point(val_str);
        if (pt && pt->x > 0.0 && pt->y > 0.0) {
          node->bounds.width = pt->x;
          node->bounds.height = pt->y;
        } else {
          log_error("Invalid size format. Use w,h (e.g. 200,120)");
          return;
        }
      } else if (prop_str == "rot" || prop_str == "rotation" || prop_str == "angle") {
        bool ok = false;
        double deg = val_str.toDouble(&ok);
        if (ok) {
          while (deg < 0.0) deg += 360.0;
          while (deg >= 360.0) deg -= 360.0;
          node->rotation_deg = deg;
        }
      } else if (prop_str == "name") {
        node->name = val_str.toStdString();
      } else if (prop_str == "text" || prop_str == "label") {
        node->properties["text"] = val_str.toStdString();
        node->name = val_str.toStdString();
      } else if (prop_str == "fill" || prop_str == "fill_color") {
        node->properties["fill_color"] = val_str.toStdString();
      } else if (prop_str == "stroke" || prop_str == "stroke_color") {
        node->properties["stroke_color"] = val_str.toStdString();
      } else if (prop_str == "strokewidth" || prop_str == "sw" || prop_str == "stroke_width") {
        node->properties["stroke_width"] = val_str.toStdString();
      } else if (prop_str == "textcolor" || prop_str == "text_color" || prop_str == "fontcolor") {
        node->properties["text_color"] = val_str.toStdString();
      } else if (prop_str == "fontsize") {
        node->properties["font_size"] = val_str.toStdString();
      } else if (prop_str == "fontfamily") {
        node->properties["font_family"] = val_str.toStdString();
      } else if (prop_str == "prop" || prop_str == "property") {
        if (val_start_idx + 1 < tokens.size()) {
          const std::string key = tokens[val_start_idx].toStdString();
          const std::string val = tokens[val_start_idx + 1].toStdString();
          node->properties[key] = val;
        }
      } else {
        log_error("Unknown property '" + prop_str + "'.");
        return;
      }

      log_info(QString("Shape '%1' (ID %2): set %3 = %4")
          .arg(QString::fromStdString(node->name))
          .arg(nid)
          .arg(prop_str)
          .arg(val_str));
    }

    canvas_->refresh_spatial_index();
    canvas_->update();
    emit canvas_->diagram_modified();
  }

  void process_point_command(const std::vector<QString>& tokens) {
    if (!canvas_ || !canvas_->graph_model()) {
      log_error("No canvas or graph model connected.");
      return;
    }
    const auto& sel = canvas_->selected_nodes();
    if (sel.empty()) {
      log_error("No shape selected. Select a shape first to view or modify its control points.");
      return;
    }

    core::NodeId nid = sel.front();
    auto* node = canvas_->graph_model()->find_node(nid);
    if (!node) {
      log_error("Selected shape node not found.");
      return;
    }

    if (tokens.size() <= 1 || tokens[1].toLower() == "list") {
      core::ensure_shape_polygon_vertices(*node);
      history_view_->append(QString("<span style='color: #38BDF8;'><b>Control Points for '%1' (%2 points):</b></span>")
          .arg(QString::fromStdString(node->name)).arg(node->polygon_vertices.size()));
      const auto handles = core::get_parametric_handles(*node);
      for (size_t i = 0; i < node->polygon_vertices.size(); ++i) {
        const auto& pv = node->polygon_vertices[i];
        const double wx = node->bounds.x + pv.x * node->bounds.width;
        const double wy = node->bounds.y + pv.y * node->bounds.height;
        QString desc;
        for (const auto& h : handles) {
          if (h.id == static_cast<int>(i)) {
            desc = QString::fromStdString(" (" + h.description + ")");
            break;
          }
        }
        history_view_->append(QString("  <b>[%1]</b> norm: (%2, %3)  world: (%4, %5)%6")
            .arg(i)
            .arg(pv.x, 0, 'f', 3)
            .arg(pv.y, 0, 'f', 3)
            .arg(wx, 0, 'f', 1)
            .arg(wy, 0, 'f', 1)
            .arg(desc));
      }
      scroll_to_bottom();
      return;
    }

    const QString sub = tokens[1].toLower();
    if (sub == "add" || sub == "insert") {
      if (tokens.size() < 3) {
        log_error("Usage: point add <x,y>");
        return;
      }
      auto pt = parse_point(tokens[2]);
      if (!pt) {
        log_error("Invalid point coordinate format: " + tokens[2] + ". Use x,y (e.g. 150,200).");
        return;
      }
      core::Vec2d world_pos = *pt;
      if (pt->x >= 0.0 && pt->x <= 1.0 && pt->y >= 0.0 && pt->y <= 1.0 &&
          node->bounds.width > 2.0 && node->bounds.height > 2.0) {
        world_pos = {node->bounds.x + pt->x * node->bounds.width, node->bounds.y + pt->y * node->bounds.height};
      }
      if (canvas_->insert_control_point_at_world(nid, world_pos)) {
        log_info(QString("Added control point at world (%1, %2) to shape '%3'")
            .arg(world_pos.x).arg(world_pos.y).arg(QString::fromStdString(node->name)));
      } else {
        log_error("Failed to add control point.");
      }
      return;
    }

    if (sub == "del" || sub == "delete" || sub == "remove" || sub == "rm") {
      if (tokens.size() < 3) {
        log_error("Usage: point del <index>");
        return;
      }
      bool ok = false;
      int idx = tokens[2].toInt(&ok);
      if (!ok || idx < 0 || idx >= static_cast<int>(node->polygon_vertices.size())) {
        log_error(QString("Invalid point index %1 (valid: 0 to %2)").arg(tokens[2]).arg(static_cast<int>(node->polygon_vertices.size()) - 1));
        return;
      }
      if (canvas_->remove_control_point(nid, idx)) {
        log_info(QString("Removed control point [%1] from shape '%2'").arg(idx).arg(QString::fromStdString(node->name)));
      } else {
        log_error("Cannot remove control point (minimum 3 vertices required).");
      }
      return;
    }

    if (sub == "set" || sub == "move") {
      if (tokens.size() < 4) {
        log_error("Usage: point set <index> <x,y>");
        return;
      }
      bool ok = false;
      int idx = tokens[2].toInt(&ok);
      core::ensure_shape_polygon_vertices(*node);
      if (!ok || idx < 0 || idx >= static_cast<int>(node->polygon_vertices.size())) {
        log_error(QString("Invalid point index %1 (valid: 0 to %2)").arg(tokens[2]).arg(static_cast<int>(node->polygon_vertices.size()) - 1));
        return;
      }
      auto pt = parse_point(tokens[3]);
      if (!pt) {
        log_error("Invalid point coordinate format: " + tokens[3] + ". Use x,y (e.g. 0.5,0.2 or world 200,150).");
        return;
      }
      core::Vec2d norm_pt = *pt;
      if (norm_pt.x < 0.0 || norm_pt.x > 1.0 || norm_pt.y < 0.0 || norm_pt.y > 1.0) {
        norm_pt.x = std::clamp((pt->x - node->bounds.x) / std::max(1.0, node->bounds.width), 0.0, 1.0);
        norm_pt.y = std::clamp((pt->y - node->bounds.y) / std::max(1.0, node->bounds.height), 0.0, 1.0);
      }
      node->polygon_vertices[idx] = norm_pt;
      canvas_->refresh_spatial_index();
      canvas_->update();
      emit canvas_->diagram_modified();
      log_info(QString("Moved control point [%1] of shape '%2' to norm (%3, %4)")
          .arg(idx).arg(QString::fromStdString(node->name)).arg(norm_pt.x, 0, 'f', 3).arg(norm_pt.y, 0, 'f', 3));
      return;
    }

    log_error("Usage: point <list | add <x,y> | del <index> | set <index> <x,y>>");
  }

  QTextEdit* history_view_{nullptr};
  QLabel* prompt_label_{nullptr};
  CommandLineEdit* input_line_{nullptr};

  vector::renderer::CanvasWidget* canvas_{nullptr};
  vector::grid::GridManager* grid_manager_{nullptr};
  NewActionCallback new_cb_;
  FileActionCallback save_cb_;
  FileActionCallback open_cb_;
  ExportActionCallback export_cb_;
  SettingsActionCallback settings_cb_;
  SymbolEditActionCallback symbol_edit_cb_{nullptr};
  PersistenceGetCallback persist_get_cb_;
  PersistenceSetCallback persist_set_cb_;
  ResetLayoutCallback reset_layout_cb_;

  std::vector<QString> command_history_;
  QString last_executed_command_;
  QStringList keywords_;

  // Interactive command state
  QString active_command_;
  int command_step_{0};
  std::vector<core::Vec2d> step_points_;
  int interactive_sides_{6};
};

} // namespace vector::gui

#endif // VECTOR_GUI_COMMAND_CONSOLE_DOCK_HPP
