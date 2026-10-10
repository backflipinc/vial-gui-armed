# SPDX-License-Identifier: GPL-2.0-or-later
# Building blocks of the Armed pages: bracketed frames, palette swatches, segmented switches, light bar

from PyQt5.QtCore import Qt, pyqtSignal, QSize, QRect
from PyQt5.QtGui import QPainter, QColor, QPen
from PyQt5.QtWidgets import QFrame, QWidget, QHBoxLayout, QPushButton, QButtonGroup, QLabel, QVBoxLayout

import armed


class BracketFrame(QFrame):
    """ Panel with the corner brackets of the design code """

    def __init__(self, title=None, accent=False):
        super().__init__()
        self.accent = accent
        self.resized = None
        self.setObjectName("armedFrame")
        self.box = QVBoxLayout(self)
        self.box.setContentsMargins(16, 12, 16, 12)
        self.box.setSpacing(9)
        self.title = None
        if title is not None:
            self.title = QLabel(title)
            self.title.setObjectName("armedFrameTitle")
            self.header = QHBoxLayout()
            self.header.setSpacing(10)
            self.header.addWidget(self.title)
            self.header.addStretch(1)
            self.box.addLayout(self.header)

    def resizeEvent(self, ev):
        super().resizeEvent(ev)
        if self.resized is not None:
            self.resized()

    def set_accent(self, accent):
        self.accent = accent
        self.update()

    def paintEvent(self, ev):
        qp = QPainter(self)
        r = self.rect().adjusted(0, 0, -1, -1)
        qp.fillRect(r, QColor(armed.PANEL))
        qp.setPen(QColor(armed.LINE))
        qp.drawRect(r.adjusted(4, 4, -4, -4))
        pen = QPen(QColor(armed.ACC if self.accent else armed.MID))
        pen.setWidth(2)
        qp.setPen(pen)
        n = 12
        x0, y0, x1, y1 = r.left() + 1, r.top() + 1, r.right(), r.bottom()
        for (x, y, dx, dy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
            qp.drawLine(x, y, x + dx * n, y)
            qp.drawLine(x, y, x, y + dy * n)
        qp.end()


class Swatches(QWidget):
    """ Row of preset colours (IO84Color list); emits the index """

    changed = pyqtSignal(int)

    SIZE = 18
    GAP = 7

    def __init__(self):
        super().__init__()
        self.colors = []
        self.current = -1
        self.setMouseTracking(True)
        self.setCursor(Qt.PointingHandCursor)

    def set_colors(self, colors):
        self.colors = colors
        self.updateGeometry()
        self.update()

    def set_current(self, idx):
        self.current = idx
        self.update()

    def sizeHint(self):
        n = max(1, len(self.colors))
        return QSize(n * (self.SIZE + self.GAP) + 4, self.SIZE + 8)

    minimumSizeHint = sizeHint

    def cell(self, i):
        return QRect(4 + i * (self.SIZE + self.GAP), 4, self.SIZE, self.SIZE)

    def paintEvent(self, ev):
        qp = QPainter(self)
        for i, c in enumerate(self.colors):
            r = self.cell(i)
            qp.fillRect(r, swatch_color(c))
            qp.setPen(QColor(armed.LINE2))
            qp.drawRect(r.adjusted(0, 0, -1, -1))
            if i == self.current:
                pen = QPen(QColor(armed.ACC))
                pen.setWidth(2)
                qp.setPen(pen)
                qp.drawRect(r.adjusted(-3, -3, 2, 2))
        qp.end()

    def mousePressEvent(self, ev):
        for i in range(len(self.colors)):
            if self.cell(i).adjusted(-3, -3, 3, 3).contains(ev.pos()):
                self.set_current(i)
                self.changed.emit(i)
                return

    def event(self, ev):
        if ev.type() == ev.ToolTip:
            for i, c in enumerate(self.colors):
                if self.cell(i).contains(ev.pos()):
                    self.setToolTip(c.name)
                    break
        return super().event(ev)


def swatch_color(c):
    if c.black:
        return QColor(0, 0, 0)
    return QColor.fromHsv(int(c.h * 359 / 255), c.s, 255)


class Segmented(QWidget):
    """ Exclusive row of buttons; emits the index """

    changed = pyqtSignal(int)

    def __init__(self, labels=()):
        super().__init__()
        self.setObjectName("armedSeg")
        self.lay = QHBoxLayout(self)
        self.lay.setContentsMargins(0, 0, 0, 0)
        self.lay.setSpacing(0)
        self.group = QButtonGroup(self)
        self.group.setExclusive(True)
        self.group.buttonClicked[int].connect(self.changed.emit)
        self.set_labels(labels)

    def set_labels(self, labels):
        for b in self.group.buttons():
            self.group.removeButton(b)
            b.deleteLater()
        for i, text in enumerate(labels):
            b = QPushButton(text)
            b.setCheckable(True)
            b.setCursor(Qt.PointingHandCursor)
            self.group.addButton(b, i)
            self.lay.addWidget(b)

    def set_current(self, idx):
        b = self.group.button(idx)
        if b is not None:
            b.setChecked(True)


class StripView(QWidget):
    """ Preview of the 9-LED light bar """

    def __init__(self):
        super().__init__()
        self.colors = [(0, 0, 0)] * 9
        self.setFixedHeight(16)

    def set_colors(self, colors):
        self.colors = colors
        self.update()

    def sizeHint(self):
        return QSize(9 * 30, 16)

    def paintEvent(self, ev):
        qp = QPainter(self)
        n = len(self.colors)
        w = min(26, (self.width() - (n - 1) * 6) // n)
        for i, rgb in enumerate(self.colors):
            r = QRect(i * (w + 6), 4, w, 8)
            c = QColor(*rgb)
            if max(rgb) < 8:
                qp.fillRect(r, QColor(armed.LINE))
            else:
                glow = QColor(c)
                glow.setAlpha(70)
                qp.fillRect(r.adjusted(-2, -3, 2, 3), glow)
                qp.fillRect(r, c)
        qp.end()
