# SPDX-License-Identifier: GPL-2.0-or-later
# Themes page for qmk-armed keyboards: lighting themes built from layers, edited live on the keyboard
import random
import time

from PyQt5.QtCore import Qt, QTimer, QSize, QPointF, QRectF
from PyQt5.QtGui import QColor, QPainter, QPen, QPolygonF, QKeySequence
from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QGridLayout, QLabel, QPushButton, QLineEdit, QComboBox, \
    QCheckBox, QMenu, QFileDialog, QMessageBox, QScrollArea, QWidget, QShortcut, QSlider, QListWidgetItem

import armed
from editor.basic_editor import BasicEditor
from lighting_theme import Theme, Layer, TYPES, TYPE_BY_ID, BLENDS, GROUPS, PRESETS, PAINT, GRADIENT, \
    RAINBOW, PULSE, WAVE, SPARKLE, REACTIVE, RIPPLE, F_RADIAL, F_MIRROR, F_REVERSE, F_HIDDEN, C_ALWAYS, C_CAPS, \
    C_NO_CAPS, C_LAYER, C_NOT_LAYER, NAME_LEN, key_groups, build_preset
from protocol.qmk_armed import F_THEMES, V_EFFECT, V_ENABLED, V_VAL, ArmedError
from theme_render import ThemeRenderer
from vial_device import VialKeyboard
from widgets.armed_controls import BracketFrame, Segmented
from widgets.keyboard_widget import KeyboardWidget
from widgets.theme_controls import ColorButton, DirectionDial, LayerList, layer_summary
from util import tr

FPS = 30
DEMO_PRESS_MS = 420
UNDO_GROUP_MS = 600
PUSH_MS = 30
FILE_FILTER = "Vial Armed theme (*.json);;All files (*)"

SIZE_LABEL = {GRADIENT: "LENGTH", RAINBOW: "LENGTH", PULSE: "SPREAD", WAVE: "WIDTH", SPARKLE: "FADE",
              REACTIVE: "FADE", RIPPLE: "WIDTH"}
SPEED_LABEL = {SPARKLE: "DENSITY", RIPPLE: "SPEED"}
PARAM_LABEL = {RAINBOW: "SATURATION", REACTIVE: "SPREAD"}
MIRROR_LABEL = {GRADIENT: "MIRROR  A → B → A", WAVE: "BOUNCE"}


def seconds(ms):
    return "{:.1f} S".format(ms / 1000) if ms < 10000 else "{} S".format(int(ms / 1000))


def pct(v, full=255):
    return "{}%".format(int(round(v * 100 / full)))


class ThemeKeyboard(KeyboardWidget):

    def __init__(self, layout_editor, page):
        super().__init__(layout_editor)
        self.page = page
        self.covered = set()
        self.center = None
        self.dragged = set()
        self.setCursor(Qt.PointingHandCursor)

    def minimumSizeHint(self):
        return QSize(240, 90)

    def sizeHint(self):
        return QSize(self.width, self.height)

    def fit(self, avail_w, avail_h):
        if not self.widgets:
            return
        self.scale = 1.0
        self.update_layout()
        self.scale = max(0.3, min(avail_w / max(1, self.width), avail_h / max(1, self.height), 1.25))
        self.update_layout()

    def mousePressEvent(self, ev):
        key, _ = self.hit_test(ev.pos())
        self.dragged = {key}
        if key is not None:
            self.page.key_down(key, ev.button(), ev.modifiers())

    def mouseMoveEvent(self, ev):
        if not ev.buttons() & (Qt.LeftButton | Qt.RightButton):
            return
        key, _ = self.hit_test(ev.pos())
        if key is not None and key not in self.dragged:
            self.dragged.add(key)
            self.page.key_drag(key)

    def mouseReleaseEvent(self, ev):
        self.dragged = set()
        self.page.key_up()

    def event(self, ev):
        if ev.type() == ev.ToolTip:
            return True
        return super().event(ev)

    def paintEvent(self, ev):
        super().paintEvent(ev)
        qp = QPainter(self)
        qp.setRenderHint(QPainter.Antialiasing)
        qp.setPen(Qt.NoPen)
        qp.setBrush(QColor(armed.ACC))
        for key in self.widgets:
            if key not in self.covered:
                continue
            qp.save()
            qp.scale(self.scale, self.scale)
            qp.translate(key.shift_x, key.shift_y)
            qp.translate(key.rotation_x, key.rotation_y)
            qp.rotate(key.rotation_angle)
            qp.translate(-key.rotation_x, -key.rotation_y)
            r = QRectF(key.rect)
            n = min(r.width(), r.height()) * 0.22
            qp.drawPolygon(QPolygonF([r.topLeft() + QPointF(2, 2), r.topLeft() + QPointF(2 + n, 2),
                                      r.topLeft() + QPointF(2, 2 + n)]))
            qp.restore()
        if self.center is not None:
            x, y = self.center
            pen = QPen(QColor(armed.ACC), 2)
            qp.setPen(pen)
            qp.setBrush(Qt.NoBrush)
            qp.drawEllipse(QPointF(x, y), 9, 9)
            qp.drawLine(QPointF(x - 15, y), QPointF(x + 15, y))
            qp.drawLine(QPointF(x, y - 15), QPointF(x, y + 15))
        qp.end()


def slider(maximum=255):
    s = QSlider(Qt.Horizontal)
    s.setRange(0, maximum)
    s.setMinimumWidth(120)
    return s


class ArmedThemes(BasicEditor):

    def __init__(self, layout_editor):
        super().__init__()
        self.layout_editor = layout_editor
        self.io = None
        self.info = None
        self.leds = []
        self.led_keys = []
        self.key_led = {}
        self.groups = {}
        self.themes = []
        self.saved = []
        self.sent = []
        self.undo = []
        self.redo = []
        self.last_state = []
        self.slot = 0
        self.cur = -1
        self.loaded = False
        self.picking_center = False
        self.drag_action = None
        self.renderer = None
        self.updating = False
        self.t0 = time.monotonic()
        self.last_demo = 0
        self.caps_preview = False
        self.fn_preview = False

        self.timer = QTimer()
        self.timer.timeout.connect(self.tick)
        self.push_timer = QTimer()
        self.push_timer.setSingleShot(True)
        self.push_timer.timeout.connect(self.push)
        self.group_timer = QTimer()
        self.group_timer.setSingleShot(True)
        self.group_timer.timeout.connect(self.close_group)

        # top bar
        top = QHBoxLayout()
        top.setSpacing(10)
        self.slots = Segmented()
        self.slots.changed.connect(self.on_slot)
        top.addWidget(self.slots)
        self.name = QLineEdit()
        self.name.setMaxLength(NAME_LEN - 1)
        self.name.setFixedWidth(200)
        self.name.setPlaceholderText("NAME")
        self.name.textEdited.connect(self.on_name)
        top.addWidget(self.name)
        self.lbl_state = QLabel()
        self.lbl_state.setObjectName("armedHint")
        top.addWidget(self.lbl_state)
        top.addStretch(1)
        self.btn_undo = QPushButton(tr("ArmedThemes", "UNDO"))
        self.btn_undo.clicked.connect(self.on_undo)
        self.btn_redo = QPushButton(tr("ArmedThemes", "REDO"))
        self.btn_redo.clicked.connect(self.on_redo)
        self.btn_presets = QPushButton(tr("ArmedThemes", "START FROM ▾"))
        self.presets_menu = QMenu(self.btn_presets)
        for i, (name, _, _) in enumerate(PRESETS):
            self.presets_menu.addAction(name, lambda i=i: self.on_preset(i))
        self.btn_presets.setMenu(self.presets_menu)
        self.btn_import = QPushButton(tr("ArmedThemes", "IMPORT"))
        self.btn_import.clicked.connect(self.on_import)
        self.btn_export = QPushButton(tr("ArmedThemes", "EXPORT"))
        self.btn_export.clicked.connect(self.on_export)
        self.btn_revert = QPushButton(tr("ArmedThemes", "REVERT"))
        self.btn_revert.clicked.connect(self.on_revert)
        self.btn_save = QPushButton(tr("ArmedThemes", "SAVE"))
        self.btn_save.setDefault(True)
        self.btn_save.clicked.connect(self.on_save)
        for b in (self.btn_undo, self.btn_redo, self.btn_presets, self.btn_import, self.btn_export, self.btn_revert,
                  self.btn_save):
            top.addWidget(b)

        # keyboard
        self.kb_frame = BracketFrame()
        kb_head = QHBoxLayout()
        self.lbl_keys = QLabel()
        self.lbl_keys.setObjectName("armedHint")
        kb_head.addWidget(self.lbl_keys, 1)
        self.chk_solo = QCheckBox(tr("ArmedThemes", "SOLO LAYER"))
        self.chk_solo.setToolTip(tr("ArmedThemes", "Preview only the selected layer"))
        self.chk_caps = QCheckBox(tr("ArmedThemes", "CAPS"))
        self.chk_caps.setToolTip(tr("ArmedThemes", "Preview with Caps Lock on"))
        self.chk_caps.toggled.connect(lambda on: setattr(self, "caps_preview", on))
        self.chk_fn = QCheckBox(tr("ArmedThemes", "FN"))
        self.chk_fn.setToolTip(tr("ArmedThemes", "Preview with Fn (layer 1) held"))
        self.chk_fn.toggled.connect(lambda on: setattr(self, "fn_preview", on))
        for w in (self.chk_solo, self.chk_caps, self.chk_fn):
            kb_head.addWidget(w)
        self.kb_frame.box.addLayout(kb_head)
        self.kb = ThemeKeyboard(layout_editor, self)
        self.kb_frame.box.addWidget(self.kb, 1, Qt.AlignCenter)
        self.kb_frame.resized = self.fit_preview

        tools = QHBoxLayout()
        tools.setSpacing(6)
        self.lbl_tools = QLabel(tr("ArmedThemes", "KEYS"))
        self.lbl_tools.setObjectName("armedKey")
        tools.addWidget(self.lbl_tools)
        self.group_buttons = []
        for g in GROUPS + ["NONE", "INVERT"]:
            b = QPushButton(g)
            b.clicked.connect(lambda _, g=g: self.on_group(g))
            if g == "FN LAYER":
                b.setToolTip(tr("ArmedThemes", "Keys bound on the Fn layer and the Fn key itself; "
                                               "pair it with SHOW: FN HELD"))
            tools.addWidget(b)
            self.group_buttons.append(b)
        tools.addStretch(1)
        self.kb_frame.box.addLayout(tools)

        # layers
        layers_frame = BracketFrame("LAYERS", accent=True)
        self.lbl_count = QLabel()
        self.lbl_count.setObjectName("armedHint")
        layers_frame.header.insertWidget(1, self.lbl_count)
        self.layer_list = LayerList()
        self.layer_list.setMinimumHeight(150)
        self.layer_list.currentRowChanged.connect(self.on_layer_row)
        self.layer_list.toggled.connect(self.on_layer_toggle)
        layers_frame.box.addWidget(self.layer_list, 1)
        row = QHBoxLayout()
        row.setSpacing(6)
        self.btn_add = QPushButton(tr("ArmedThemes", "+ ADD ▾"))
        self.add_menu = QMenu(self.btn_add)
        for t in TYPES:
            act = self.add_menu.addAction("{:<10} {}".format(t.name, t.hint), lambda t=t: self.on_add(t.id))
            act.setToolTip(t.hint)
        self.btn_add.setMenu(self.add_menu)
        self.btn_dup = QPushButton(tr("ArmedThemes", "COPY"))
        self.btn_dup.clicked.connect(self.on_duplicate)
        self.btn_up = QPushButton("▲")
        self.btn_up.clicked.connect(lambda: self.on_move(1))
        self.btn_down = QPushButton("▼")
        self.btn_down.clicked.connect(lambda: self.on_move(-1))
        self.btn_del = QPushButton(tr("ArmedThemes", "DELETE"))
        self.btn_del.clicked.connect(self.on_delete)
        for b in (self.btn_add, self.btn_dup, self.btn_up, self.btn_down, self.btn_del):
            row.addWidget(b)
        layers_frame.box.addLayout(row)

        # layer properties
        self.props = BracketFrame("LAYER")
        self.lbl_hint = QLabel()
        self.lbl_hint.setObjectName("armedHint")
        self.lbl_hint.setWordWrap(True)
        self.props.box.addWidget(self.lbl_hint)
        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(8)
        r = 0

        self.type_combo = QComboBox()
        for t in TYPES:
            self.type_combo.addItem(t.name, t.id)
        self.type_combo.currentIndexChanged.connect(self.on_type)
        self.row_type = self.add_row(grid, r, "TYPE", self.type_combo)
        r += 1

        colors = QHBoxLayout()
        self.color_a = ColorButton("A")
        self.color_a.changed.connect(lambda c: self.edit(a=c))
        self.color_b = ColorButton("B")
        self.color_b.changed.connect(lambda c: self.edit(b=c))
        self.btn_swap = QPushButton("⇄")
        self.btn_swap.setToolTip(tr("ArmedThemes", "Swap the colours"))
        self.btn_swap.clicked.connect(self.on_swap)
        colors.addWidget(self.color_a)
        colors.addWidget(self.color_b)
        colors.addWidget(self.btn_swap)
        colors.addStretch(1)
        self.lbl_colors = QLabel("COLOURS")
        self.row_colors = self.add_row(grid, r, self.lbl_colors, colors)
        r += 1

        dir_box = QHBoxLayout()
        self.dial = DirectionDial()
        self.dial.changed.connect(self.on_dial)
        dir_box.addWidget(self.dial)
        dir_opts = QVBoxLayout()
        dir_opts.setSpacing(6)
        self.chk_radial = QCheckBox(tr("ArmedThemes", "FROM A POINT"))
        self.chk_radial.setToolTip(tr("ArmedThemes", "Spread out from a point instead of across the keyboard"))
        self.chk_radial.toggled.connect(lambda on: self.edit_flag(F_RADIAL, on))
        self.radial_way = Segmented(["OUTWARD", "INWARD"])
        self.radial_way.changed.connect(lambda i: self.edit_flag(F_REVERSE, bool(i)))
        self.btn_center = QPushButton(tr("ArmedThemes", "PICK THE POINT"))
        self.btn_center.setCheckable(True)
        self.btn_center.toggled.connect(self.on_pick_center)
        self.chk_mirror = QCheckBox()
        self.chk_mirror.toggled.connect(lambda on: self.edit_flag(F_MIRROR, on))
        for w in (self.chk_radial, self.radial_way, self.btn_center, self.chk_mirror):
            dir_opts.addWidget(w)
        dir_opts.addStretch(1)
        dir_box.addLayout(dir_opts)
        dir_box.addStretch(1)
        self.row_dir = self.add_row(grid, r, "DIRECTION", dir_box)
        r += 1

        self.speed = slider()
        self.speed.valueChanged.connect(lambda v: self.edit(speed=v))
        self.lbl_speed, self.val_speed = QLabel(), QLabel()
        self.row_speed = self.add_row(grid, r, self.lbl_speed, self.speed, self.val_speed)
        r += 1
        self.size = slider()
        self.size.valueChanged.connect(lambda v: self.edit(size=v))
        self.lbl_size, self.val_size = QLabel(), QLabel()
        self.row_size = self.add_row(grid, r, self.lbl_size, self.size, self.val_size)
        r += 1
        self.param = slider()
        self.param.valueChanged.connect(lambda v: self.edit(param=v))
        self.lbl_param, self.val_param = QLabel(), QLabel()
        self.row_param = self.add_row(grid, r, self.lbl_param, self.param, self.val_param)
        r += 1
        self.opacity = slider()
        self.opacity.valueChanged.connect(lambda v: self.edit(opacity=v))
        self.val_opacity = QLabel()
        self.row_opacity = self.add_row(grid, r, "OPACITY", self.opacity, self.val_opacity)
        r += 1
        self.blend = Segmented(BLENDS)
        self.blend.changed.connect(lambda i: self.edit(blend=i))
        self.blend.setToolTip(tr("ArmedThemes", "NORMAL covers the layers below, ADD brightens them, "
                                                "MULTIPLY tints them"))
        self.row_blend = self.add_row(grid, r, "MIX", self.blend)
        r += 1
        self.cond = QComboBox()
        self.cond.currentIndexChanged.connect(self.on_cond)
        self.row_cond = self.add_row(grid, r, "SHOW", self.cond)
        grid.setColumnStretch(1, 1)
        self.props.box.addLayout(grid)
        self.props.box.addStretch(1)

        self.lbl_empty = QLabel(tr("ArmedThemes", "No layers yet. Press + ADD, or START FROM a preset."))
        self.lbl_empty.setObjectName("armedHint")
        self.lbl_empty.setAlignment(Qt.AlignCenter)
        self.props.box.addWidget(self.lbl_empty)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        holder = QWidget()
        hl = QVBoxLayout(holder)
        hl.setContentsMargins(0, 0, 0, 0)
        hl.addWidget(self.props)
        scroll.setWidget(holder)

        right = QVBoxLayout()
        right.setSpacing(14)
        right.addWidget(layers_frame, 2)
        right.addWidget(scroll, 3)
        right_w = QWidget()
        right_w.setLayout(right)
        right_w.setFixedWidth(470)

        body = QHBoxLayout()
        body.setSpacing(18)
        body.addWidget(self.kb_frame, 1)
        body.addWidget(right_w)

        self.addLayout(top)
        self.addLayout(body, 1)
        self.shortcuts = []

    def add_row(self, grid, r, label, widget, value=None):
        lbl = label if isinstance(label, QLabel) else QLabel(label)
        lbl.setObjectName("armedKey")
        lbl.setFixedWidth(96)
        grid.addWidget(lbl, r, 0, Qt.AlignTop if not isinstance(widget, QSlider) else Qt.AlignVCenter)
        span = 1 if value is not None else 2
        if isinstance(widget, (QHBoxLayout, QVBoxLayout)):
            grid.addLayout(widget, r, 1, 1, span)
        else:
            grid.addWidget(widget, r, 1, 1, span)
        items = [lbl, widget]
        if value is not None:
            value.setFixedWidth(84)
            value.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            grid.addWidget(value, r, 2)
            items.append(value)
        return items

    @staticmethod
    def show_row(items, on):
        for w in items:
            if isinstance(w, (QHBoxLayout, QVBoxLayout)):
                for i in range(w.count()):
                    it = w.itemAt(i)
                    if it.widget():
                        it.widget().setVisible(on)
                    elif it.layout():
                        ArmedThemes.show_row([it.layout()], on)
            else:
                w.setVisible(on)


    def valid(self):
        if not isinstance(self.device, VialKeyboard):
            return False
        io = self.device.keyboard.armed
        return io is not None and io.has(F_THEMES)

    def rebuild(self, device):
        super().rebuild(device)
        self.io = device.keyboard.armed if self.valid() else None
        self.loaded = False
        if self.io is None:
            self.timer.stop()
            return
        kb = device.keyboard
        self.leds = self.io.leds
        self.renderer = ThemeRenderer(self.leds)
        self.kb.set_keys(kb.keys, kb.encoders)
        self.fit_preview()
        by_pos = {(w.desc.row, w.desc.col): w for w in self.kb.widgets}
        self.led_keys = [by_pos.get((led.row, led.col)) for led in self.leds]
        self.key_led = {w: i for i, w in enumerate(self.led_keys) if w is not None}
        for w in self.kb.widgets:
            w.setText("")
            w.glow = (0, 0, 0)
        self.groups = key_groups(self.leds, kb.layout)
        self.fill_conditions(kb.layers)
        self.fit_centers()

    def fill_conditions(self, layers):
        self.updating = True
        self.cond.clear()
        self.cond.addItem("ALWAYS", C_ALWAYS)
        self.cond.addItem("CAPS LOCK ON", C_CAPS)
        self.cond.addItem("CAPS LOCK OFF", C_NO_CAPS)
        self.cond.addItem("FN HELD", C_LAYER | 1)
        self.cond.addItem("FN NOT HELD", C_NOT_LAYER | 1)
        self.cond.addItem("BASE LAYER ONLY", C_LAYER)
        for n in range(2, min(16, layers)):
            self.cond.addItem("LAYER {} ON".format(n), C_LAYER | n)
        self.updating = False

    def fit_centers(self):
        """ Linear map from LED coordinates (0..224, 0..64) to keyboard widget pixels """
        pts = [(led.x, led.y, w) for led, w in zip(self.leds, self.led_keys) if w is not None]
        self.led_map = None
        if len(pts) < 2:
            return

        def fit(vals, pix):
            n = len(vals)
            mv, mp = sum(vals) / n, sum(pix) / n
            var = sum((v - mv) ** 2 for v in vals) or 1
            k = sum((v - mv) * (p - mp) for v, p in zip(vals, pix)) / var
            return k, mp - k * mv

        self.led_map = (fit([p[0] for p in pts], [self.key_center(p[2]).x() for p in pts]),
                        fit([p[1] for p in pts], [self.key_center(p[2]).y() for p in pts]))

    def key_center(self, w):
        r = w.polygon.boundingRect()
        return r.center()

    def led_to_px(self, x, y):
        if not self.led_map:
            return None
        (kx, bx), (ky, by) = self.led_map
        return (kx * x + bx) * self.kb.scale, (ky * y + by) * self.kb.scale

    def fit_preview(self):
        r = self.kb_frame.contentsRect()
        self.kb.fit(r.width() - 40, r.height() - 110)

    def load(self):
        try:
            self.info = self.io.theme_info()
            if self.info is None or self.info.leds != len(self.leds):
                raise ArmedError("unexpected theme layout")
            blobs = [self.io.theme_read(s, self.info.size) for s in range(self.info.slots)]
        except Exception as e:
            self.lbl_state.setText("CAN'T READ THEMES: {}".format(e))
            return False
        n = self.info.slots
        self.themes = [Theme.unpack(b, self.info.leds, self.info.layers) for b in blobs]
        self.io.themes, self.io.theme_modes = self.themes, self.info.modes
        self.saved = list(blobs)
        self.sent = list(blobs)
        self.undo = [[] for _ in range(n)]
        self.redo = [[] for _ in range(n)]
        self.last_state = [t.copy() for t in self.themes]
        self.loaded = True
        effect = self.io.get(V_EFFECT)
        self.slot = self.info.modes.index(effect) if effect in self.info.modes else min(self.slot, n - 1)
        self.cur = len(self.theme.layers) - 1
        self.refresh_all()
        return True

    @property
    def theme(self):
        return self.themes[self.slot]

    @property
    def layer(self):
        layers = self.theme.layers
        return layers[self.cur] if 0 <= self.cur < len(layers) else None

    def pack(self, slot=None):
        slot = self.slot if slot is None else slot
        return self.themes[slot].pack(self.info.leds, self.info.layers)


    def apply_to_keyboard(self):
        mode = self.info.modes[self.slot]
        if self.io.get(V_EFFECT) != mode:
            self.io.set(V_EFFECT, mode)
        if not self.io.get(V_ENABLED):
            self.io.set(V_ENABLED, 1)

    def schedule_push(self):
        self.push_timer.start(PUSH_MS)

    def push(self):
        if not self.loaded:
            return
        new, old = self.pack(), self.sent[self.slot]
        ranges = []
        i = 0
        while i < len(new):
            if new[i] == old[i]:
                i += 1
                continue
            j = i
            while j < len(new) and (new[j] != old[j] or any(new[k] != old[k] for k in range(j, min(len(new), j + 6)))):
                j += 1
            ranges.append((i, j))
            i = j
        if not ranges:
            return
        try:
            for a, b in ranges:
                self.io.theme_write(self.slot, a, new[a:b])
            self.sent[self.slot] = new
            self.apply_to_keyboard()
        except Exception as e:
            self.lbl_state.setText("KEYBOARD ERROR: {}".format(e))
            return
        self.refresh_state()


    def changed(self, layers=False, controls=False):
        """ Every edit ends here: undo grouping, preview, keyboard """
        if not self.group_timer.isActive():
            self.undo[self.slot].append(self.last_state[self.slot])
            del self.undo[self.slot][:-100]
            self.redo[self.slot].clear()
        self.group_timer.start(UNDO_GROUP_MS)
        if layers:
            self.refresh_layers()
        else:
            self.refresh_layer_row()
        if controls:
            self.refresh_controls()
        self.refresh_keys()
        self.schedule_push()

    def close_group(self):
        if self.loaded:
            self.last_state[self.slot] = self.theme.copy()

    def edit(self, **kw):
        layer = self.layer
        if self.updating or layer is None:
            return
        for k, v in kw.items():
            setattr(layer, k, v)
        self.refresh_values()
        self.changed()

    def edit_flag(self, flag, on):
        layer = self.layer
        if self.updating or layer is None:
            return
        layer.set_flag(flag, on)
        if flag == F_RADIAL and not on:
            layer.set_flag(F_REVERSE, False)
        self.changed(controls=True)

    def on_dial(self, v):
        layer = self.layer
        if layer is None:
            return
        layer.dir = v
        layer.set_flag(F_RADIAL, False)
        layer.set_flag(F_REVERSE, False)
        self.changed(controls=True)

    def on_type(self, idx):
        layer = self.layer
        if self.updating or layer is None:
            return
        tid = self.type_combo.itemData(idx)
        if tid == layer.type:
            return
        fresh = Layer(tid, layer.mask if tid != PAINT else set())
        fresh.blend, fresh.opacity, fresh.cond, fresh.dir = layer.blend, layer.opacity, layer.cond, layer.dir
        fresh.flags |= layer.flags & F_HIDDEN
        self.theme.layers[self.cur] = fresh
        self.changed(layers=True, controls=True)

    def on_cond(self, idx):
        if not self.updating and idx >= 0:
            self.edit(cond=self.cond.itemData(idx))

    def on_swap(self):
        layer = self.layer
        if layer is not None:
            layer.a, layer.b = layer.b, layer.a
            self.changed(controls=True)

    def on_name(self, text):
        if not self.loaded:
            return
        self.theme.name = "".join(c if 32 <= ord(c) < 127 else "?" for c in text.upper())
        self.slots.set_labels(self.slot_labels())
        self.slots.set_current(self.slot)
        self.changed()

    def on_add(self, tid):
        layers = self.theme.layers
        if len(layers) >= self.info.layers:
            return
        mask = set() if tid == PAINT else set(range(len(self.leds)))
        layers.insert(self.cur + 1, Layer(tid, mask))
        self.cur += 1
        self.changed(layers=True, controls=True)

    def on_duplicate(self):
        layer = self.layer
        if layer is None or len(self.theme.layers) >= self.info.layers:
            return
        copy = Layer.unpack(layer.pack(len(self.leds)), len(self.leds))
        self.theme.layers.insert(self.cur + 1, copy)
        self.cur += 1
        self.changed(layers=True, controls=True)

    def on_move(self, d):
        layers = self.theme.layers
        j = self.cur + d
        if self.layer is None or not 0 <= j < len(layers):
            return
        layers[self.cur], layers[j] = layers[j], layers[self.cur]
        self.cur = j
        self.changed(layers=True)

    def on_delete(self):
        if self.layer is None:
            return
        del self.theme.layers[self.cur]
        self.cur = min(self.cur, len(self.theme.layers) - 1)
        self.changed(layers=True, controls=True)

    def on_layer_toggle(self, row):
        idx = len(self.theme.layers) - 1 - row
        layer = self.theme.layers[idx]
        layer.set_flag(F_HIDDEN, not layer.hidden)
        self.changed(layers=True)

    def on_layer_row(self, row):
        if self.updating or row < 0:
            return
        self.cur = len(self.theme.layers) - 1 - row
        self.btn_center.setChecked(False)
        self.refresh_controls()
        self.refresh_keys()

    def on_preset(self, idx):
        name = self.theme.name
        theme = build_preset(idx, self.leds, self.device.keyboard.layout)
        if PRESETS[idx][0] == "BLANK":
            theme.name = name
        self.themes[self.slot] = theme
        self.cur = len(theme.layers) - 1
        self.changed(layers=True, controls=True)
        self.refresh_all()

    def on_pick_center(self, on):
        self.picking_center = on
        self.refresh_keys()


    def key_down(self, key, button, mods):
        layer = self.layer
        i = self.key_led.get(key)
        if layer is None or i is None:
            return
        if self.picking_center:
            layer.cx, layer.cy = self.leds[i].x, self.leds[i].y
            layer.set_flag(F_RADIAL, True)
            self.btn_center.setChecked(False)
            self.changed(controls=True)
            return
        if layer.type == PAINT and button == Qt.LeftButton and mods & (Qt.ControlModifier | Qt.AltModifier):
            if i in layer.mask:
                layer.a = self.theme.paint[i]
                self.refresh_controls()
            return
        if button == Qt.RightButton:
            self.drag_action = "remove"
        elif layer.type == PAINT:
            self.drag_action = "paint"
        else:
            self.drag_action = "remove" if i in layer.mask else "add"
        self.key_drag(key)

    def key_drag(self, key):
        layer = self.layer
        i = self.key_led.get(key)
        if layer is None or i is None or self.drag_action is None:
            return
        if self.drag_action == "remove":
            layer.mask.discard(i)
        else:
            layer.mask.add(i)
            if self.drag_action == "paint":
                self.theme.paint[i] = tuple(layer.a)
        self.changed()

    def key_up(self):
        self.drag_action = None

    def on_group(self, g):
        layer = self.layer
        if layer is None:
            return
        everything = set(range(len(self.leds)))
        if g == "NONE":
            keys, add = everything, False
        elif g == "INVERT":
            fresh = everything - layer.mask
            layer.mask = fresh
            if layer.type == PAINT:
                for i in fresh:
                    self.theme.paint[i] = tuple(layer.a)
            self.changed()
            return
        else:
            keys = self.groups.get(g, set())
            add = layer.type == PAINT or not keys <= layer.mask
        if add:
            layer.mask |= keys
            if layer.type == PAINT:
                for i in keys:
                    self.theme.paint[i] = tuple(layer.a)
        else:
            layer.mask -= keys
        self.changed()


    def flush_group(self):
        if self.group_timer.isActive():
            self.group_timer.stop()
            self.close_group()

    def on_undo(self):
        self.flush_group()
        if not self.undo[self.slot]:
            return
        self.redo[self.slot].append(self.theme.copy())
        self.themes[self.slot] = self.undo[self.slot].pop()
        self.after_history()

    def on_redo(self):
        self.flush_group()
        if not self.redo[self.slot]:
            return
        self.undo[self.slot].append(self.theme.copy())
        self.themes[self.slot] = self.redo[self.slot].pop()
        self.after_history()

    def after_history(self):
        self.last_state[self.slot] = self.theme.copy()
        self.cur = min(max(self.cur, 0), len(self.theme.layers) - 1)
        self.refresh_all()
        self.schedule_push()

    def on_slot(self, slot):
        self.flush_group()
        self.push_timer.stop()
        self.push()
        self.slot = slot
        self.cur = len(self.theme.layers) - 1
        self.btn_center.setChecked(False)
        try:
            self.apply_to_keyboard()
        except Exception as e:
            self.lbl_state.setText("KEYBOARD ERROR: {}".format(e))
        self.refresh_all()

    def on_save(self):
        self.flush_group()
        self.push_timer.stop()
        self.push()
        try:
            self.io.theme_save(self.slot)
        except Exception as e:
            QMessageBox.warning(self.parentWidget(), "", tr("ArmedThemes", "Could not save the theme: {}").format(e))
            return
        self.saved[self.slot] = self.sent[self.slot]
        self.refresh_state()

    def on_revert(self):
        self.flush_group()
        self.push_timer.stop()
        try:
            self.io.theme_revert(self.slot)
            blob = self.io.theme_read(self.slot, self.info.size)
        except Exception as e:
            self.lbl_state.setText("KEYBOARD ERROR: {}".format(e))
            return
        self.undo[self.slot].append(self.theme.copy())
        self.themes[self.slot] = Theme.unpack(blob, self.info.leds, self.info.layers)
        self.saved[self.slot] = self.sent[self.slot] = blob
        self.last_state[self.slot] = self.theme.copy()
        self.cur = len(self.theme.layers) - 1
        self.refresh_all()

    def on_export(self):
        path, _ = QFileDialog.getSaveFileName(self.parentWidget(), tr("ArmedThemes", "Export theme"),
                                              (self.theme.name or "theme").lower().replace(" ", "-") + ".json",
                                              FILE_FILTER)
        if path:
            with open(path, "w") as f:
                f.write(self.theme.to_json(self.leds))

    def on_import(self):
        path, _ = QFileDialog.getOpenFileName(self.parentWidget(), tr("ArmedThemes", "Import theme"), "", FILE_FILTER)
        if not path:
            return
        try:
            with open(path) as f:
                theme = Theme.from_json(f.read(), self.leds, self.info.layers)
        except Exception as e:
            QMessageBox.warning(self.parentWidget(), "", tr("ArmedThemes", "Not a theme file: {}").format(e))
            return
        self.themes[self.slot] = theme
        self.cur = len(theme.layers) - 1
        self.changed(layers=True, controls=True)
        self.refresh_all()


    def slot_labels(self):
        return ["{} · {}".format(i + 1, t.name or "THEME") for i, t in enumerate(self.themes)]

    def refresh_all(self):
        self.updating = True
        self.slots.set_labels(self.slot_labels())
        self.slots.set_current(self.slot)
        self.name.setText(self.theme.name)
        self.updating = False
        self.refresh_layers()
        self.refresh_controls()
        self.refresh_keys()
        self.refresh_state()

    def refresh_state(self):
        if not self.loaded:
            return
        dirty = self.pack() != self.saved[self.slot]
        self.lbl_state.setText('<span style="color:{}">■</span> {}'.format(
            armed.ACC if dirty else armed.OK,
            tr("ArmedThemes", "UNSAVED") if dirty else tr("ArmedThemes", "SAVED")))
        self.lbl_state.setToolTip(tr("ArmedThemes", "The keyboard already shows your changes; SAVE keeps them "
                                                    "after it is unplugged") if dirty else "")
        self.btn_save.setEnabled(dirty)
        self.btn_revert.setEnabled(dirty)
        self.btn_undo.setEnabled(bool(self.undo[self.slot]) or self.group_timer.isActive())
        self.btn_redo.setEnabled(bool(self.redo[self.slot]))

    def refresh_layers(self):
        self.updating = True
        layers = self.theme.layers
        self.layer_list.clear()
        for idx in reversed(range(len(layers))):
            item = QListWidgetItem(layer_summary(layers[idx]))
            item.setData(Qt.UserRole, layers[idx])
            item.setData(Qt.UserRole + 1, idx + 1)
            self.layer_list.addItem(item)
        if self.layer is not None:
            self.layer_list.setCurrentRow(len(layers) - 1 - self.cur)
        self.lbl_count.setText("{} / {} · TOP DRAWS OVER BOTTOM".format(len(layers), self.info.layers))
        full = len(layers) >= self.info.layers
        self.btn_add.setEnabled(not full)
        self.btn_dup.setEnabled(not full and self.layer is not None)
        for b in (self.btn_del, self.btn_up, self.btn_down):
            b.setEnabled(self.layer is not None)
        self.updating = False
        self.refresh_state()

    def refresh_layer_row(self):
        item = self.layer_list.item(len(self.theme.layers) - 1 - self.cur) if self.layer is not None else None
        if item is not None:
            item.setText(layer_summary(self.layer))
            self.layer_list.update()
        self.slots.set_labels(self.slot_labels())
        self.slots.set_current(self.slot)
        self.refresh_state()

    def refresh_controls(self):
        layer = self.layer
        self.lbl_empty.setVisible(layer is None)
        rows = (self.row_type, self.row_colors, self.row_dir, self.row_speed, self.row_size, self.row_param,
                self.row_opacity, self.row_blend, self.row_cond)
        if layer is None:
            self.lbl_hint.setText("")
            self.props.title.setText("LAYER")
            for row in rows:
                self.show_row(row, False)
            return
        t = TYPE_BY_ID.get(layer.type, TYPES[0])
        c = t.controls
        self.updating = True
        self.props.title.setText("LAYER {:02d} · {}".format(self.cur + 1, t.name))
        self.lbl_hint.setText(t.hint)
        self.type_combo.setCurrentIndex(self.type_combo.findData(layer.type))
        for row in (self.row_type, self.row_opacity, self.row_blend, self.row_cond):
            self.show_row(row, True)
        self.show_row(self.row_colors, bool(c & {"a", "b", "paint"}))
        self.lbl_colors.setText("BRUSH" if layer.type == PAINT else ("COLOURS" if "b" in c else "COLOUR"))
        self.color_a.setVisible(True)
        self.color_b.setVisible("b" in c)
        self.btn_swap.setVisible("b" in c)
        self.color_a.label = "" if layer.type == PAINT else ("A" if "b" in c else "")
        self.color_a.set_color(layer.a)
        self.color_b.set_color(layer.b)
        self.show_row(self.row_dir, "dir" in c)
        if "dir" in c:
            radial = layer.flag(F_RADIAL)
            self.dial.set_value(layer.dir, radial)
            self.chk_radial.setChecked(radial)
            self.radial_way.setVisible(radial)
            self.radial_way.set_current(1 if layer.flag(F_REVERSE) else 0)
            self.chk_mirror.setVisible("mirror" in c)
            self.chk_mirror.setText(MIRROR_LABEL.get(layer.type, "MIRROR"))
            self.chk_mirror.setChecked(layer.flag(F_MIRROR))
        self.show_row(self.row_speed, "speed" in c)
        self.lbl_speed.setText(SPEED_LABEL.get(layer.type, "SPEED"))
        self.speed.setValue(layer.speed)
        self.show_row(self.row_size, "size" in c)
        self.lbl_size.setText(SIZE_LABEL.get(layer.type, "SIZE"))
        self.size.setValue(layer.size)
        self.show_row(self.row_param, "param" in c)
        self.lbl_param.setText(PARAM_LABEL.get(layer.type, ""))
        self.param.setMaximum(90 if layer.type == REACTIVE else 255)
        self.param.setValue(0 if layer.type == REACTIVE and layer.param == 255 else layer.param)
        self.opacity.setValue(layer.opacity)
        self.blend.set_current(layer.blend)
        idx = self.cond.findData(layer.cond)
        if idx < 0:
            self.cond.addItem("CONDITION 0x{:02X}".format(layer.cond), layer.cond)
            idx = self.cond.count() - 1
        self.cond.setCurrentIndex(idx)
        self.updating = False
        self.refresh_values()

    def refresh_values(self):
        layer = self.layer
        if layer is None:
            return
        tp = layer.type
        if tp == SPARKLE:
            self.val_speed.setText(pct(layer.speed))
        elif tp == RIPPLE:
            self.val_speed.setText(pct(layer.speed))
        else:
            self.val_speed.setText("STILL" if not layer.speed else seconds(65536 / layer.speed))
        if tp == SPARKLE:
            self.val_size.setText(seconds(255 / (1 + (255 - layer.size) // 16) * 16))
        elif tp == REACTIVE:
            self.val_size.setText(seconds(100 + layer.size * 8))
        elif tp in (GRADIENT, RAINBOW):
            self.val_size.setText("FLAT" if not layer.size else pct(layer.size, 128))
        elif tp == PULSE:
            self.val_size.setText("TOGETHER" if not layer.size else pct(layer.size, 128))
        else:
            self.val_size.setText(pct(layer.size))
        if tp == REACTIVE:
            self.val_param.setText("KEY ONLY" if layer.param in (0, 255) else "{:.1f} KEYS".format(layer.param / 15))
        else:
            self.val_param.setText(pct(layer.param))
        self.val_opacity.setText(pct(layer.opacity))

    def refresh_keys(self):
        layer = self.layer
        if self.picking_center:
            text = tr("ArmedThemes", "CLICK A KEY TO PUT THE POINT THERE")
        elif layer is None:
            text = ""
        elif layer.type == PAINT:
            text = tr("ArmedThemes", "CLICK / DRAG TO PAINT · RIGHT-CLICK ERASES · CTRL+CLICK PICKS A KEY'S COLOUR")
        else:
            text = tr("ArmedThemes", "◤ = THIS LAYER SHOWS THERE · CLICK / DRAG TO CHANGE · RIGHT-DRAG REMOVES")
        self.lbl_keys.setText(text)
        self.lbl_tools.setText("PAINT" if layer is not None and layer.type == PAINT else "KEYS")
        for b in self.group_buttons:
            b.setEnabled(layer is not None)
        if layer is None or layer.type == PAINT:
            self.kb.covered = set()
        else:
            self.kb.covered = {self.led_keys[i] for i in layer.mask if i < len(self.led_keys)}
        self.kb.center = None
        if layer is not None and "dir" in TYPE_BY_ID[layer.type].controls and layer.flag(F_RADIAL):
            self.kb.center = self.led_to_px(layer.cx, layer.cy)
        self.kb.update()


    def now(self):
        return int((time.monotonic() - self.t0) * 1000)

    def follow_keyboard(self):
        """ Open the slot that is lit now, if another page switched to a theme """
        try:
            self.io.reload_values()
        except Exception:
            return
        effect = self.io.get(V_EFFECT)
        if effect in self.info.modes and self.info.modes.index(effect) != self.slot:
            self.flush_group()
            self.slot = self.info.modes.index(effect)
            self.cur = len(self.theme.layers) - 1
            self.btn_center.setChecked(False)
            self.refresh_all()

    def activate(self):
        if self.io is None:
            return
        if not self.loaded:
            self.load()
        else:
            self.follow_keyboard()
        self.groups = key_groups(self.leds, self.device.keyboard.layout)
        self.timer.start(1000 // FPS)
        self.set_shortcuts(True)

    def deactivate(self):
        self.timer.stop()
        self.set_shortcuts(False)
        if self.loaded:
            self.flush_group()
            self.push_timer.stop()
            self.push()

    def set_shortcuts(self, on):
        parent = self.kb_frame.window()
        if on and not self.shortcuts and parent is not None:
            for seq, fn in ((QKeySequence(QKeySequence.Undo), self.on_undo), (QKeySequence(QKeySequence.Redo), self.on_redo),
                            (QKeySequence("Ctrl+Y"), self.on_redo), (QKeySequence(QKeySequence.Save), self.on_save)):
                sc = QShortcut(seq, parent)
                sc.activated.connect(fn)
                self.shortcuts.append(sc)
        for sc in self.shortcuts:
            sc.setEnabled(on)

    def tick(self):
        if not self.loaded:
            return
        t = self.now()
        theme = self.theme
        layer = self.layer
        if self.chk_solo.isChecked() and layer is not None:
            solo = Theme(theme.name, len(self.leds))
            solo.paint = theme.paint
            shown = Layer.unpack(layer.pack(len(self.leds)), len(self.leds))
            shown.set_flag(F_HIDDEN, False)
            solo.layers = [shown]
            theme = solo
        r = self.renderer
        if any(l.type in (REACTIVE, RIPPLE) and not l.hidden for l in theme.layers) and t - self.last_demo > DEMO_PRESS_MS:
            self.last_demo = t
            r.hit(random.randrange(len(self.leds)), t)
        r.caps = self.caps_preview
        r.layer_on = {1} if self.fn_preview else set()
        v = min(255, self.io.get(V_VAL) * 255 // max(1, self.io.max_brightness)) if self.io.get(V_VAL) else 255
        for w, c in zip(self.led_keys, r.frame(theme, t, max(v, 90))):
            if w is not None:
                w.glow = c
        self.kb.update()
