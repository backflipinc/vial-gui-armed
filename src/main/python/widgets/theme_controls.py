# SPDX-License-Identifier: GPL-2.0-or-later
# Controls of the Themes page: colour button, direction dial, layer list
import math

from PyQt5.QtCore import Qt, pyqtSignal, QSize, QRect, QPointF
from PyQt5.QtGui import QPainter, QColor, QPen, QBrush, QPolygonF, QLinearGradient
from PyQt5.QtWidgets import QWidget, QMenu, QWidgetAction, QColorDialog, QStyledItemDelegate, QStyle, QListWidget

import armed
from lighting_theme import TYPE_BY_ID, RAINBOW, PAINT, F_RADIAL

PALETTE = [
    (255, 255, 255), (255, 170, 0), (255, 90, 0), (255, 0, 0), (255, 0, 90), (255, 0, 255), (150, 0, 255),
    (60, 0, 255), (0, 0, 255), (0, 120, 255), (0, 200, 255), (0, 255, 200), (0, 255, 60), (120, 255, 0),
    (255, 255, 0), (255, 200, 120), (120, 60, 0), (60, 60, 60), (20, 20, 20), (0, 0, 0),
]


class PaletteGrid(QWidget):

    picked = pyqtSignal(tuple)

    CELL, GAP, COLS = 20, 5, 10

    def __init__(self, current=None):
        super().__init__()
        self.current = current
        self.setCursor(Qt.PointingHandCursor)
        rows = (len(PALETTE) + self.COLS - 1) // self.COLS
        self.setFixedSize(self.COLS * (self.CELL + self.GAP) + 12, rows * (self.CELL + self.GAP) + 12)

    def cell(self, i):
        return QRect(8 + (i % self.COLS) * (self.CELL + self.GAP), 8 + (i // self.COLS) * (self.CELL + self.GAP),
                     self.CELL, self.CELL)

    def paintEvent(self, ev):
        qp = QPainter(self)
        for i, c in enumerate(PALETTE):
            r = self.cell(i)
            qp.fillRect(r, QColor(*c))
            pen = QPen(QColor(armed.ACC if c == self.current else armed.LINE2))
            pen.setWidth(2 if c == self.current else 1)
            qp.setPen(pen)
            qp.drawRect(r.adjusted(0, 0, -1, -1))
        qp.end()

    def mousePressEvent(self, ev):
        for i, c in enumerate(PALETTE):
            if self.cell(i).contains(ev.pos()):
                self.picked.emit(c)
                return


class ColorButton(QWidget):
    changed = pyqtSignal(tuple)

    def __init__(self, label=""):
        super().__init__()
        self.label = label
        self.color = (0, 0, 0)
        self.setCursor(Qt.PointingHandCursor)
        self.setFixedHeight(30)
        self.setMinimumWidth(118)

    def set_color(self, c):
        self.color = tuple(c)
        self.update()

    def sizeHint(self):
        return QSize(130, 30)

    def paintEvent(self, ev):
        qp = QPainter(self)
        r = self.rect().adjusted(0, 0, -1, -1)
        qp.fillRect(r, QColor(armed.KEY))
        qp.setPen(QColor(armed.ACC if self.underMouse() else armed.LINE2))
        qp.drawRect(r)
        chip = QRect(5, 5, 26, r.height() - 9)
        qp.fillRect(chip, QColor(*self.color))
        qp.setPen(QColor(armed.LINE2))
        qp.drawRect(chip)
        qp.setFont(armed.mono(12))
        qp.setPen(QColor(armed.DIM))
        qp.drawText(r.adjusted(38, 0, -6, 0), Qt.AlignVCenter | Qt.AlignLeft, self.label)
        qp.setPen(QColor(armed.TXT))
        qp.drawText(r.adjusted(38, 0, -6, 0), Qt.AlignVCenter | Qt.AlignRight, "#{:02X}{:02X}{:02X}".format(*self.color))
        qp.end()

    def enterEvent(self, ev):
        self.update()

    def leaveEvent(self, ev):
        self.update()

    def mousePressEvent(self, ev):
        menu = QMenu(self)
        grid = PaletteGrid(self.color)
        act = QWidgetAction(menu)
        act.setDefaultWidget(grid)
        menu.addAction(act)
        menu.addSeparator()
        custom = menu.addAction("ANY COLOUR…")
        grid.picked.connect(lambda c: (menu.close(), self.pick(c)))
        chosen = menu.exec_(self.mapToGlobal(self.rect().bottomLeft()))
        if chosen is custom:
            c = QColorDialog.getColor(QColor(*self.color), self, "Colour")
            if c.isValid():
                self.pick((c.red(), c.green(), c.blue()))

    def pick(self, c):
        self.set_color(c)
        self.changed.emit(self.color)


class DirectionDial(QWidget):
    """ 0..255 = a full turn, 0 points right, 64 down; snaps to the 8 compass points unless Shift is held """

    changed = pyqtSignal(int)

    def __init__(self):
        super().__init__()
        self.value = 0
        self.radial = False
        self.setFixedSize(86, 86)
        self.setCursor(Qt.PointingHandCursor)

    def set_value(self, v, radial=False):
        self.value, self.radial = v & 0xFF, radial
        self.update()

    def paintEvent(self, ev):
        qp = QPainter(self)
        qp.setRenderHint(QPainter.Antialiasing)
        c = QPointF(self.width() / 2, self.height() / 2)
        rad = min(self.width(), self.height()) / 2 - 6
        qp.setPen(QPen(QColor(armed.LINE2), 1))
        qp.setBrush(QColor(armed.KEY))
        qp.drawEllipse(c, rad, rad)
        for k in range(8):
            a = k * math.pi / 4
            p1 = c + QPointF(math.cos(a), math.sin(a)) * (rad - 4)
            p2 = c + QPointF(math.cos(a), math.sin(a)) * rad
            qp.drawLine(p1, p2)
        acc = QColor(armed.DIM if self.radial else armed.ACC)
        a = self.value * 2 * math.pi / 256
        tip = c + QPointF(math.cos(a), math.sin(a)) * (rad - 8)
        tail = c - QPointF(math.cos(a), math.sin(a)) * (rad - 8)
        qp.setPen(QPen(acc, 2))
        qp.drawLine(tail, tip)
        side = QPointF(-math.sin(a), math.cos(a)) * 6
        back = c + QPointF(math.cos(a), math.sin(a)) * (rad - 20)
        qp.setBrush(acc)
        qp.drawPolygon(QPolygonF([tip, back + side, back - side]))
        qp.end()

    def mousePressEvent(self, ev):
        self.drag(ev)

    def mouseMoveEvent(self, ev):
        if ev.buttons() & Qt.LeftButton:
            self.drag(ev)

    def drag(self, ev):
        dx, dy = ev.x() - self.width() / 2, ev.y() - self.height() / 2
        if dx * dx + dy * dy < 16:
            return
        v = int(round(math.atan2(dy, dx) * 128 / math.pi)) & 0xFF
        if not ev.modifiers() & Qt.ShiftModifier:
            snap = int(round(v / 32)) * 32 & 0xFF
            if min(abs(v - snap), 256 - abs(v - snap)) <= 6:
                v = snap
        if v != self.value or self.radial:
            self.value = v
            self.update()
            self.changed.emit(v)


def layer_swatch(qp, r, layer):
    if layer.type == RAINBOW:
        g = QLinearGradient(r.left(), 0, r.right(), 0)
        for k in range(7):
            g.setColorAt(k / 6, QColor.fromHsv(k * 359 // 6, 255, 255))
        qp.fillRect(r, QBrush(g))
    elif layer.type == PAINT:
        w = r.width() // 4
        for k, c in enumerate(((255, 170, 0), (0, 200, 255), (255, 0, 90), (120, 255, 0))):
            qp.fillRect(QRect(r.left() + k * w, r.top(), w, r.height()), QColor(*c))
    elif "b" in TYPE_BY_ID[layer.type].controls:
        g = QLinearGradient(r.left(), 0, r.right(), 0)
        g.setColorAt(0, QColor(*layer.a))
        g.setColorAt(1, QColor(*layer.b))
        qp.fillRect(r, QBrush(g))
    else:
        qp.fillRect(r, QColor(*layer.a))


class LayerDelegate(QStyledItemDelegate):

    EYE = 26

    def paint(self, qp, option, index):
        layer = index.data(Qt.UserRole)
        if layer is None:
            return
        qp.save()
        qp.setRenderHint(QPainter.Antialiasing, False)
        selected = option.state & QStyle.State_Selected
        r = option.rect.adjusted(2, 2, -4, -2)
        if selected:
            qp.setPen(QColor(armed.ACC))
            qp.drawRect(r.adjusted(0, 0, -1, -1))
            qp.fillRect(QRect(r.left(), r.top(), 4, r.height()), QColor(armed.ACC))
        elif option.state & QStyle.State_MouseOver:
            qp.setPen(QColor(armed.LINE2))
            qp.drawRect(r.adjusted(0, 0, -1, -1))
        dim = layer.hidden
        qp.setFont(armed.mono(12))
        qp.setPen(QColor(armed.ACC if selected else armed.DIM))
        qp.drawText(r.adjusted(12, 0, 0, 0), Qt.AlignVCenter, "{:02d}".format(index.data(Qt.UserRole + 1)))
        sw = QRect(r.left() + 40, r.center().y() - 6, 34, 12)
        layer_swatch(qp, sw, layer)
        qp.setPen(QColor(armed.LINE2))
        qp.drawRect(sw)
        qp.setFont(armed.head(15))
        qp.setPen(QColor(armed.DIM if dim else (armed.ACC if selected else armed.TXT)))
        qp.drawText(r.adjusted(84, 0, -self.EYE - 6, 0), Qt.AlignVCenter, index.data(Qt.DisplayRole))
        eye = self.eye_rect(option.rect)
        qp.setPen(QColor(armed.DIM if dim else armed.ACC))
        qp.setFont(armed.mono(11))
        qp.drawText(eye, Qt.AlignCenter, "OFF" if dim else "ON")
        qp.restore()

    def eye_rect(self, rect):
        return QRect(rect.right() - self.EYE - 10, rect.top() + 4, self.EYE + 4, rect.height() - 8)

    def sizeHint(self, option, index):
        return QSize(260, 34)


class LayerList(QListWidget):
    """ Top layer first; clicking ON / OFF toggles a layer without selecting it """

    toggled = pyqtSignal(int)

    def __init__(self):
        super().__init__()
        self.setObjectName("armedList")
        self.delegate = LayerDelegate(self)
        self.setItemDelegate(self.delegate)
        self.setMouseTracking(True)

    def mousePressEvent(self, ev):
        item = self.itemAt(ev.pos())
        if item is not None and self.delegate.eye_rect(self.visualItemRect(item)).contains(ev.pos()):
            self.toggled.emit(self.row(item))
            return
        super().mousePressEvent(ev)


def layer_summary(layer):
    t = TYPE_BY_ID.get(layer.type)
    name = t.name if t else "?"
    if t and "dir" in t.controls and layer.flag(F_RADIAL):
        name += " \u25ce"
    return name
