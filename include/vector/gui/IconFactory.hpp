// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_GUI_ICON_FACTORY_HPP
#define VECTOR_GUI_ICON_FACTORY_HPP

#ifdef VECTOR_ENABLE_QT

#include <QBrush>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>

#include <cmath>

namespace vector::gui {

class IconFactory {
public:
  enum class IconType {
    Select,
    FileNew,
    FileOpen,
    FileSave,
    Rectangle,
    RoundedRect,
    Ellipse,
    PolygonHex,
    Triangle,
    Diamond,
    Octagon,
    SubsystemBlock,
    SubsystemFile,
    InputPort,
    OutputPort,
    Wire,
    MergeFigures,
    BooleanUnion = MergeFigures,
    BooleanIntersect,
    BooleanSubtract,
    BooleanXor,
    SplitFigure,
    Undo,
    Redo,
    Delete,
    ZoomIn,
    ZoomOut,
    FitContent,
    ShowGrid,
    Snapping,
    DrillDown,
    PopScope,
    BringToFront,
    SendToBack,
    BringForward,
    SendBackward,
    FontBold,
    FontItalic,
    FontUnderline,
    TextColor,
    FillColor,
    StrokeColor,
    FileExport,
    Pan,
    RightTriangle,
    Pentagon,
    Heptagon,
    Decagon,
    Dodecagon,
    Star,
    Cross,
    Trapezoid,
    Parallelogram,
    Chevron,
    Cylinder,
    BidirectionalPort,
    PolygonDraw,
    AngleSnap,
    RotateCW,
    RotateCCW
  };

  static QPixmap render_pixmap(IconType type, const QColor& fg, int size = 48) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const double s = static_cast<double>(size);
    auto px = [s](double val) { return (val / 48.0) * s; };

    QPen strokePen(fg, px(2.6), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(strokePen);
    p.setBrush(Qt::NoBrush);

    switch (type) {
      case IconType::Select: {
        // Arrow Pointer / Cursor
        QPainterPath path;
        path.moveTo(px(10), px(7));
        path.lineTo(px(10), px(35));
        path.lineTo(px(18), px(27));
        path.lineTo(px(25), px(39));
        path.lineTo(px(30), px(36));
        path.lineTo(px(23), px(24));
        path.lineTo(px(33), px(24));
        path.closeSubpath();

        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 180));
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        break;
      }
      case IconType::FileNew: {
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 25));
        QPolygonF doc;
        doc << QPointF(px(12), px(8))
            << QPointF(px(28), px(8))
            << QPointF(px(36), px(16))
            << QPointF(px(36), px(40))
            << QPointF(px(12), px(40));
        p.drawPolygon(doc);
        p.drawLine(QPointF(px(28), px(8)), QPointF(px(28), px(16)));
        p.drawLine(QPointF(px(28), px(16)), QPointF(px(36), px(16)));
        p.drawLine(QPointF(px(18), px(24)), QPointF(px(30), px(24)));
        p.drawLine(QPointF(px(18), px(30)), QPointF(px(26), px(30)));
        break;
      }
      case IconType::FileOpen: {
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(234, 179, 8, 40));
        QPolygonF back;
        back << QPointF(px(8), px(14))
             << QPointF(px(20), px(14))
             << QPointF(px(24), px(18))
             << QPointF(px(40), px(18))
             << QPointF(px(40), px(36))
             << QPointF(px(8), px(36));
        p.drawPolygon(back);
        p.setBrush(QColor(234, 179, 8, 80));
        QPolygonF front;
        front << QPointF(px(6), px(36))
              << QPointF(px(12), px(22))
              << QPointF(px(42), px(22))
              << QPointF(px(36), px(36));
        p.drawPolygon(front);
        break;
      }
      case IconType::FileSave: {
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(59, 130, 246, 40));
        QPolygonF disk;
        disk << QPointF(px(10), px(8))
             << QPointF(px(32), px(8))
             << QPointF(px(38), px(14))
             << QPointF(px(38), px(40))
             << QPointF(px(10), px(40));
        p.drawPolygon(disk);
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 50));
        p.drawRect(QRectF(px(16), px(8), px(16), px(12)));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(px(15), px(24), px(18), px(16)));
        break;
      }
      case IconType::Rectangle: {
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawRect(QRectF(px(8), px(12), px(32), px(24)));
        break;
      }
      case IconType::RoundedRect: {
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawRoundedRect(QRectF(px(8), px(12), px(32), px(24)), px(6), px(6));
        break;
      }
      case IconType::Ellipse: {
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawEllipse(QRectF(px(8), px(12), px(32), px(24)));
        break;
      }
      case IconType::PolygonHex: {
        QPolygonF poly;
        const double cx = px(24), cy = px(24), r = px(16);
        for (int i = 0; i < 6; ++i) {
          const double rad = (i * 60.0 - 30.0) * (M_PI / 180.0);
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Triangle: {
        QPolygonF poly;
        poly << QPointF(px(24), px(9))
             << QPointF(px(9), px(39))
             << QPointF(px(39), px(39));
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Diamond: {
        QPolygonF poly;
        poly << QPointF(px(24), px(8))
             << QPointF(px(40), px(24))
             << QPointF(px(24), px(40))
             << QPointF(px(8), px(24));
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Octagon: {
        QPolygonF poly;
        const double cx = px(24), cy = px(24), r = px(16.5);
        for (int i = 0; i < 8; ++i) {
          const double rad = (i * 45.0 + 22.5) * (M_PI / 180.0);
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::SubsystemBlock: {
        // Outer block with banner and internal chip/pins
        p.setBrush(QColor(59, 130, 246, 40));
        p.setPen(QPen(QColor(59, 130, 246), px(2.4)));
        p.drawRoundedRect(QRectF(px(8), px(8), px(32), px(32)), px(3), px(3));
        p.setBrush(QColor(59, 130, 246, 160));
        p.drawRect(QRectF(px(8), px(8), px(32), px(8)));
        p.setPen(QPen(fg, px(2.2)));
        p.drawLine(QPointF(px(3), px(20)), QPointF(px(8), px(20)));
        p.drawLine(QPointF(px(3), px(28)), QPointF(px(8), px(28)));
        p.drawLine(QPointF(px(40), px(24)), QPointF(px(45), px(24)));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(16), px(20), px(16), px(14)));
        break;
      }
      case IconType::SubsystemFile: {
        // Folder / File icon with nested block
        QPainterPath path;
        path.moveTo(px(8), px(12));
        path.lineTo(px(20), px(12));
        path.lineTo(px(24), px(16));
        path.lineTo(px(40), px(16));
        path.lineTo(px(40), px(38));
        path.lineTo(px(8), px(38));
        path.closeSubpath();
        p.setPen(QPen(QColor(234, 179, 8), px(2.4)));
        p.setBrush(QColor(234, 179, 8, 40));
        p.drawPath(path);
        p.setPen(QPen(fg, px(1.8)));
        p.drawRect(QRectF(px(18), px(22), px(12), px(10)));
        break;
      }
      case IconType::InputPort: {
        // Green inward chevron: ► with connection stub
        QPainterPath chevron;
        chevron.moveTo(px(10), px(13));
        chevron.lineTo(px(26), px(24));
        chevron.lineTo(px(10), px(35));
        chevron.closeSubpath();
        const QColor green(16, 185, 129);
        p.setPen(QPen(green, px(2.4), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(green);
        p.drawPath(chevron);

        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(26), px(24)), QPointF(px(38), px(24)));
        p.setBrush(fg);
        p.drawRect(QRectF(px(37), px(21), px(6), px(6)));
        break;
      }
      case IconType::OutputPort: {
        // Inward stub to amber outward chevron
        const QColor amber(245, 158, 11);
        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(6), px(24)), QPointF(px(18), px(24)));
        p.setBrush(fg);
        p.drawRect(QRectF(px(4), px(21), px(6), px(6)));

        QPainterPath chevron;
        chevron.moveTo(px(18), px(13));
        chevron.lineTo(px(34), px(24));
        chevron.lineTo(px(18), px(35));
        chevron.closeSubpath();
        p.setPen(QPen(amber, px(2.4), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(amber);
        p.drawPath(chevron);
        break;
      }
      case IconType::Wire: {
        // Orthogonal Manhattan routing line
        const QColor blue(59, 130, 246);
        p.setPen(QPen(blue, px(3.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath wire;
        wire.moveTo(px(10), px(34));
        wire.lineTo(px(24), px(34));
        wire.lineTo(px(24), px(14));
        wire.lineTo(px(38), px(14));
        p.drawPath(wire);

        p.setPen(Qt::NoPen);
        p.setBrush(blue);
        p.drawEllipse(QRectF(px(7), px(31), px(6), px(6)));
        p.drawEllipse(QRectF(px(35), px(11), px(6), px(6)));
        break;
      }
      case IconType::MergeFigures: {
        QPainterPath p1; p1.addRect(QRectF(px(8), px(8), px(20), px(20)));
        QPainterPath p2; p2.addRect(QRectF(px(18), px(18), px(22), px(22)));
        QPainterPath united = p1.united(p2);
        p.setPen(QPen(QColor(59, 130, 246), px(2.2), Qt::SolidLine));
        p.setBrush(QColor(59, 130, 246, 60));
        p.drawPath(united);
        break;
      }
      case IconType::BooleanIntersect: {
        p.setPen(QPen(fg, px(1.6), Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(8), px(8), px(20), px(20)));
        p.drawRect(QRectF(px(18), px(18), px(22), px(22)));
        QPainterPath p1; p1.addRect(QRectF(px(8), px(8), px(20), px(20)));
        QPainterPath p2; p2.addRect(QRectF(px(18), px(18), px(22), px(22)));
        QPainterPath inter = p1.intersected(p2);
        p.setPen(QPen(QColor(16, 185, 129), px(2.2), Qt::SolidLine));
        p.setBrush(QColor(16, 185, 129, 90));
        p.drawPath(inter);
        break;
      }
      case IconType::BooleanSubtract: {
        p.setPen(QPen(fg, px(1.6), Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(18), px(18), px(22), px(22)));
        QPainterPath p1; p1.addRect(QRectF(px(8), px(8), px(20), px(20)));
        QPainterPath p2; p2.addRect(QRectF(px(18), px(18), px(22), px(22)));
        QPainterPath diff = p1.subtracted(p2);
        p.setPen(QPen(QColor(239, 68, 68), px(2.2), Qt::SolidLine));
        p.setBrush(QColor(239, 68, 68, 80));
        p.drawPath(diff);
        break;
      }
      case IconType::BooleanXor: {
        QPainterPath p1; p1.addRect(QRectF(px(8), px(8), px(20), px(20)));
        QPainterPath p2; p2.addRect(QRectF(px(18), px(18), px(22), px(22)));
        QPainterPath xor_p = p1.united(p2).subtracted(p1.intersected(p2));
        xor_p.setFillRule(Qt::OddEvenFill);
        p.setPen(QPen(QColor(168, 85, 247), px(2.2), Qt::SolidLine));
        p.setBrush(QColor(168, 85, 247, 80));
        p.drawPath(xor_p);
        break;
      }
      case IconType::SplitFigure: {
        p.setPen(QPen(QColor(239, 68, 68), px(2.2)));
        p.setBrush(QColor(239, 68, 68, 40));
        p.drawRect(QRectF(px(9), px(9), px(12), px(12)));
        p.drawRect(QRectF(px(27), px(9), px(12), px(12)));
        p.drawRect(QRectF(px(9), px(27), px(12), px(12)));
        p.drawRect(QRectF(px(27), px(27), px(12), px(12)));
        break;
      }
      case IconType::Undo: {
        p.setPen(QPen(fg, px(3.0), Qt::SolidLine, Qt::RoundCap));
        QPainterPath arc;
        arc.arcMoveTo(QRectF(px(12), px(14), px(24), px(24)), 45);
        arc.arcTo(QRectF(px(12), px(14), px(24), px(24)), 45, 180);
        p.drawPath(arc);

        QPolygonF head;
        head << QPointF(px(8), px(26))
             << QPointF(px(17), px(20))
             << QPointF(px(17), px(32));
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawPolygon(head);
        break;
      }
      case IconType::Redo: {
        p.setPen(QPen(fg, px(3.0), Qt::SolidLine, Qt::RoundCap));
        QPainterPath arc;
        arc.arcMoveTo(QRectF(px(12), px(14), px(24), px(24)), 135);
        arc.arcTo(QRectF(px(12), px(14), px(24), px(24)), 135, -180);
        p.drawPath(arc);

        QPolygonF head;
        head << QPointF(px(40), px(26))
             << QPointF(px(31), px(20))
             << QPointF(px(31), px(32));
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawPolygon(head);
        break;
      }
      case IconType::Delete: {
        const QColor red(239, 68, 68);
        p.setPen(QPen(red, px(3.2), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(14), px(14)), QPointF(px(34), px(34)));
        p.drawLine(QPointF(px(34), px(14)), QPointF(px(14), px(34)));
        break;
      }
      case IconType::ZoomIn: {
        p.setPen(QPen(fg, px(2.6), Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(QRectF(px(9), px(9), px(22), px(22)));
        p.drawLine(QPointF(px(26), px(26)), QPointF(px(39), px(39)));
        p.drawLine(QPointF(px(14), px(20)), QPointF(px(26), px(20)));
        p.drawLine(QPointF(px(20), px(14)), QPointF(px(20), px(26)));
        break;
      }
      case IconType::ZoomOut: {
        p.setPen(QPen(fg, px(2.6), Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(QRectF(px(9), px(9), px(22), px(22)));
        p.drawLine(QPointF(px(26), px(26)), QPointF(px(39), px(39)));
        p.drawLine(QPointF(px(14), px(20)), QPointF(px(26), px(20)));
        break;
      }
      case IconType::FitContent: {
        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap));
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 40));
        p.drawRect(QRectF(px(16), px(16), px(16), px(16)));

        p.drawLine(QPointF(px(8), px(14)), QPointF(px(8), px(8)));
        p.drawLine(QPointF(px(8), px(8)), QPointF(px(14), px(8)));

        p.drawLine(QPointF(px(40), px(14)), QPointF(px(40), px(8)));
        p.drawLine(QPointF(px(40), px(8)), QPointF(px(34), px(8)));

        p.drawLine(QPointF(px(8), px(34)), QPointF(px(8), px(40)));
        p.drawLine(QPointF(px(8), px(40)), QPointF(px(14), px(40)));

        p.drawLine(QPointF(px(40), px(34)), QPointF(px(40), px(40)));
        p.drawLine(QPointF(px(40), px(40)), QPointF(px(34), px(40)));
        break;
      }
      case IconType::ShowGrid: {
        p.setPen(QPen(fg, px(2.0), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(10), px(18)), QPointF(px(38), px(18)));
        p.drawLine(QPointF(px(10), px(30)), QPointF(px(38), px(30)));
        p.drawLine(QPointF(px(18), px(10)), QPointF(px(18), px(38)));
        p.drawLine(QPointF(px(30), px(10)), QPointF(px(30), px(38)));
        break;
      }
      case IconType::Snapping: {
        const QColor red(239, 68, 68);
        const QColor blue(59, 130, 246);

        p.setPen(QPen(red, px(5.0), Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(px(14), px(11)), QPointF(px(14), px(25)));

        p.setPen(QPen(blue, px(5.0), Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(px(34), px(11)), QPointF(px(34), px(25)));

        p.setPen(QPen(fg, px(5.0), Qt::SolidLine, Qt::RoundCap));
        QPainterPath base;
        base.arcMoveTo(QRectF(px(11.5), px(15), px(25), px(22)), 180);
        base.arcTo(QRectF(px(11.5), px(15), px(25), px(22)), 180, 180);
        p.drawPath(base);
        break;
      }
      case IconType::DrillDown: {
        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath box;
        box.moveTo(px(9), px(20));
        box.lineTo(px(9), px(38));
        box.lineTo(px(39), px(38));
        box.lineTo(px(39), px(20));
        p.drawPath(box);

        const QColor cyan(6, 182, 212);
        p.setPen(QPen(cyan, px(3.2), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(24), px(8)), QPointF(px(24), px(28)));
        QPolygonF head;
        head << QPointF(px(24), px(33))
             << QPointF(px(16), px(22))
             << QPointF(px(32), px(22));
        p.setPen(Qt::NoPen);
        p.setBrush(cyan);
        p.drawPolygon(head);
        break;
      }
      case IconType::PopScope: {
        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath box;
        box.moveTo(px(9), px(22));
        box.lineTo(px(9), px(40));
        box.lineTo(px(39), px(40));
        box.lineTo(px(39), px(22));
        p.drawPath(box);

        const QColor orange(249, 115, 22);
        p.setPen(QPen(orange, px(3.2), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(24), px(15)), QPointF(px(24), px(33)));
        QPolygonF head;
        head << QPointF(px(24), px(8))
             << QPointF(px(16), px(19))
             << QPointF(px(32), px(19));
        p.setPen(Qt::NoPen);
        p.setBrush(orange);
        p.drawPolygon(head);
        break;
      }
      case IconType::FontBold: {
        QFont f = p.font();
        f.setPixelSize(px(28));
        f.setBold(true);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(0, 0, px(48), px(48)), Qt::AlignCenter, "B");
        break;
      }
      case IconType::FontItalic: {
        QFont f = p.font();
        f.setPixelSize(px(28));
        f.setItalic(true);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(0, 0, px(48), px(48)), Qt::AlignCenter, "I");
        break;
      }
      case IconType::FontUnderline: {
        QFont f = p.font();
        f.setPixelSize(px(26));
        f.setUnderline(true);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(0, 0, px(48), px(48)), Qt::AlignCenter, "U");
        break;
      }
      case IconType::TextColor: {
        QFont f = p.font();
        f.setPixelSize(px(24));
        f.setBold(true);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(0, px(2), px(48), px(30)), Qt::AlignCenter, "A");
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(239, 68, 68));
        p.drawRoundedRect(QRectF(px(10), px(36), px(28), px(5)), px(2), px(2));
        break;
      }
      case IconType::FillColor: {
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(59, 130, 246));
        QPolygonF bucket;
        bucket << QPointF(px(14), px(16))
               << QPointF(px(26), px(8))
               << QPointF(px(38), px(20))
               << QPointF(px(26), px(32));
        p.drawPolygon(bucket);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(59, 130, 246));
        p.drawRoundedRect(QRectF(px(8), px(38), px(32), px(5)), px(2), px(2));
        break;
      }
      case IconType::StrokeColor: {
        p.setPen(QPen(fg, px(3.0), Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(10), px(10), px(28), px(22)));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(16, 185, 129));
        p.drawRoundedRect(QRectF(px(8), px(38), px(32), px(5)), px(2), px(2));
        break;
      }
      case IconType::FileExport: {
        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(8), px(12), px(26), px(28)));
        const QColor cyan(6, 182, 212);
        p.setPen(QPen(cyan, px(3.0), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(22), px(26)), QPointF(px(40), px(26)));
        QPolygonF head;
        head << QPointF(px(42), px(26))
             << QPointF(px(32), px(20))
             << QPointF(px(32), px(32));
        p.setPen(Qt::NoPen);
        p.setBrush(cyan);
        p.drawPolygon(head);
        break;
      }
      case IconType::BringToFront: {
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(8), px(8), px(22), px(22)));

        const QColor blue(59, 130, 246);
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(blue);
        p.drawRect(QRectF(px(18), px(18), px(22), px(22)));
        break;
      }
      case IconType::SendToBack: {
        const QColor blue(59, 130, 246);
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(blue);
        p.drawRect(QRectF(px(8), px(8), px(22), px(22)));

        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(248, 250, 252));
        p.drawRect(QRectF(px(18), px(18), px(22), px(22)));
        break;
      }
      case IconType::BringForward: {
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(10), px(22), px(20), px(18)));

        const QColor green(34, 197, 94);
        p.setPen(QPen(green, px(3.0), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(36), px(36)), QPointF(px(36), px(14)));
        QPolygonF head;
        head << QPointF(px(36), px(8))
             << QPointF(px(30), px(18))
             << QPointF(px(42), px(18));
        p.setPen(Qt::NoPen);
        p.setBrush(green);
        p.drawPolygon(head);
        break;
      }
      case IconType::SendBackward: {
        p.setPen(QPen(fg, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(px(10), px(8), px(20), px(18)));

        const QColor orange(249, 115, 22);
        p.setPen(QPen(orange, px(3.0), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(36), px(12)), QPointF(px(36), px(34)));
        QPolygonF head;
        head << QPointF(px(36), px(40))
             << QPointF(px(30), px(30))
             << QPointF(px(42), px(30));
        p.setPen(Qt::NoPen);
        p.setBrush(orange);
        p.drawPolygon(head);
        break;
      }
      case IconType::Pan: {
        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(24), px(11)), QPointF(px(24), px(37)));
        p.drawLine(QPointF(px(11), px(24)), QPointF(px(37), px(24)));
        QPolygonF up; up << QPointF(px(24), px(7)) << QPointF(px(20), px(13)) << QPointF(px(28), px(13));
        QPolygonF dn; dn << QPointF(px(24), px(41)) << QPointF(px(20), px(35)) << QPointF(px(28), px(35));
        QPolygonF lt; lt << QPointF(px(7), px(24)) << QPointF(px(13), px(20)) << QPointF(px(13), px(28));
        QPolygonF rt; rt << QPointF(px(41), px(24)) << QPointF(px(35), px(20)) << QPointF(px(35), px(28));
        p.setBrush(fg);
        p.drawPolygon(up);
        p.drawPolygon(dn);
        p.drawPolygon(lt);
        p.drawPolygon(rt);
        break;
      }
      case IconType::RightTriangle: {
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        QPolygonF poly;
        poly << QPointF(px(10), px(10))
             << QPointF(px(38), px(38))
             << QPointF(px(10), px(38));
        p.drawPolygon(poly);
        p.drawLine(QPointF(px(10), px(30)), QPointF(px(18), px(30)));
        p.drawLine(QPointF(px(18), px(30)), QPointF(px(18), px(38)));
        break;
      }
      case IconType::Pentagon: {
        QPolygonF poly;
        const double cx = px(24), cy = px(25), r = px(16.5);
        for (int i = 0; i < 5; ++i) {
          const double rad = -M_PI / 2.0 + (2.0 * M_PI * i) / 5.0;
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Heptagon: {
        QPolygonF poly;
        const double cx = px(24), cy = px(24), r = px(16.5);
        for (int i = 0; i < 7; ++i) {
          const double rad = -M_PI / 2.0 + (2.0 * M_PI * i) / 7.0;
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Decagon: {
        QPolygonF poly;
        const double cx = px(24), cy = px(24), r = px(16.5);
        for (int i = 0; i < 10; ++i) {
          const double rad = -M_PI / 2.0 + (2.0 * M_PI * i) / 10.0;
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Dodecagon: {
        QPolygonF poly;
        const double cx = px(24), cy = px(24), r = px(16.5);
        for (int i = 0; i < 12; ++i) {
          const double rad = -M_PI / 2.0 + (2.0 * M_PI * i) / 12.0;
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Star: {
        QPolygonF poly;
        const double cx = px(24), cy = px(25), r_out = px(17), r_in = px(7.5);
        for (int i = 0; i < 10; ++i) {
          const double r = (i % 2 == 0) ? r_out : r_in;
          const double rad = -M_PI / 2.0 + (M_PI * i) / 5.0;
          poly << QPointF(cx + r * std::cos(rad), cy + r * std::sin(rad));
        }
        p.setBrush(QColor(234, 179, 8, 80));
        p.setPen(QPen(QColor(234, 179, 8), px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Cross: {
        QPolygonF poly;
        poly << QPointF(px(18), px(8)) << QPointF(px(30), px(8))
             << QPointF(px(30), px(18)) << QPointF(px(40), px(18))
             << QPointF(px(40), px(30)) << QPointF(px(30), px(30))
             << QPointF(px(30), px(40)) << QPointF(px(18), px(40))
             << QPointF(px(18), px(30)) << QPointF(px(8), px(30))
             << QPointF(px(8), px(18)) << QPointF(px(18), px(18));
        p.setBrush(QColor(239, 68, 68, 60));
        p.setPen(QPen(QColor(239, 68, 68), px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Trapezoid: {
        QPolygonF poly;
        poly << QPointF(px(15), px(12))
             << QPointF(px(33), px(12))
             << QPointF(px(41), px(36))
             << QPointF(px(7), px(36));
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Parallelogram: {
        QPolygonF poly;
        poly << QPointF(px(16), px(12))
             << QPointF(px(41), px(12))
             << QPointF(px(32), px(36))
             << QPointF(px(7), px(36));
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Chevron: {
        QPolygonF poly;
        poly << QPointF(px(8), px(18))
             << QPointF(px(26), px(18))
             << QPointF(px(26), px(10))
             << QPointF(px(42), px(24))
             << QPointF(px(26), px(38))
             << QPointF(px(26), px(30))
             << QPointF(px(8), px(30));
        p.setBrush(QColor(59, 130, 246, 60));
        p.setPen(QPen(QColor(59, 130, 246), px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolygon(poly);
        break;
      }
      case IconType::Cylinder: {
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 35));
        QPainterPath cyl_path;
        cyl_path.moveTo(px(10), px(18));
        cyl_path.lineTo(px(10), px(34));
        cyl_path.arcTo(QRectF(px(10), px(28), px(28), px(12)), 180, 180);
        cyl_path.lineTo(px(38), px(18));
        cyl_path.closeSubpath();
        p.drawPath(cyl_path);
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 60));
        p.drawEllipse(QRectF(px(10), px(12), px(28), px(12)));
        break;
      }
      case IconType::BidirectionalPort: {
        const QColor purple(168, 85, 247);
        p.setPen(QPen(purple, px(2.2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(purple);
        QPolygonF left_chev;
        left_chev << QPointF(px(6), px(24)) << QPointF(px(16), px(15)) << QPointF(px(16), px(33));
        p.drawPolygon(left_chev);
        QPolygonF right_chev;
        right_chev << QPointF(px(42), px(24)) << QPointF(px(32), px(15)) << QPointF(px(32), px(33));
        p.drawPolygon(right_chev);
        p.setPen(QPen(fg, px(2.4), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px(16), px(24)), QPointF(px(32), px(24)));
        break;
      }
      case IconType::PolygonDraw: {
        QPolygonF poly;
        poly << QPointF(px(10), px(14))
             << QPointF(px(28), px(8))
             << QPointF(px(40), px(22))
             << QPointF(px(32), px(38))
             << QPointF(px(14), px(34));
        p.setBrush(QColor(fg.red(), fg.green(), fg.blue(), 30));
        p.drawPolygon(poly);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(59, 130, 246));
        for (int i = 0; i < poly.size(); ++i) {
          p.drawEllipse(QRectF(poly[i].x() - px(3), poly[i].y() - px(3), px(6), px(6)));
        }
        break;
      }
      case IconType::AngleSnap: {
        p.setPen(QPen(fg, px(2.5), Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(px(8), px(8), px(32), px(32)), 0 * 16, 90 * 16);
        p.drawLine(QPointF(px(24), px(24)), QPointF(px(40), px(24)));
        p.drawLine(QPointF(px(24), px(24)), QPointF(px(24), px(8)));
        QPolygonF arrow;
        arrow << QPointF(px(21), px(11)) << QPointF(px(24), px(6)) << QPointF(px(27), px(11));
        p.setBrush(fg);
        p.drawPolygon(arrow);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(px(32), px(8), px(6), px(6)));
        break;
      }
      case IconType::RotateCW: {
        p.setPen(QPen(fg, px(2.8), Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(px(10), px(10), px(28), px(28)), 30 * 16, 270 * 16);
        QPolygonF arrow;
        arrow << QPointF(px(32), px(18)) << QPointF(px(40), px(20)) << QPointF(px(36), px(28));
        p.setBrush(fg);
        p.drawPolygon(arrow);
        break;
      }
      case IconType::RotateCCW: {
        p.setPen(QPen(fg, px(2.8), Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(px(10), px(10), px(28), px(28)), -120 * 16, 270 * 16);
        QPolygonF arrow;
        arrow << QPointF(px(16), px(18)) << QPointF(px(8), px(20)) << QPointF(px(12), px(28));
        p.setBrush(fg);
        p.drawPolygon(arrow);
        break;
      }
    }

    return pixmap;
  }

  static QIcon create_icon(IconType type, const QColor& fg) {
    QIcon icon;
    icon.addPixmap(render_pixmap(type, fg, 48), QIcon::Normal, QIcon::Off);
    icon.addPixmap(render_pixmap(type, fg, 24), QIcon::Normal, QIcon::Off);

    QColor active_col = fg;
    active_col.setAlpha(255);
    icon.addPixmap(render_pixmap(type, active_col, 48), QIcon::Active, QIcon::On);

    QColor dis_col = fg;
    dis_col.setAlpha(80);
    icon.addPixmap(render_pixmap(type, dis_col, 48), QIcon::Disabled, QIcon::Off);
    return icon;
  }
};

} // namespace vector::gui

#endif // VECTOR_ENABLE_QT

#endif // VECTOR_GUI_ICON_FACTORY_HPP
