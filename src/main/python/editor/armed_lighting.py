# SPDX-License-Identifier: GPL-2.0-or-later
# Lighting page for keyboards with the IO84 channel: every effect, both colours, Fn overlay, light bar, live preview
import random
import time

from PyQt5.QtCore import Qt, QTimer, QSize, QRect
from PyQt5.QtGui import QColor
from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QGridLayout, QLabel, QSlider, QListWidget, QListWidgetItem, \
    QStyledItemDelegate, QStyle, QPushButton, QCheckBox

import armed
from editor.basic_editor import BasicEditor
from effects_preview import EffectPreview, StripPreview, TWO_COLOR, effect_key
from protocol.io84 import V_ENABLED, V_EFFECT, V_COLOR2, V_FN_COLOR, V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS, \
    V_HUE, V_SAT, V_VAL, V_SPEED
from vial_device import VialKeyboard
from widgets.armed_controls import BracketFrame, Swatches, Segmented, StripView
from widgets.keyboard_widget import KeyboardWidget
from util import tr

FPS = 30
HIDDEN_EFFECTS = {"VIALRGB_DIRECT"}
DEMO_PRESS_MS = 420


class EffectDelegate(QStyledItemDelegate):

    def paint(self, qp, option, index):
        qp.save()
        selected = option.state & QStyle.State_Selected
        r = option.rect.adjusted(6, 1, -10, -1)
        if selected:
            qp.setPen(QColor(armed.ACC))
            qp.drawRect(r.adjusted(0, 0, -1, -1))
            qp.fillRect(QRect(r.left(), r.top(), 4, r.height()), QColor(armed.ACC))
        elif option.state & QStyle.State_MouseOver:
            qp.setPen(QColor(armed.LINE2))
            qp.drawRect(r.adjusted(0, 0, -1, -1))
        qp.setFont(armed.mono(12))
        qp.setPen(QColor(armed.ACC if selected else armed.DIM))
        qp.drawText(r.adjusted(14, 0, 0, 0), Qt.AlignVCenter, "{:02d}".format(index.row()))
        qp.setFont(armed.head(15))
        qp.setPen(QColor(armed.ACC if selected else armed.TXT))
        text = qp.fontMetrics().elidedText(index.data(), Qt.ElideRight, r.width() - 84)
        qp.drawText(r.adjusted(44, 0, -40, 0), Qt.AlignVCenter, text)
        if index.data(Qt.UserRole + 1):
            badge = QRect(r.right() - 34, r.center().y() - 8, 26, 16)
            qp.setFont(armed.mono(11))
            qp.setPen(QColor(armed.ACC_D if not selected else armed.ACC))
            qp.drawRect(badge)
            qp.setPen(QColor(armed.ACC))
            qp.drawText(badge, Qt.AlignCenter, "2C")
        qp.restore()

    def sizeHint(self, option, index):
        return QSize(260, 32)


class PreviewKeyboard(KeyboardWidget):
    """ Keyboard drawn as LEDs; a click counts as a key press for reactive effects """

    def __init__(self, layout_editor, on_press):
        super().__init__(layout_editor)
        self.on_press = on_press
        self.setCursor(Qt.PointingHandCursor)

    def minimumSizeHint(self):
        return QSize(240, 90)

    def sizeHint(self):
        return QSize(self.width, self.height)

    def fit(self, avail_w, avail_h):
        """ Scales the keyboard to the space it has """
        if not self.widgets:
            return
        self.scale = 1.0
        self.update_layout()
        s = min(avail_w / max(1, self.width), avail_h / max(1, self.height), 1.1)
        self.scale = max(0.3, s)
        self.update_layout()

    def mousePressEvent(self, ev):
        key, _ = self.hit_test(ev.pos())
        if key is not None:
            self.on_press(key)

    def event(self, ev):
        if ev.type() == ev.ToolTip:
            return True
        return super().event(ev)


def slider(maximum):
    s = QSlider(Qt.Horizontal)
    s.setRange(0, maximum)
    s.setMinimumWidth(140)
    return s


class ArmedLighting(BasicEditor):

    def __init__(self, layout_editor):
        super().__init__()
        self.layout_editor = layout_editor
        self.io = None
        self.leds = []
        self.led_keys = []
        self.fn_preview = False

        self.timer = QTimer()
        self.timer.timeout.connect(self.tick)
        self.t0 = time.monotonic()
        self.last_demo = 0

        self.preview = None
        self.strip_preview = StripPreview()

        top = QHBoxLayout()
        top.addStretch(1)
        self.lbl_hint = QLabel(tr("ArmedLighting", "2C = uses COLOR 2 · click the keyboard to press keys"))
        self.lbl_hint.setObjectName("armedHint")
        top.addWidget(self.lbl_hint)
        self.btn_enabled = QPushButton("ON")
        self.btn_enabled.setCheckable(True)
        self.btn_enabled.clicked.connect(self.on_enabled)
        top.addWidget(self.btn_enabled)

        self.effects = QListWidget()
        self.effects.setObjectName("armedList")
        self.effects.setItemDelegate(EffectDelegate(self.effects))
        self.effects.setMouseTracking(True)
        self.effects.setFixedWidth(300)
        self.effects.currentRowChanged.connect(self.on_effect)

        # COLOR
        self.color_frame = BracketFrame("COLOR", accent=True)
        self.color_chip = QLabel()
        self.color_chip.setFixedSize(22, 16)
        self.color_frame.header.insertWidget(1, self.color_chip)
        self.hue = slider(255)
        self.sat = slider(255)
        self.hue.setObjectName("armedHue")
        for name, s in (("HUE", self.hue), ("SAT", self.sat)):
            self.color_frame.box.addLayout(self.slider_row(name, s))
        self.hue.valueChanged.connect(lambda v: self.on_hsv(V_HUE, v))
        self.sat.valueChanged.connect(lambda v: self.on_hsv(V_SAT, v))

        # COLOR 2
        self.color2_frame = BracketFrame("COLOR 2")
        self.color2_name = QLabel()
        self.color2_name.setObjectName("armedHint")
        self.color2_frame.header.insertWidget(1, self.color2_name)
        self.color2 = Swatches()
        self.color2.changed.connect(lambda i: self.on_index(V_COLOR2, i))
        self.color2_frame.box.addWidget(self.color2)
        self.color2_frame.box.addStretch(1)

        # BRIGHT / SPEED
        self.level_frame = BracketFrame()
        self.val = slider(255)
        self.speed = slider(255)
        self.level_frame.box.addLayout(self.slider_row("BRIGHT", self.val, percent=True))
        self.level_frame.box.addLayout(self.slider_row("SPEED", self.speed, percent=True))
        self.val.valueChanged.connect(lambda v: self.on_hsv(V_VAL, v))
        self.speed.valueChanged.connect(lambda v: self.on_hsv(V_SPEED, v))
        for s in (self.hue, self.sat, self.val, self.speed):
            s.sliderReleased.connect(self.on_slider_released)

        # FN COLOR
        self.fn_frame = BracketFrame("FN COLOR")
        self.fn_name = QLabel()
        self.fn_name.setObjectName("armedHint")
        self.fn_frame.header.insertWidget(1, self.fn_name)
        self.btn_fn = QPushButton(tr("ArmedLighting", "HOLD FN"))
        self.btn_fn.setCheckable(True)
        self.btn_fn.toggled.connect(self.on_fn_preview)
        self.fn_frame.header.addWidget(self.btn_fn)
        self.fn_color = Swatches()
        self.fn_color.changed.connect(lambda i: self.on_index(V_FN_COLOR, i))
        self.fn_frame.box.addWidget(self.fn_color)
        self.fn_frame.box.addStretch(1)

        # LIGHT BAR
        self.strip_frame = BracketFrame("LIGHT BAR")
        self.strip_view = StripView()
        self.strip_frame.header.insertWidget(1, self.strip_view, 1)
        self.strip_mode = Segmented()
        self.strip_mode.changed.connect(lambda i: self.on_index(V_STRIP_MODE, i))
        self.strip_color = Swatches()
        self.strip_color.changed.connect(lambda i: self.on_index(V_STRIP_COLOR, i))
        self.strip_caps = QCheckBox(tr("ArmedLighting", "CAPS ALERT"))
        self.strip_caps.toggled.connect(lambda on: self.on_index(V_STRIP_CAPS, int(on)))
        row = QHBoxLayout()
        row.setSpacing(18)
        row.addWidget(self.strip_mode)
        row.addWidget(self.strip_color)
        row.addStretch(1)
        row.addWidget(self.strip_caps)
        self.strip_frame.box.addLayout(row)

        # keyboard preview
        self.kb_frame = BracketFrame()
        self.kb = PreviewKeyboard(layout_editor, self.on_preview_press)
        self.kb_frame.box.addWidget(self.kb, 0, Qt.AlignCenter)
        self.kb_frame.resized = self.fit_preview
        self.kb_frame.box.setContentsMargins(16, 16, 16, 16)

        grid = QGridLayout()
        grid.setHorizontalSpacing(18)
        grid.setVerticalSpacing(14)
        grid.addWidget(self.color_frame, 0, 0)
        grid.addWidget(self.color2_frame, 0, 1)
        grid.addWidget(self.level_frame, 1, 0)
        grid.addWidget(self.fn_frame, 1, 1)
        grid.addWidget(self.strip_frame, 2, 0, 1, 2)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)

        right = QVBoxLayout()
        right.setSpacing(14)
        right.addLayout(grid)
        right.addWidget(self.kb_frame, 1)

        body = QHBoxLayout()
        body.setSpacing(18)
        body.addWidget(self.effects)
        body.addLayout(right, 1)

        self.addLayout(top)
        self.addLayout(body, 1)

    def slider_row(self, name, s, percent=False):
        row = QHBoxLayout()
        lbl = QLabel(name)
        lbl.setObjectName("armedKey")
        lbl.setFixedWidth(58)
        num = QLabel()
        num.setFixedWidth(44)
        num.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        s.valueChanged.connect(lambda v: num.setText("{}%".format(v * 100 // max(1, s.maximum()))
                                                    if percent else str(v)))
        row.addWidget(lbl)
        row.addWidget(s, 1)
        row.addWidget(num)
        return row

    def valid(self):
        return isinstance(self.device, VialKeyboard) and self.device.keyboard.io84 is not None

    def fit_preview(self):
        r = self.kb_frame.contentsRect()
        m = self.kb_frame.box.contentsMargins()
        self.kb.fit(r.width() - m.left() - m.right(), r.height() - m.top() - m.bottom())

    def rebuild(self, device):
        super().rebuild(device)
        self.io = device.keyboard.io84 if self.valid() else None
        if self.io is None:
            self.timer.stop()
            return

        kb = device.keyboard
        self.leds = self.io.leds
        self.preview = EffectPreview(self.leds)
        self.kb.set_keys(kb.keys, kb.encoders)
        self.fit_preview()
        by_pos = {(w.desc.row, w.desc.col): w for w in self.kb.widgets}
        self.led_keys = [by_pos.get((led.row, led.col)) for led in self.leds]
        for w in self.kb.widgets:
            w.setText("")
            w.glow = (0, 0, 0)
        self.fn_keys = self.fn_lit_keys(kb)

        self.val.setMaximum(self.io.max_brightness)
        self.block(True)
        self.effects.clear()
        for e in self.io.effects:
            if effect_key(e.name) in HIDDEN_EFFECTS:
                continue
            item = QListWidgetItem(e.name)
            item.setData(Qt.UserRole, e.qmk_id)
            item.setData(Qt.UserRole + 1, effect_key(e.name) in TWO_COLOR)
            self.effects.addItem(item)
        for sw in (self.color2, self.fn_color, self.strip_color):
            sw.set_colors(self.io.colors)
        self.strip_mode.set_labels(self.io.strip_modes)
        self.block(False)
        self.load_values()

    def fn_lit_keys(self, kb):
        """ Keys lit by the firmware Fn overlay: bound on layer 1, plus the key holding it """
        lit = set()
        for (layer, row, col), code in kb.layout.items():
            if layer != 1:
                continue
            if code not in ("KC_TRNS", "KC_NO") or kb.layout.get((0, row, col)) == "MO(1)":
                lit.add((row, col))
        return lit

    def block(self, on):
        for w in (self.effects, self.hue, self.sat, self.val, self.speed, self.color2, self.fn_color,
                  self.strip_color, self.strip_mode, self.strip_caps, self.btn_enabled):
            w.blockSignals(on)

    def load_values(self):
        io = self.io
        self.block(True)
        ids = [e.qmk_id for e in io.effects]
        effect = io.get(V_EFFECT)
        self.effects.setCurrentRow(ids.index(effect) if effect in ids else -1)
        if self.effects.currentItem():
            self.effects.scrollToItem(self.effects.currentItem(), self.effects.PositionAtCenter)
        self.hue.setValue(io.get(V_HUE))
        self.sat.setValue(io.get(V_SAT))
        self.val.setValue(io.get(V_VAL))
        self.speed.setValue(io.get(V_SPEED))
        self.color2.set_current(io.get(V_COLOR2))
        self.fn_color.set_current(io.get(V_FN_COLOR))
        self.strip_color.set_current(io.get(V_STRIP_COLOR))
        self.strip_mode.set_current(io.get(V_STRIP_MODE))
        self.strip_caps.setChecked(bool(io.get(V_STRIP_CAPS)))
        self.btn_enabled.setChecked(bool(io.get(V_ENABLED)))
        self.block(False)
        for s in (self.hue, self.sat, self.val, self.speed):
            s.valueChanged.emit(s.value())
        self.sync_preview()

    def color_name(self, idx):
        return self.io.colors[idx].name if 0 <= idx < len(self.io.colors) else ""

    def sync_preview(self):
        io, p = self.io, self.preview
        item = self.effects.currentItem()
        p.set_effect(item.text() if item else "SOLID_COLOR")
        p.h, p.s = io.get(V_HUE), io.get(V_SAT)
        p.v = min(255, io.get(V_VAL) * 255 // max(1, io.max_brightness))
        p.speed = io.get(V_SPEED)
        p.color1_black = p.h == 0 and p.s == 1
        c2 = io.colors[io.get(V_COLOR2)] if io.get(V_COLOR2) < len(io.colors) else None
        if c2:
            p.color2, p.color2_black = (c2.h, c2.s), c2.black
        sc = io.colors[io.get(V_STRIP_COLOR)] if io.get(V_STRIP_COLOR) < len(io.colors) else None
        if sc:
            self.strip_preview.color, self.strip_preview.color_black = (sc.h, sc.s), sc.black
        modes = io.strip_modes
        self.strip_preview.mode = modes[io.get(V_STRIP_MODE)] if io.get(V_STRIP_MODE) < len(modes) else "OFF"

        chip = QColor(0, 0, 0) if p.color1_black else QColor.fromHsv(int(p.h * 359 / 255), p.s, 255)
        self.color_chip.setStyleSheet("background: {}; border: 1px solid {};".format(chip.name(), armed.LINE2))
        self.color2_name.setText(self.color_name(io.get(V_COLOR2)))
        self.fn_name.setText(self.color_name(io.get(V_FN_COLOR)))
        self.btn_enabled.setText("ON" if io.get(V_ENABLED) else "OFF")

        name = item.text().upper() if item else ""
        uses2 = effect_key(name) in TWO_COLOR
        self.color2_frame.set_accent(uses2)
        self.color2_name.setStyleSheet("color: {};".format(armed.TXT if uses2 else armed.DIM))

    # events

    def on_effect(self, row):
        item = self.effects.item(row)
        if item is None or self.io is None:
            return
        self.io.set(V_EFFECT, item.data(Qt.UserRole))
        if not self.io.get(V_ENABLED):
            self.io.set(V_ENABLED, 1)
            self.btn_enabled.setChecked(True)
        self.sync_preview()

    def on_enabled(self, checked):
        self.io.set(V_ENABLED, int(checked))
        self.sync_preview()

    def on_hsv(self, vid, v):
        if self.io is None:
            return
        self.io.set(vid, v)
        self.sync_preview()

    def on_slider_released(self):
        self.io.save()

    def on_index(self, vid, idx):
        if self.io is None:
            return
        self.io.set(vid, idx)
        self.sync_preview()

    def on_fn_preview(self, on):
        self.fn_preview = on

    def on_preview_press(self, key):
        for i, w in enumerate(self.led_keys):
            if w is key:
                self.preview.hit(i, self.now())
                return

    # animation

    def now(self):
        return int((time.monotonic() - self.t0) * 1000)

    def activate(self):
        if self.io is not None:
            self.io.reload_values()
            self.load_values()
            self.timer.start(1000 // FPS)

    def deactivate(self):
        self.timer.stop()

    def tick(self):
        if self.io is None or self.preview is None:
            return
        t = self.now()
        p = self.preview
        if p.reactive and t - self.last_demo > DEMO_PRESS_MS:
            self.last_demo = t
            p.hit(random.randrange(len(self.leds)), t)

        enabled = self.io.get(V_ENABLED)
        colors = p.frame(t) if enabled else [(0, 0, 0)] * len(self.leds)
        if self.fn_preview:
            fc = self.io.colors[self.io.get(V_FN_COLOR)] if self.io.get(V_FN_COLOR) < len(self.io.colors) else None
            on = (0, 0, 0)
            if fc is not None and not fc.black:
                c = QColor.fromHsv(int(fc.h * 359 / 255), fc.s, max(p.v, 160))
                on = (c.red(), c.green(), c.blue())
            colors = [on if (led.row, led.col) in self.fn_keys else (0, 0, 0) for led in self.leds]
        for w, c in zip(self.led_keys, colors):
            if w is not None:
                w.glow = c
        self.kb.update()
        self.strip_preview.caps = False
        self.strip_view.set_colors(self.strip_preview.frame(t))
