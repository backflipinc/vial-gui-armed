# SPDX-License-Identifier: GPL-2.0-or-later
# The HUD page's editing surface: the screen at an integer zoom, selection, moving with smart guides, resizing
from PyQt5.QtCore import Qt, pyqtSignal, QRect, QPoint
from PyQt5.QtGui import QPainter, QColor, QPen
from PyQt5.QtWidgets import QWidget

import armed

HANDLE = 9


class HudCanvas(QWidget):

    selection_changed = pyqtSignal()
    edit_started = pyqtSignal()
    edited = pyqtSignal()
    context_requested = pyqtSignal(QPoint)
    cursor_moved = pyqtSignal(int, int)  # screen px, -1 when outside
    nudge = pyqtSignal(int, int)
    activated = pyqtSignal(int)  # double click on a widget

    def __init__(self):
        super().__init__()
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMouseTracking(True)
        self.image = None
        self.scale = 2
        self.w, self.h = 320, 172
        self.hud = None
        self.areas = []
        self.sel = []  # widget indexes, the primary one last
        self.hover = -1
        self.drag = None
        self.band = None
        self.guides = []  # ("x" | "y", screen px)
        self.snap = True
        self.show_bounds = False

    # ---- setup

    def set_screen(self, w, h):
        self.w, self.h = w, h
        self.set_scale(self.scale)

    def set_scale(self, s):
        self.scale = max(1, min(6, s))
        self.setFixedSize(self.w * self.scale + 2, self.h * self.scale + 2)
        self.update()

    def set_content(self, image, hud, areas):
        self.image = image
        self.hud = hud
        self.areas = areas
        self.update()

    @property
    def primary(self):
        return self.sel[-1] if self.sel else -1

    # ---- geometry

    def to_px(self, pos):
        return (pos.x() - 1) // self.scale, (pos.y() - 1) // self.scale

    def rect_of(self, a):
        s = self.scale
        return QRect(1 + a[0] * s, 1 + a[1] * s, (a[2] - a[0] + 1) * s, (a[3] - a[1] + 1) * s)

    def handle(self):
        if len(self.sel) == 1 and self.area(self.sel[0]):
            r = self.rect_of(self.area(self.sel[0]))
            return QRect(r.right() - HANDLE // 2, r.bottom() - HANDLE // 2, HANDLE, HANDLE)
        return None

    def area(self, i):
        return self.areas[i] if 0 <= i < len(self.areas) else None

    def pick(self, x, y):
        for i in range(len(self.areas) - 1, -1, -1):
            a = self.areas[i]
            if a and a[0] <= x <= a[2] and a[1] <= y <= a[3]:
                return i
        return -1

    def targets(self, exclude):
        """ edges and centres to snap to: the screen's and the other widgets' """
        xs, ys = [0, self.w // 2, self.w], [0, self.h // 2, self.h]
        for i, a in enumerate(self.areas):
            if a and i not in exclude:
                xs += [a[0], (a[0] + a[2] + 1) // 2, a[2] + 1]
                ys += [a[1], (a[1] + a[3] + 1) // 2, a[3] + 1]
        return xs, ys

    def snap_axis(self, edges, targets):
        """ the shift that brings the closest of `edges` onto a target, and the target """
        limit = max(2, 8 // self.scale)
        best = None
        for e in edges:
            for t in targets:
                d = t - e
                if abs(d) <= limit and (best is None or abs(d) < abs(best[0])):
                    best = (d, t)
        return best

    # ---- painting

    def paintEvent(self, ev):
        qp = QPainter(self)
        qp.fillRect(self.rect(), QColor("#000"))
        qp.setPen(QColor(armed.LINE2))
        qp.drawRect(self.rect().adjusted(0, 0, -1, -1))
        if self.image is not None:
            qp.drawImage(QRect(1, 1, self.w * self.scale, self.h * self.scale), self.image)
        if self.show_bounds:
            qp.setPen(QPen(QColor(armed.DIM), 1, Qt.DotLine))
            for a in self.areas:
                if a:
                    qp.drawRect(self.rect_of(a).adjusted(0, 0, -1, -1))
        a = self.area(self.hover)
        if a and self.hover not in self.sel and self.drag is None:
            qp.setPen(QPen(QColor(armed.MID), 1, Qt.DashLine))
            qp.drawRect(self.rect_of(a).adjusted(0, 0, -1, -1))
        for i in self.sel:
            a = self.area(i)
            if a:
                solid = i == self.primary and len(self.sel) == 1
                qp.setPen(QPen(QColor(armed.ACC), 1, Qt.SolidLine if solid else Qt.DashLine))
                qp.drawRect(self.rect_of(a).adjusted(0, 0, -1, -1))
        h = self.handle()
        if h is not None:
            qp.fillRect(h, QColor(armed.ACC))
        qp.setPen(QPen(QColor(armed.ACC), 1, Qt.DashLine))
        for axis, v in self.guides:
            p = 1 + v * self.scale
            if axis == "x":
                qp.drawLine(p, 0, p, self.height())
            else:
                qp.drawLine(0, p, self.width(), p)
        if self.band is not None:
            qp.setPen(QPen(QColor(armed.ACC), 1, Qt.DotLine))
            qp.fillRect(self.band_rect(), QColor(255, 170, 0, 30))
            qp.drawRect(self.band_rect())
        qp.end()

    def band_rect(self):
        (x0, y0), (x1, y1) = self.band
        return QRect(QPoint(min(x0, x1), min(y0, y1)), QPoint(max(x0, x1), max(y0, y1)))

    # ---- mouse

    def mousePressEvent(self, ev):
        self.setFocus()
        if ev.button() == Qt.RightButton:
            i = self.pick(*self.to_px(ev.pos()))
            if i >= 0 and i not in self.sel:
                self.sel = [i]
                self.selection_changed.emit()
            self.context_requested.emit(ev.globalPos())
            return
        if ev.button() != Qt.LeftButton or self.hud is None:
            return
        x, y = self.to_px(ev.pos())
        h = self.handle()
        if h is not None and h.contains(ev.pos()):
            i = self.sel[0]
            a = self.areas[i]
            self.drag = ("size", x, y, {i: (a[2] - a[0] + 1, a[3] - a[1] + 1)}, False)
            return
        i = self.pick(x, y)
        additive = ev.modifiers() & (Qt.ShiftModifier | Qt.ControlModifier)
        if additive:
            if i >= 0:
                if i in self.sel:
                    self.sel.remove(i)
                else:
                    self.sel.append(i)
                self.selection_changed.emit()
            else:
                self.band = (ev.pos(), ev.pos())
                self.band_base = list(self.sel)
            return
        if i < 0:
            self.sel = []
            self.selection_changed.emit()
            self.band = (ev.pos(), ev.pos())
            self.band_base = []
            return
        # a click on one of several selected widgets narrows the selection to it unless it turns into a drag
        self.click_only = i if i in self.sel and len(self.sel) > 1 else -1
        if i not in self.sel:
            self.sel = [i]
        else:
            self.sel.remove(i)
            self.sel.append(i)
        self.selection_changed.emit()
        start = {j: (self.hud.widgets[j].x, self.hud.widgets[j].y) for j in self.sel}
        self.drag = ("move", x, y, start, False)

    def mouseMoveEvent(self, ev):
        x, y = self.to_px(ev.pos())
        inside = 0 <= x < self.w and 0 <= y < self.h
        self.cursor_moved.emit(x if inside else -1, y if inside else -1)
        if self.band is not None:
            self.band = (self.band[0], ev.pos())
            r = self.band_rect()
            picked = [i for i, a in enumerate(self.areas) if a and r.intersects(self.rect_of(a))]
            sel = list(self.band_base) + [i for i in picked if i not in self.band_base]
            if sel != self.sel:
                self.sel = sel
                self.selection_changed.emit()
            self.update()
            return
        if self.drag is None:
            h = self.handle()
            self.setCursor(Qt.SizeFDiagCursor if h is not None and h.contains(ev.pos()) else
                           Qt.SizeAllCursor if self.pick(x, y) in self.sel else Qt.ArrowCursor)
            hover = self.pick(x, y)
            if hover != self.hover:
                self.hover = hover
                self.update()
            return
        kind, x0, y0, start, started = self.drag
        dx, dy = x - x0, y - y0
        if not started:
            if dx == 0 and dy == 0:
                return
            self.edit_started.emit()
            self.drag = (kind, x0, y0, start, True)
        snap = self.snap and not ev.modifiers() & Qt.AltModifier
        self.guides = []
        if kind == "move":
            boxes = [self.areas[j] for j in start if self.areas[j]]
            if boxes and snap:
                l = min(b[0] for b in boxes) + dx
                r = max(b[2] for b in boxes) + 1 + dx
                t = min(b[1] for b in boxes) + dy
                b_ = max(b[3] for b in boxes) + 1 + dy
                xs, ys = self.targets(set(start))
                sx = self.snap_axis([l, (l + r) // 2, r], xs)
                sy = self.snap_axis([t, (t + b_) // 2, b_], ys)
                if sx:
                    dx += sx[0]
                    self.guides.append(("x", sx[1]))
                if sy:
                    dy += sy[0]
                    self.guides.append(("y", sy[1]))
            for j, (wx, wy) in start.items():
                wd = self.hud.widgets[j]
                wd.x = max(0, min(self.w - 1, wx + dx))
                wd.y = max(0, min(self.h - 1, wy + dy))
        else:
            (j, (w0, h0)), = start.items()
            wd = self.hud.widgets[j]
            nw, nh = w0 + dx, h0 + dy
            if snap:
                xs, ys = self.targets({j})
                sx = self.snap_axis([wd.x + nw], xs)
                sy = self.snap_axis([wd.y + nh], ys)
                if sx:
                    nw += sx[0]
                    self.guides.append(("x", sx[1]))
                if sy:
                    nh += sy[0]
                    self.guides.append(("y", sy[1]))
            wd.w = max(1, min(self.w - wd.x, nw))
            wd.h = max(1, min(self.h - wd.y, nh))
        self.edited.emit()

    def mouseReleaseEvent(self, ev):
        if self.drag is not None and not self.drag[4] and getattr(self, "click_only", -1) >= 0:
            self.sel = [self.click_only]
            self.selection_changed.emit()
        self.click_only = -1
        self.drag = None
        self.band = None
        self.guides = []
        self.update()

    def mouseDoubleClickEvent(self, ev):
        i = self.pick(*self.to_px(ev.pos()))
        if i >= 0:
            self.activated.emit(i)

    def leaveEvent(self, ev):
        self.hover = -1
        self.cursor_moved.emit(-1, -1)
        self.update()

    def keyPressEvent(self, ev):
        step = 8 if ev.modifiers() & Qt.ShiftModifier else 1
        d = {Qt.Key_Left: (-step, 0), Qt.Key_Right: (step, 0), Qt.Key_Up: (0, -step), Qt.Key_Down: (0, step)}.get(ev.key())
        if d is None or not self.sel:
            return super().keyPressEvent(ev)
        self.nudge.emit(*d)
