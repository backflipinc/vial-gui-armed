# SPDX-License-Identifier: GPL-2.0-or-later
# HUD page: build the keyboard's main screen from widgets on a pixel-exact preview, and feed it from this computer
import hashlib
import json
import logging
import os
import sys

from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QObject, QLockFile, QItemSelectionModel, QEvent
from PyQt5.QtGui import QPainter, QColor, QPixmap, QIcon, QKeySequence
from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QGridLayout, QLabel, QPushButton, QLineEdit, QComboBox,     QCheckBox, QMenu, QFileDialog, QMessageBox, QWidget, QSpinBox, QDoubleSpinBox, QListWidget, QListWidgetItem,     QScrollArea, QStackedWidget, QAbstractItemView, QShortcut, QApplication, QAbstractSpinBox, \
    QAbstractScrollArea

import armed
import hud_agent
from hud_layout import Layout, Widget, LayoutError, TYPE_NAMES, FRAME_NAMES, COLOR_NAMES, ALIGN_NAMES, NEW_WIDGET,     W_TEXT, W_BAR, W_GRAPH, W_INDICATOR, W_BATTERY, W_FRAME, W_TEXTBOX, FRAME_BRACKETS,     C_BG, C_PANEL, C_LINE, C_DIM, C_TEXT, C_ACCENT, C_OK, C_WARN, C_AUTO, F_ALIGN, F_WARN, F_WARN_BELOW, F_FILL,     F_TAIL, F_DOT, S_NONE, S_LAYER, S_CHARGING, S_HOST, S_HOST_TXT, K_TEXT, K_NUMBER, K_LINES
from hud_presets import PRESETS
from hud_render import Renderer, QffFont, Sources, Value, DEFAULT_PALETTE, panel_rgb, value_text
from editor.basic_editor import BasicEditor
from editor.armed_screen import Job
from protocol.qmk_armed import F_HUD, SUB_HUD_FONT, HUD_FONT_PAYLOAD, ArmedError
from unlocker import Unlocker
from vial_device import VialKeyboard
from widgets.armed_controls import BracketFrame, Segmented
from widgets.hud_canvas import HudCanvas
from util import tr

UNDO_DEPTH = 100
PUSH_DELAY_MS = 120
POLL_MS = 150
CONFIG_SAVE_MS = 600


class AgentHost(QObject):
    """ Runs the HUD agent inside Vial Armed while no other agent holds the lock """

    changed = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.agent = hud_agent.HudAgent()
        os.makedirs(hud_agent.config_dir(), exist_ok=True)
        self.lock = QLockFile(os.path.join(hud_agent.config_dir(), "hud-agent.lock"))
        self.locked = False
        self.paused = False
        self.keyboard = None
        self.text_len = 160
        self.timer = QTimer()
        self.timer.timeout.connect(self.tick)
        self.timer.start(250)
        self.lock_timer = QTimer()
        self.lock_timer.timeout.connect(self.try_lock)
        self.lock_timer.start(5000)
        self.try_lock()

    def try_lock(self):
        if not self.locked:
            self.locked = self.lock.tryLock(0)
            if self.locked:
                self.attach()
                self.changed.emit()

    def set_device(self, device):
        self.agent.detach()
        self.keyboard = None
        if isinstance(device, VialKeyboard) and device.keyboard.armed is not None and device.keyboard.armed.has(F_HUD):
            self.keyboard = device.keyboard
            try:
                info = self.keyboard.armed.hud_info()
            except Exception as e:
                logging.warning("HUD agent: %s", e)
                info = None
            self.text_len = info.text_len if info else 160
        self.attach()
        self.changed.emit()

    def attach(self):
        if self.keyboard is None or not self.locked or self.paused:
            self.agent.detach()
            return
        dev = self.keyboard.dev

        def send(msg):
            if dev.write(b"\x00" + msg.ljust(32, b"\x00")) < 0:
                raise OSError("write failed")
        self.agent.attach(send, self.text_len)

    def set_paused(self, paused):
        self.paused = paused
        self.attach()
        self.changed.emit()

    def tick(self):
        was = self.agent.send is not None
        self.agent.tick()
        if was and self.agent.send is None:
            self.changed.emit()

    @property
    def feeding(self):
        return self.agent.send is not None

    def status(self):
        if not self.locked:
            return armed.WARN, tr("ArmedHud", "ANOTHER VIAL ARMED OR `vial-armed --agent` FEEDS THE HUD")
        if self.keyboard is None:
            return armed.LINE2, tr("ArmedHud", "NO KEYBOARD WITH A HUD")
        if self.paused:
            return armed.ACC, tr("ArmedHud", "PAUSED WHILE A TRANSFER RUNS")
        if not self.feeding:
            return armed.WARN, tr("ArmedHud", "STOPPED: ") + self.agent.last_error
        return armed.OK, tr("ArmedHud", "FEEDING THE KEYBOARD")


class PreviewSources(Sources):

    def __init__(self, agent):
        self.agent = agent
        self.kb = {}
        self.kb_history = {}

    def value(self, s):
        if S_HOST <= s < S_HOST + hud_agent.HOST_VALUES:
            v = self.agent.values[s - S_HOST]
            return self.host_value(v)
        if S_HOST_TXT <= s < S_HOST_TXT + hud_agent.HOST_TEXTS:
            t = self.agent.texts[s - S_HOST_TXT]
            if t is None:
                return Value(valid=False, hint=C_LINE, text="--")
            return Value(text=t.split("\n")[0][:23])
        return self.kb.get(s)

    @staticmethod
    def host_value(v):
        if v is None:
            return Value(valid=False, hint=C_LINE)
        return Value(int(round(v[0] * 10 ** v[1])), v[1])

    def host_text(self, slot):
        return self.agent.texts[slot]

    def history(self, s):
        if S_HOST <= s < S_HOST + hud_agent.HOST_VALUES:
            return [self.host_value(v) for v in self.agent.history[s - S_HOST]]
        return self.kb_history.get(s, [])


def make_icon(kind, size=18):
    """ small line icons for the arrange tools """
    pm = QPixmap(size, size)
    pm.fill(Qt.transparent)
    qp = QPainter(pm)
    fg, acc = QColor(armed.TXT), QColor(armed.ACC)
    s = size

    def bar(x, y, w, h, c=fg):
        qp.fillRect(x, y, w, h, c)

    if kind.startswith("align_"):
        k = kind[6:]
        if k in ("left", "hcenter", "right"):
            x = {"left": 2, "hcenter": s // 2, "right": s - 3}[k]
            bar(x, 1, 1, s - 2, acc)
            for y, w in ((4, 11), (10, 7)):
                bx = {"left": 3, "hcenter": s // 2 - w // 2, "right": s - 3 - w}[k]
                bar(bx, y, w, 4)
        else:
            y = {"top": 2, "vcenter": s // 2, "bottom": s - 3}[k]
            bar(1, y, s - 2, 1, acc)
            for x, h in ((4, 11), (10, 7)):
                by = {"top": 3, "vcenter": s // 2 - h // 2, "bottom": s - 3 - h}[k]
                bar(x, by, 4, h)
    elif kind == "dist_h":
        bar(1, 1, 1, s - 2, acc)
        bar(s - 2, 1, 1, s - 2, acc)
        for x in (3, 8, 13):
            bar(x, 5, 2, 8)
    elif kind == "dist_v":
        bar(1, 1, s - 2, 1, acc)
        bar(1, s - 2, s - 2, 1, acc)
        for y in (3, 8, 13):
            bar(5, y, 8, 2)
    elif kind == "same_w":
        bar(3, 3, 12, 4)
        bar(3, 11, 12, 4)
        bar(2, 8, 14, 1, acc)
    elif kind == "same_h":
        bar(3, 3, 4, 12)
        bar(11, 3, 4, 12)
        bar(8, 2, 1, 14, acc)
    qp.end()
    return QIcon(pm)


class RoleSwatches(QWidget):
    """ Colour roles as swatches in the keyboard's colours; AUTO first when allowed """

    changed = pyqtSignal(int)

    def __init__(self, roles):
        super().__init__()
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)
        self.buttons = {}
        for r in roles:
            b = QPushButton("A" if r == C_AUTO else "")
            b.setObjectName("armedSwatch")
            b.setCheckable(True)
            b.setFixedSize(26, 22)
            b.setToolTip(ROLE_TIPS.get(r, COLOR_NAMES[r]))
            b.clicked.connect(lambda _, r=r: self.changed.emit(r))
            lay.addWidget(b)
            self.buttons[r] = b
        lay.addStretch(1)

    def set_palette(self, palette):
        for r, b in self.buttons.items():
            if r == C_AUTO:
                continue
            c = panel_rgb(palette[r]) if r < len(palette) else (0, 0, 0)
            b.setStyleSheet("background: rgb({},{},{});".format(*c))

    def set_current(self, role):
        for r, b in self.buttons.items():
            b.setChecked(r == role)


ROLE_TIPS = {
    C_AUTO: "AUTO: the colour the source suggests (accent above the base layer, green while charging, link state, ...)",
    C_BG: "BG: the screen's black", C_PANEL: "PANEL: lit tiles", C_LINE: "LINE: separators, unlit parts",
    C_DIM: "DIM: labels", C_TEXT: "TEXT: values", C_ACCENT: "ACCENT: follows DISPLAY > ACCENT on the keyboard",
    C_OK: "OK: green", C_WARN: "WARN: red",
}

TYPE_TIPS = {
    W_TEXT: "Text with a live value in place of {}, or a fixed label",
    W_BAR: "A bar filled by a number between two ends",
    W_GRAPH: "A number over time",
    W_INDICATOR: "A tile lit while the source is on, or a coloured dot",
    W_BATTERY: "The keyboard's battery icon",
    W_FRAME: "Corner brackets, a box, a line or a filled panel",
    W_TEXTBOX: "Several lines of text, e.g. a command's output",
}

ZOOMS = ("FIT", "1×", "2×", "3×", "4×")


class WheelGuard(QObject):
    """ The wheel scrolls the page; it changes a combo box or a number only after a click on it """

    def eventFilter(self, obj, ev):
        if ev.type() != QEvent.Wheel or obj.hasFocus():
            return False
        p = obj.parentWidget()
        while p is not None and not isinstance(p, QAbstractScrollArea):
            p = p.parentWidget()
        if p is not None:
            QApplication.sendEvent(p.verticalScrollBar(), ev)
        return True


class ArmedHud(BasicEditor):

    def __init__(self, agent_host):
        super().__init__()
        self.host = agent_host
        self.host.changed.connect(self.refresh_agent)
        self.io = None
        self.info = None
        self.sources = []
        self.fonts = []
        self.palette = list(DEFAULT_PALETTE)
        self.preview = PreviewSources(self.host.agent)
        self.renderer = None
        self.hud = Layout()
        self.areas = []
        self.saved = None  # stored on the keyboard
        self.sent = None  # blob the keyboard shows now
        self.loaded = False
        self.sel = []  # widget indexes, primary last
        self.undo, self.redo = [], []
        self.updating = False
        self.job = None
        self.poll_idx = 0
        self.font_tries = 0
        self.cfg = hud_agent.load_config()
        self.shortcuts = []
        self.cursor = (-1, -1)

        self.push_timer = QTimer()
        self.push_timer.setSingleShot(True)
        self.push_timer.timeout.connect(self.push)
        self.poll_timer = QTimer()
        self.poll_timer.timeout.connect(self.poll)
        self.paint_timer = QTimer()
        self.paint_timer.timeout.connect(self.redraw)
        self.cfg_timer = QTimer()
        self.cfg_timer.setSingleShot(True)
        self.cfg_timer.timeout.connect(self.save_config)

        # ---- top bar
        top = QHBoxLayout()
        top.setSpacing(10)
        self.view = Segmented([tr("ArmedHud", "LAYOUT"), tr("ArmedHud", "AGENT")])
        self.view.set_current(0)
        self.view.changed.connect(self.on_view)
        top.addWidget(self.view)
        top.addSpacing(8)
        self.lbl_state = QLabel()
        top.addWidget(self.lbl_state)
        self.lbl_error = QLabel()
        self.lbl_error.setObjectName("armedHint")
        top.addWidget(self.lbl_error)
        top.addStretch(1)
        self.btn_undo = QPushButton(tr("ArmedHud", "UNDO"))
        self.btn_undo.setToolTip("Ctrl+Z")
        self.btn_undo.clicked.connect(self.on_undo)
        self.btn_redo = QPushButton(tr("ArmedHud", "REDO"))
        self.btn_redo.setToolTip("Ctrl+Shift+Z")
        self.btn_redo.clicked.connect(self.on_redo)
        self.btn_presets = QPushButton(tr("ArmedHud", "START FROM ▾"))
        menu = QMenu(self.btn_presets)
        for name, fn in PRESETS:
            menu.addAction(name, lambda fn=fn: self.on_preset(fn))
        self.btn_presets.setMenu(menu)
        self.btn_import = QPushButton(tr("ArmedHud", "IMPORT"))
        self.btn_import.clicked.connect(self.on_import)
        self.btn_export = QPushButton(tr("ArmedHud", "EXPORT"))
        self.btn_export.clicked.connect(self.on_export)
        self.btn_revert = QPushButton(tr("ArmedHud", "REVERT"))
        self.btn_revert.setToolTip(tr("ArmedHud", "Back to the layout stored on the keyboard"))
        self.btn_revert.clicked.connect(self.on_revert)
        self.btn_save = QPushButton(tr("ArmedHud", "SAVE"))
        self.btn_save.setToolTip(tr("ArmedHud", "Store on the keyboard (Ctrl+S)"))
        self.btn_save.setDefault(True)
        self.btn_save.clicked.connect(self.on_save)
        for b in (self.btn_undo, self.btn_redo):
            top.addWidget(b)
        top.addSpacing(10)
        for b in (self.btn_presets, self.btn_import, self.btn_export):
            top.addWidget(b)
        top.addSpacing(10)
        for b in (self.btn_revert, self.btn_save):
            top.addWidget(b)

        self.stack = QStackedWidget()
        self.stack.addWidget(self.build_layout_view())
        self.stack.addWidget(self.build_agent_view())

        self.wheel_guard = WheelGuard()
        for w in self.stack.findChildren(QComboBox) + self.stack.findChildren(QAbstractSpinBox):
            w.setFocusPolicy(Qt.StrongFocus)
            w.installEventFilter(self.wheel_guard)

        self.addLayout(top)
        self.addSpacing(10)
        self.addWidget(self.stack, 1)
        self.load_agent_rows()
        self.refresh_agent()

    # ---- building the views

    def build_layout_view(self):
        # screen
        self.screen_frame = BracketFrame("SCREEN", accent=True)
        self.lbl_screen = QLabel()
        self.lbl_screen.setObjectName("armedHint")
        self.screen_frame.header.insertWidget(1, self.lbl_screen)
        self.zoom = Segmented(ZOOMS)
        self.zoom.set_current(0)
        self.zoom.changed.connect(lambda _: self.fit_canvas())
        self.screen_frame.header.addWidget(self.zoom)

        add = QHBoxLayout()
        add.setSpacing(4)
        lbl = QLabel(tr("ArmedHud", "ADD"))
        lbl.setObjectName("armedKey")
        lbl.setFixedWidth(64)
        add.addWidget(lbl)
        self.add_buttons = []
        for t, name in TYPE_NAMES.items():
            b = QPushButton(name)
            b.setObjectName("armedTool")
            b.setToolTip(TYPE_TIPS.get(t, ""))
            b.clicked.connect(lambda _, t=t: self.on_add(t))
            add.addWidget(b)
            self.add_buttons.append(b)
        add.addStretch(1)
        self.screen_frame.box.addLayout(add)

        arrange = QHBoxLayout()
        arrange.setSpacing(4)
        lbl = QLabel(tr("ArmedHud", "ARRANGE"))
        lbl.setObjectName("armedKey")
        lbl.setFixedWidth(64)
        arrange.addWidget(lbl)
        self.arrange_buttons = []
        tools = [
            ("align_left", "Align left edges", lambda: self.align("left")),
            ("align_hcenter", "Align centres horizontally", lambda: self.align("hcenter")),
            ("align_right", "Align right edges", lambda: self.align("right")),
            None,
            ("align_top", "Align top edges", lambda: self.align("top")),
            ("align_vcenter", "Align middles vertically", lambda: self.align("vcenter")),
            ("align_bottom", "Align bottom edges", lambda: self.align("bottom")),
            None,
            ("dist_h", "Distribute horizontally: equal gaps (3 or more)", lambda: self.distribute("x")),
            ("dist_v", "Distribute vertically: equal gaps (3 or more)", lambda: self.distribute("y")),
            None,
            ("same_w", "Same width as the last selected", lambda: self.match("w")),
            ("same_h", "Same height as the last selected", lambda: self.match("h")),
        ]
        for t in tools:
            if t is None:
                arrange.addSpacing(10)
                continue
            b = QPushButton()
            b.setObjectName("armedTool")
            b.setIcon(make_icon(t[0]))
            b.setToolTip(tr("ArmedHud", t[1]) + tr("ArmedHud", " — one widget: to the screen; several: to their bounds"))
            b.clicked.connect(t[2])
            arrange.addWidget(b)
            self.arrange_buttons.append((t[0], b))
        arrange.addStretch(1)
        self.chk_snap = QCheckBox(tr("ArmedHud", "SNAP"))
        self.chk_snap.setChecked(True)
        self.chk_snap.setToolTip(tr("ArmedHud", "Snap to the screen and to other widgets while dragging; hold Alt to drag freely"))
        self.chk_snap.toggled.connect(lambda on: setattr(self.canvas, "snap", on))
        self.chk_bounds = QCheckBox(tr("ArmedHud", "BOUNDS"))
        self.chk_bounds.setToolTip(tr("ArmedHud", "Outline every widget"))
        self.chk_bounds.toggled.connect(self.on_bounds)
        arrange.addWidget(self.chk_snap)
        arrange.addWidget(self.chk_bounds)
        self.screen_frame.box.addLayout(arrange)

        self.canvas = HudCanvas()
        self.canvas.selection_changed.connect(lambda: self.set_selection(list(self.canvas.sel), from_canvas=True))
        self.canvas.edit_started.connect(self.snapshot)
        self.canvas.edited.connect(lambda: self.changed(refresh_props=True))
        self.canvas.nudge.connect(self.on_nudge)
        self.canvas.context_requested.connect(self.on_context)
        self.canvas.cursor_moved.connect(self.on_cursor)
        self.canvas.activated.connect(self.on_activate)
        self.canvas_scroll = QScrollArea()
        self.canvas_scroll.setAlignment(Qt.AlignCenter)
        self.canvas_scroll.setWidget(self.canvas)
        self.canvas_scroll.setMinimumHeight(200)
        self.screen_frame.box.addWidget(self.canvas_scroll, 1)
        self.screen_frame.resized = self.fit_canvas

        self.lbl_status = QLabel()
        self.lbl_status.setObjectName("armedHint")
        self.lbl_status.setWordWrap(True)
        self.screen_frame.box.addWidget(self.lbl_status)

        # widget list
        list_frame = BracketFrame("WIDGETS")
        self.lbl_count = QLabel()
        self.lbl_count.setObjectName("armedHint")
        list_frame.header.insertWidget(1, self.lbl_count)
        self.list = QListWidget()
        self.list.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.list.setDragDropMode(QAbstractItemView.InternalMove)
        self.list.setToolTip(tr("ArmedHud", "The top one is drawn last, over the others. Drag to change the order"))
        self.list.itemSelectionChanged.connect(self.on_list_selection)
        # a drag inside the list arrives as a move or as a remove + insert, depending on the Qt version
        for sig in (self.list.model().rowsMoved, self.list.model().rowsInserted, self.list.model().rowsRemoved):
            sig.connect(lambda *a: None if self.updating else QTimer.singleShot(0, self.on_rows_moved))
        list_frame.box.addWidget(self.list, 1)
        row = QHBoxLayout()
        row.setSpacing(4)
        self.btn_dup = QPushButton(tr("ArmedHud", "DUPLICATE"))
        self.btn_dup.setToolTip("Ctrl+D")
        self.btn_dup.clicked.connect(self.on_duplicate)
        self.btn_up = QPushButton("▲")
        self.btn_up.setToolTip(tr("ArmedHud", "Bring forward (Ctrl+])"))
        self.btn_up.clicked.connect(lambda: self.on_order(1))
        self.btn_down = QPushButton("▼")
        self.btn_down.setToolTip(tr("ArmedHud", "Send backward (Ctrl+[)"))
        self.btn_down.clicked.connect(lambda: self.on_order(-1))
        self.btn_del = QPushButton(tr("ArmedHud", "DELETE"))
        self.btn_del.setToolTip("Del")
        self.btn_del.clicked.connect(self.on_delete)
        for b in (self.btn_dup, self.btn_up, self.btn_down, self.btn_del):
            row.addWidget(b)
        list_frame.box.addLayout(row)

        self.props = BracketFrame("PROPERTIES")
        self.build_props()
        props_scroll = QScrollArea()
        props_scroll.setWidgetResizable(True)
        props_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        holder = QWidget()
        hl = QVBoxLayout(holder)
        hl.setContentsMargins(0, 0, 0, 0)
        hl.addWidget(self.props)
        props_scroll.setWidget(holder)

        right = QVBoxLayout()
        right.setSpacing(14)
        right.setContentsMargins(0, 0, 0, 0)
        right.addWidget(list_frame, 2)
        right.addWidget(props_scroll, 3)
        right_w = QWidget()
        right_w.setLayout(right)
        right_w.setFixedWidth(420)

        body = QHBoxLayout()
        body.setSpacing(18)
        body.setContentsMargins(0, 0, 0, 0)
        body.addWidget(self.screen_frame, 1)
        body.addWidget(right_w)
        view = QWidget()
        view.setLayout(body)

        for seq, fn in (("Ctrl+D", self.on_duplicate), (QKeySequence.Copy, self.on_copy), (QKeySequence.Paste, self.on_paste),
                        (QKeySequence.SelectAll, self.select_all), (QKeySequence.Delete, self.on_delete),
                        ("Backspace", self.on_delete), ("Escape", lambda: self.set_selection([])),
                        ("Ctrl+]", lambda: self.on_order(1)), ("Ctrl+[", lambda: self.on_order(-1))):
            for w in (self.canvas, self.list):
                sc = QShortcut(QKeySequence(seq), w)
                sc.setContext(Qt.WidgetShortcut)
                sc.activated.connect(fn)
        return view

    def build_props(self):
        box = self.props.box
        self.lbl_sel = QLabel()
        self.lbl_sel.setObjectName("armedHint")
        self.lbl_sel.setWordWrap(True)
        box.addWidget(self.lbl_sel)
        self.rows = {}
        self.sections = []

        def section(title):
            lbl = QLabel(title)
            lbl.setObjectName("armedSection")
            box.addWidget(lbl)
            grid = QGridLayout()
            grid.setHorizontalSpacing(10)
            grid.setVerticalSpacing(7)
            grid.setColumnStretch(1, 1)
            box.addLayout(grid)
            self.sections.append((lbl, []))
            return grid

        def row(grid, key, label, widget):
            r = grid.rowCount()
            lbl = QLabel(label)
            lbl.setObjectName("armedKey")
            lbl.setFixedWidth(92)
            grid.addWidget(lbl, r, 0)
            if isinstance(widget, (QHBoxLayout, QVBoxLayout, QGridLayout)):
                holder = QWidget()
                widget.setContentsMargins(0, 0, 0, 0)
                holder.setLayout(widget)
                widget = holder
            grid.addWidget(widget, r, 1)
            self.rows[key] = (lbl, widget)
            self.sections[-1][1].append(key)

        g = section(tr("ArmedHud", "CONTENT"))
        self.type_combo = QComboBox()
        for t, name in TYPE_NAMES.items():
            self.type_combo.addItem(name, t)
        self.type_combo.currentIndexChanged.connect(self.on_type)
        row(g, "type", "TYPE", self.type_combo)
        src = QVBoxLayout()
        src.setSpacing(3)
        self.source_combo = QComboBox()
        self.source_combo.currentIndexChanged.connect(self.on_source)
        self.lbl_now = QLabel()
        self.lbl_now.setObjectName("armedHint")
        src.addWidget(self.source_combo)
        src.addWidget(self.lbl_now)
        row(g, "source", "SOURCE", src)
        lab = QHBoxLayout()
        lab.setSpacing(4)
        self.label_edit = QLineEdit()
        self.label_edit.setMaxLength(46)
        self.label_edit.textEdited.connect(lambda t: self.edit(label=t))
        self.btn_value = QPushButton("{}")
        self.btn_value.setObjectName("armedTool")
        self.btn_value.setToolTip(tr("ArmedHud", "Insert the value"))
        self.btn_value.clicked.connect(self.insert_value)
        lab.addWidget(self.label_edit, 1)
        lab.addWidget(self.btn_value)
        row(g, "label", "TEXT", lab)
        self.digits = QSpinBox()
        self.digits.setRange(0, 9)
        self.digits.setSpecialValueText(tr("ArmedHud", "AS IS"))
        self.digits.setSuffix(tr("ArmedHud", " DIGITS"))
        self.digits.setToolTip(tr("ArmedHud", "Pad numbers with zeros: 3 shows 7 as 007"))
        self.digits.valueChanged.connect(lambda v: self.edit(param=v))
        row(g, "digits", "PAD", self.digits)

        g = section(tr("ArmedHud", "LOOK"))
        self.font_combo = QComboBox()
        self.font_combo.currentIndexChanged.connect(lambda i: self.edit(font=i) if i >= 0 else None)
        row(g, "font", "FONT", self.font_combo)
        self.fg = RoleSwatches((C_AUTO, C_TEXT, C_ACCENT, C_DIM, C_LINE, C_OK, C_WARN, C_PANEL))
        self.fg.changed.connect(lambda r: self.edit(fg=r))
        row(g, "fg", "COLOUR", self.fg)
        self.bg = RoleSwatches((C_BG, C_PANEL, C_LINE, C_DIM, C_ACCENT))
        self.bg.changed.connect(lambda r: self.edit(bg=r))
        row(g, "bg", "BACKGROUND", self.bg)
        self.align_seg = Segmented(ALIGN_NAMES)
        self.align_seg.changed.connect(lambda i: self.edit_flags(F_ALIGN, i))
        row(g, "align", "ALIGN TEXT", self.align_seg)
        self.chk_dot = QCheckBox(tr("ArmedHud", "DOT ONLY"))
        self.chk_dot.toggled.connect(lambda on: self.edit_flags(F_DOT, F_DOT if on else 0))
        row(g, "dot", "STYLE", self.chk_dot)
        frame = QHBoxLayout()
        self.frame_style = QComboBox()
        for i, n in enumerate(FRAME_NAMES):
            self.frame_style.addItem(n, i)
        self.frame_style.currentIndexChanged.connect(lambda i: self.edit(param=i) if i >= 0 else None)
        self.corner = QSpinBox()
        self.corner.setRange(0, 60)
        self.corner.setSpecialValueText(tr("ArmedHud", "CORNER 6 PX"))
        self.corner.setPrefix(tr("ArmedHud", "CORNER "))
        self.corner.setSuffix(" PX")
        self.corner.valueChanged.connect(lambda v: self.edit(lo=v))
        frame.addWidget(self.frame_style)
        frame.addWidget(self.corner)
        row(g, "frame", "SHAPE", frame)
        self.segments = QSpinBox()
        self.segments.setRange(0, 40)
        self.segments.setSpecialValueText(tr("ArmedHud", "SOLID"))
        self.segments.setSuffix(tr("ArmedHud", " SEGMENTS"))
        self.segments.valueChanged.connect(lambda v: self.edit(param=v if v != 1 else 2))
        row(g, "segments", "FILL", self.segments)
        graph = QHBoxLayout()
        self.period = QDoubleSpinBox()
        self.period.setRange(0.1, 25.5)
        self.period.setSingleStep(0.5)
        self.period.setDecimals(1)
        self.period.setPrefix(tr("ArmedHud", "EVERY "))
        self.period.setSuffix(" S")
        self.period.valueChanged.connect(lambda v: self.edit(param=int(round(v * 10))))
        self.chk_fill = QCheckBox(tr("ArmedHud", "COLUMNS"))
        self.chk_fill.setToolTip(tr("ArmedHud", "Filled columns instead of a line"))
        self.chk_fill.toggled.connect(lambda on: self.edit_flags(F_FILL, F_FILL if on else 0))
        graph.addWidget(self.period)
        graph.addWidget(self.chk_fill)
        row(g, "period", "SAMPLE", graph)
        lines = QHBoxLayout()
        self.gap = QSpinBox()
        self.gap.setRange(0, 20)
        self.gap.setPrefix(tr("ArmedHud", "GAP "))
        self.gap.setSuffix(" PX")
        self.gap.valueChanged.connect(lambda v: self.edit(param=v))
        self.chk_tail = QCheckBox(tr("ArmedHud", "SHOW THE END"))
        self.chk_tail.setToolTip(tr("ArmedHud", "When the text doesn't fit, show its last lines, like a terminal"))
        self.chk_tail.toggled.connect(lambda on: self.edit_flags(F_TAIL, F_TAIL if on else 0))
        lines.addWidget(self.gap)
        lines.addWidget(self.chk_tail)
        row(g, "lines", "LINES", lines)

        g = section(tr("ArmedHud", "POSITION AND SIZE"))
        geo = QGridLayout()
        geo.setHorizontalSpacing(6)
        geo.setVerticalSpacing(5)
        self.spin = {}
        for n, k in enumerate(("x", "y", "w", "h")):
            sp = QSpinBox()
            sp.setRange(0, 999)
            sp.setPrefix(k.upper() + "  ")
            sp.setSuffix(" PX")
            sp.valueChanged.connect(lambda v, k=k: self.edit(**{k: v}))
            self.spin[k] = sp
            geo.addWidget(sp, n // 2, n % 2)
        self.spin["w"].setSpecialValueText(tr("ArmedHud", "W  TO EDGE"))
        self.spin["h"].setSpecialValueText(tr("ArmedHud", "H  ONE LINE"))
        row(g, "geo", "", geo)

        g = section(tr("ArmedHud", "RANGE AND ALERT"))
        rng = QHBoxLayout()
        self.lo = QDoubleSpinBox()
        self.hi = QDoubleSpinBox()
        dash = QLabel("…")
        for sp, k in ((self.lo, "lo"), (self.hi, "hi")):
            sp.setRange(-32768, 32767)
            sp.valueChanged.connect(lambda v, k=k: self.edit_scaled(k, v))
        rng.addWidget(self.lo, 1)
        rng.addWidget(dash)
        rng.addWidget(self.hi, 1)
        row(g, "range", "FROM … TO", rng)
        warn = QHBoxLayout()
        self.chk_warn = QCheckBox(tr("ArmedHud", "RED FROM"))
        self.chk_warn.setToolTip(tr("ArmedHud", "Switch to the WARN colour at this value"))
        self.chk_warn.toggled.connect(lambda on: self.edit_flags(F_WARN, F_WARN if on else 0))
        self.warn = QDoubleSpinBox()
        self.warn.setRange(-32768, 32767)
        self.warn.valueChanged.connect(lambda v: self.edit_scaled("warn", v))
        self.below = QComboBox()
        self.below.addItem(tr("ArmedHud", "AND UP"), 0)
        self.below.addItem(tr("ArmedHud", "AND DOWN"), F_WARN_BELOW)
        self.below.currentIndexChanged.connect(lambda i: self.edit_flags(F_WARN_BELOW, self.below.itemData(i) or 0))
        warn.addWidget(self.chk_warn)
        warn.addWidget(self.warn, 1)
        warn.addWidget(self.below)
        row(g, "warn", "ALERT", warn)

        self.lbl_hint = QLabel()
        self.lbl_hint.setObjectName("armedHint")
        self.lbl_hint.setWordWrap(True)
        box.addSpacing(6)
        box.addWidget(self.lbl_hint)
        box.addStretch(1)

    def build_agent_view(self):
        self.agent_frame = BracketFrame("AGENT", accent=True)
        self.lbl_agent = QLabel()
        self.agent_frame.header.insertWidget(1, self.lbl_agent)
        self.chk_autostart = QCheckBox(tr("ArmedHud", "RUN AT LOGIN WITHOUT THE WINDOW"))
        self.chk_autostart.setToolTip(tr("ArmedHud", "Adds `vial-armed --agent` to your desktop's autostart"))
        self.chk_autostart.toggled.connect(self.on_autostart)
        self.agent_frame.header.addWidget(self.chk_autostart)
        self.chk_autostart.setVisible(sys.platform.startswith("linux"))
        note = QLabel(tr("ArmedHud", "What this computer sends to the HUD while Vial Armed runs: pick these as sources "
                                     "(H1–H16, T1–T4) in the LAYOUT view. Over USB and the 2.4 GHz dongle; Bluetooth has "
                                     "no data channel. Commands run here as you, through the shell, and are kept only on "
                                     "this computer. A number takes the first number the command prints, a text its "
                                     "whole output."))
        note.setObjectName("armedHint")
        note.setWordWrap(True)
        self.agent_frame.box.addWidget(note)

        def header(grid, titles):
            for c, t in enumerate(titles):
                lbl = QLabel(t)
                lbl.setObjectName("armedSection")
                grid.addWidget(lbl, 0, c)

        values = QGridLayout()
        values.setHorizontalSpacing(8)
        values.setVerticalSpacing(5)
        header(values, ("", tr("ArmedHud", "NUMBER"), tr("ArmedHud", "COMMAND"), tr("ArmedHud", "EVERY"),
                        tr("ArmedHud", "DECIMALS"), tr("ArmedHud", "NOW"), tr("ArmedHud", "ON SCREEN")))
        self.value_rows = []
        for i in range(hud_agent.HOST_VALUES):
            lbl = QLabel("H{}".format(i + 1))
            lbl.setObjectName("armedKey")
            metric = QComboBox()
            metric.addItem("—", "")
            for m in hud_agent.METRICS:
                metric.addItem(m.name, m.key)
            cmd = QLineEdit()
            cmd.setPlaceholderText(tr("ArmedHud", "pick COMMAND to run your own"))
            interval = QDoubleSpinBox()
            interval.setRange(0.5, 3600)
            interval.setDecimals(1)
            interval.setSuffix(" S")
            dec = QSpinBox()
            dec.setRange(0, 4)
            live = QLabel()
            live.setMinimumWidth(80)
            used = QLabel()
            used.setObjectName("armedHint")
            for k, w in enumerate((lbl, metric, cmd, interval, dec, live, used)):
                values.addWidget(w, i + 1, k)
            metric.currentIndexChanged.connect(lambda _, i=i: self.on_value_cfg(i, "metric"))
            cmd.textEdited.connect(lambda _, i=i: self.on_value_cfg(i, "command"))
            interval.valueChanged.connect(lambda _, i=i: self.on_value_cfg(i, "interval"))
            dec.valueChanged.connect(lambda _, i=i: self.on_value_cfg(i, "dec"))
            self.value_rows.append((metric, cmd, interval, dec, live, used))
        values.setColumnStretch(2, 1)
        self.agent_frame.box.addLayout(values)
        self.agent_frame.box.addSpacing(10)

        texts = QGridLayout()
        texts.setHorizontalSpacing(8)
        texts.setVerticalSpacing(5)
        header(texts, ("", tr("ArmedHud", "TEXT FROM A COMMAND"), "", tr("ArmedHud", "EVERY"), tr("ArmedHud", "OUTPUT"),
                       tr("ArmedHud", "ON SCREEN")))
        self.text_rows = []
        for i in range(hud_agent.HOST_TEXTS):
            lbl = QLabel("T{}".format(i + 1))
            lbl.setObjectName("armedKey")
            cmd = QLineEdit()
            cmd.setPlaceholderText(tr("ArmedHud", "e.g. uptime -p"))
            ex = QPushButton(tr("ArmedHud", "EXAMPLES ▾"))
            m = QMenu(ex)
            for name, example in hud_agent.TEXT_EXAMPLES:
                m.addAction(name, lambda i=i, example=example: self.set_text_command(i, example))
            ex.setMenu(m)
            interval = QDoubleSpinBox()
            interval.setRange(0.5, 3600)
            interval.setDecimals(1)
            interval.setSuffix(" S")
            live = QLabel()
            live.setMinimumWidth(240)
            used = QLabel()
            used.setObjectName("armedHint")
            for k, w in enumerate((lbl, cmd, ex, interval, live, used)):
                texts.addWidget(w, i + 1, k)
            cmd.textEdited.connect(lambda _, i=i: self.on_text_cfg(i))
            interval.valueChanged.connect(lambda _, i=i: self.on_text_cfg(i))
            self.text_rows.append((cmd, interval, live, used))
        texts.setColumnStretch(1, 2)
        texts.setColumnStretch(4, 1)
        self.agent_frame.box.addLayout(texts)
        self.agent_frame.box.addStretch(1)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        holder = QWidget()
        hl = QVBoxLayout(holder)
        hl.setContentsMargins(0, 0, 0, 0)
        hl.addWidget(self.agent_frame)
        scroll.setWidget(holder)
        return scroll

    # ---- device

    def valid(self):
        if not isinstance(self.device, VialKeyboard):
            return False
        io = self.device.keyboard.armed
        return io is not None and io.has(F_HUD)

    def rebuild(self, device):
        super().rebuild(device)
        self.io = device.keyboard.armed if self.valid() else None
        self.loaded = False
        self.fonts = []
        self.poll_timer.stop()
        self.paint_timer.stop()
        if self.io is None:
            return
        try:
            self.info = self.io.hud_info()
            self.sources = self.io.hud_sources(self.info)
            self.palette = self.io.hud_palette(self.info)
        except Exception as e:
            logging.warning("HUD: %s", e)
            self.io = None
            return
        self.canvas.set_screen(self.info.width, self.info.height)
        self.lbl_screen.setText("{} × {} PX".format(self.info.width, self.info.height))
        self.fg.set_palette(self.palette)
        self.bg.set_palette(self.palette)

    def activate(self):
        if self.io is None:
            return
        if not self.loaded:
            self.load()
        else:
            try:
                self.palette = self.io.hud_palette(self.info)
                self.fg.set_palette(self.palette)
                self.bg.set_palette(self.palette)
            except Exception:
                pass
        self.poll_timer.start(POLL_MS)
        self.paint_timer.start(500)
        self.set_shortcuts(True)
        QTimer.singleShot(0, self.fit_canvas)

    def deactivate(self):
        self.poll_timer.stop()
        self.paint_timer.stop()
        self.set_shortcuts(False)
        if self.loaded:
            self.push_timer.stop()
            self.push()

    def set_shortcuts(self, on):
        parent = self.screen_frame.window()
        if on and not self.shortcuts and parent is not None:
            for seq, fn in ((QKeySequence(QKeySequence.Undo), self.on_undo),
                            (QKeySequence(QKeySequence.Redo), self.on_redo), (QKeySequence("Ctrl+Y"), self.on_redo),
                            (QKeySequence(QKeySequence.Save), self.on_save)):
                sc = QShortcut(seq, parent)
                sc.activated.connect(fn)
                self.shortcuts.append(sc)
        for sc in self.shortcuts:
            sc.setEnabled(on)

    def load(self):
        try:
            blob = self.io.hud_read(self.info)
            self.hud = Layout.unpack(blob, self.info)
        except (ArmedError, LayoutError, RuntimeError) as e:
            self.lbl_error.setText("ERROR: {}".format(e))
            return
        self.saved = self.hud.copy()
        self.sent = blob
        self.undo, self.redo = [], []
        self.loaded = True
        self.sel = []
        self.font_tries = 0
        self.load_fonts()
        self.refresh_all()

    def font_cache(self, head):
        key = hashlib.sha1(bytes(head) + self.io.version_str.encode()).hexdigest()[:16]
        return os.path.join(hud_agent.config_dir(), "hud-fonts", key + ".qff")

    def load_fonts(self):
        """ the keyboard's fonts: from the cache, or read once (a few seconds over USB, longer over the dongle) """
        if self.job is not None:
            return
        io, n = self.io, self.info.fonts
        heads = []
        try:
            for i in range(n):
                heads.append(io._send(SUB_HUD_FONT, i, 0, 0, 0, 0, HUD_FONT_PAYLOAD)[8:32])
        except Exception as e:
            logging.warning("HUD fonts: %s", e)
            return
        fonts = [None] * n
        missing = []
        for i, h in enumerate(heads):
            try:
                with open(self.font_cache(h), "rb") as f:
                    fonts[i] = f.read()
            except OSError:
                missing.append(i)
        if not missing:
            self.set_fonts(fonts)
            return

        def work(progress, cancelled):
            last = [-1]

            def report(k, d, t):
                # per piece would flood the UI thread, which holds the GIL while Qt relays the page out
                pct = (k * 100 + d * 100 // max(1, t)) // len(missing)
                if pct != last[0]:
                    last[0] = pct
                    progress(pct, 100)

            for k, i in enumerate(missing):
                fonts[i] = io.hud_font(i, lambda d, t, k=k: report(k, d, t))
                path = self.font_cache(heads[i])
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(fonts[i])
            return fonts

        self.job = Job(work)
        self.job.io = io
        self.job.progressed.connect(lambda d, t: self.lbl_error.setText(
            tr("ArmedHud", "READING THE KEYBOARD'S FONTS {}%").format(100 * d // max(1, t))))
        self.job.done.connect(self.on_fonts)
        Unlocker.global_main_window.set_busy(True)
        self.job.start()

    def on_fonts(self, fonts, error):
        job, self.job = self.job, None
        if job is None:
            return
        job.wait()
        Unlocker.global_main_window.set_busy(False)
        if job.io is not self.io or error:
            # the keyboard was reloaded meanwhile, or a reply got lost: what was read is cached, try again
            self.font_tries += 1
            if self.io is not None and self.loaded and self.font_tries <= 3:
                QTimer.singleShot(0, self.load_fonts)
            elif error:
                self.lbl_error.setText("ERROR: " + error)
            return
        self.lbl_error.setText("")
        self.set_fonts(fonts)

    def set_fonts(self, fonts):
        self.fonts = []
        for f in fonts:
            try:
                self.fonts.append(QffFont(f))
            except ValueError:
                self.fonts.append(None)
        self.updating = True
        self.font_combo.clear()
        for i, f in enumerate(self.fonts):
            self.font_combo.addItem("{} PX".format(f.line_height if f else "?"))
        self.updating = False
        self.refresh_all()

    # ---- rendering

    def redraw(self):
        if self.io is None or not self.loaded:
            return
        r = Renderer(self.info.width, self.info.height, self.fonts, self.palette, self.preview)
        r.names = {s.id: s.name for s in self.sources}
        image = r.render(self.hud)
        self.areas = [r.area(w) for w in self.hud.widgets]
        self.canvas.sel = list(self.sel)
        self.canvas.set_content(image, self.hud, self.areas)
        self.refresh_live()

    def refresh_live(self):
        for i, row in enumerate(self.value_rows):
            v = self.host.agent.values[i]
            row[4].setText("—" if v is None else "{:.{}f}".format(v[0], v[1]))
        for i, row in enumerate(self.text_rows):
            t = self.host.agent.texts[i]
            err = self.host.agent.text_runners[i].error
            row[2].setText(err.upper() if err else "—" if t is None else (t.split("\n")[0][:48] or " "))
        wd = self.current()
        if wd is not None and wd.source and len(self.sel) == 1:
            v = self.preview.value(wd.source)
            self.lbl_now.setText(tr("ArmedHud", "NOW: ") + (value_text(v, 0) if v else tr("ArmedHud", "not read yet")))
        self.refresh_agent()

    def poll(self):
        """ keyboard sources the layout uses, one per tick """
        if self.io is None or self.job is not None:
            return
        used = sorted({w.source for w in self.hud.widgets if 0 < w.source < S_HOST} |
                      ({S_CHARGING} if any(w.type == W_BATTERY for w in self.hud.widgets) else set()))
        if not used:
            return
        self.poll_idx = (self.poll_idx + 1) % len(used)
        s = used[self.poll_idx]
        try:
            r = self.io.hud_value(s)
        except Exception:
            return
        if r is None:
            self.preview.kb.pop(s, None)
            return
        v = Value(r[3], r[2], r[1], r[0], r[4])
        self.preview.kb[s] = v
        hist = self.preview.kb_history.setdefault(s, [])
        hist.append(v)
        del hist[:-64]

    def fit_canvas(self):
        if not self.info:
            return
        z = self.zoom.group.checkedId()
        if z > 0:
            self.canvas.set_scale(z)
            return
        vp = self.canvas_scroll.viewport().size()
        self.canvas.set_scale(max(1, min((vp.width() - 4) // self.info.width, (vp.height() - 4) // self.info.height)))

    def on_bounds(self, on):
        self.canvas.show_bounds = on
        self.canvas.update()

    def on_view(self, i):
        self.stack.setCurrentIndex(i)
        if i == 0:
            QTimer.singleShot(0, self.fit_canvas)

    def on_cursor(self, x, y):
        self.cursor = (x, y)
        self.refresh_status()

    # ---- selection

    def current(self):
        i = self.sel[-1] if self.sel else -1
        return self.hud.widgets[i] if 0 <= i < len(self.hud.widgets) else None

    def selected(self):
        return [self.hud.widgets[i] for i in self.sel if 0 <= i < len(self.hud.widgets)]

    def set_selection(self, sel, from_canvas=False):
        self.sel = [i for i in sel if 0 <= i < len(self.hud.widgets)]
        if not from_canvas:
            self.canvas.sel = list(self.sel)
            self.canvas.update()
        self.sync_list_selection()
        self.refresh_props()
        self.refresh_buttons()
        self.refresh_status()

    def select_all(self):
        self.set_selection(list(range(len(self.hud.widgets))))

    def row_of(self, i):
        return len(self.hud.widgets) - 1 - i

    def sync_list_selection(self):
        self.updating = True
        self.list.clearSelection()
        for i in self.sel:
            item = self.list.item(self.row_of(i))
            if item is not None:
                item.setSelected(True)
        if self.sel:
            self.list.setCurrentRow(self.row_of(self.sel[-1]), QItemSelectionModel.NoUpdate)
            self.list.scrollToItem(self.list.item(self.row_of(self.sel[-1])))
        self.updating = False

    def on_list_selection(self):
        if self.updating:
            return
        rows = [self.list.row(it) for it in self.list.selectedItems()]
        sel = [self.row_of(r) for r in sorted(rows, reverse=True)]
        cur = self.list.currentRow()
        if cur >= 0 and self.row_of(cur) in sel:
            sel.remove(self.row_of(cur))
            sel.append(self.row_of(cur))
        self.sel = sel
        self.canvas.sel = list(sel)
        self.canvas.update()
        self.refresh_props()
        self.refresh_buttons()
        self.refresh_status()

    def on_rows_moved(self):
        if self.updating or self.list.count() != len(self.hud.widgets):
            return  # mid-drop: the item is removed but not inserted yet
        order = [self.list.item(r).data(Qt.UserRole) for r in range(self.list.count())][::-1]
        if order == list(range(len(order))) or sorted(order) != list(range(len(order))):
            return
        self.snapshot()
        picked = [self.hud.widgets[i] for i in self.sel]
        self.hud.widgets = [self.hud.widgets[i] for i in order]
        self.sel = [next(k for k, x in enumerate(self.hud.widgets) if x is w) for w in picked]
        self.changed()

    def on_activate(self, i):
        self.set_selection([i])
        if self.hud.widgets[i].type in (W_TEXT, W_INDICATOR):
            self.label_edit.setFocus()
            self.label_edit.selectAll()

    # ---- editing

    def snapshot(self):
        self.undo.append(self.hud.copy())
        del self.undo[:-UNDO_DEPTH]
        self.redo = []

    def changed(self, refresh_props=True):
        self.push_timer.start(PUSH_DELAY_MS)
        self.refresh_list()
        if refresh_props:
            self.refresh_props()
        self.refresh_state()
        self.refresh_buttons()
        self.redraw()
        self.refresh_status()

    def refresh_all(self):
        self.refresh_list()
        self.refresh_props()
        self.refresh_state()
        self.refresh_buttons()
        self.redraw()
        self.refresh_status()

    def edit(self, **kw):
        targets = self.selected()
        if not targets or self.updating:
            return
        if all(getattr(w, k) == v for w in targets for k, v in kw.items()):
            return
        self.snapshot()
        for w in targets:
            for k, v in kw.items():
                setattr(w, k, v)
        self.changed(refresh_props="source" in kw)

    def edit_flags(self, mask, value):
        targets = self.selected()
        if not targets or self.updating:
            return
        if all(w.flags & mask == value for w in targets):
            return
        self.snapshot()
        for w in targets:
            w.flags = (w.flags & ~mask) | value
        self.changed(refresh_props=False)

    def source_dec(self, wd):
        if S_HOST <= wd.source < S_HOST + hud_agent.HOST_VALUES:
            return int(self.cfg["values"][wd.source - S_HOST].get("dec", 0))
        for s in self.sources:
            if s.id == wd.source:
                return s.dec or 0
        return 0

    def edit_scaled(self, key, v):
        wd = self.current()
        if wd is not None:
            self.edit(**{key: int(round(v * 10 ** self.source_dec(wd)))})

    def on_type(self):
        wd = self.current()
        t = self.type_combo.currentData()
        if self.updating or wd is None or t is None or t == wd.type:
            return
        self.snapshot()
        wd.type = t
        for k, v in NEW_WIDGET.get(t, {}).items():
            if k not in ("w", "h", "source", "label"):
                setattr(wd, k, v)
        self.changed()

    def on_source(self):
        if self.updating:
            return
        s = self.source_combo.currentData()
        wd = self.current()
        if wd is None or s is None or s == wd.source:
            return
        kw = {"source": s}
        if S_HOST <= s < S_HOST + hud_agent.HOST_VALUES and wd.type in (W_BAR, W_GRAPH) and wd.hi <= wd.lo:
            m = hud_agent.METRIC.get(self.cfg["values"][s - S_HOST]["metric"])
            if m is not None:
                d = 10 ** int(self.cfg["values"][s - S_HOST].get("dec", 0))
                kw.update(lo=int(m.lo * d), hi=int(m.hi * d))
        self.edit(**kw)

    def insert_value(self):
        if "{}" not in self.label_edit.text():
            self.label_edit.insert("{}")
            self.edit(label=self.label_edit.text())
        self.label_edit.setFocus()

    def free_spot(self, w, h):
        x, y = (self.info.width - w) // 2, (self.info.height - h) // 2
        taken = {(wd.x, wd.y) for wd in self.hud.widgets}
        while (x, y) in taken and x + 8 < self.info.width and y + 8 < self.info.height:
            x, y = x + 8, y + 8
        return max(0, x), max(0, y)

    def on_add(self, t):
        if not self.info or len(self.hud.widgets) >= self.info.max_widgets:
            return
        self.snapshot()
        wd = Widget.new(t, 0, 0)
        if t == W_TEXT:
            wd.source = S_LAYER
        elif t == W_INDICATOR:
            wd.source = self.sources[0].id if self.sources else S_NONE
            wd.label = None
        elif t in (W_BAR, W_GRAPH):
            wd.source = S_HOST
            m = hud_agent.METRIC.get(self.cfg["values"][0]["metric"])
            if m is not None:
                d = 10 ** int(self.cfg["values"][0].get("dec", 0))
                wd.lo, wd.hi = int(m.lo * d), int(m.hi * d)
        elif t == W_TEXTBOX:
            wd.source = S_HOST_TXT
        f = self.fonts[wd.font] if wd.font < len(self.fonts) and self.fonts[wd.font] else None
        wd.x, wd.y = self.free_spot(wd.w or 80, wd.h or (f.line_height if f else 14))
        self.hud.widgets.append(wd)
        self.sel = [len(self.hud.widgets) - 1]
        self.changed()
        self.sync_list_selection()
        self.canvas.setFocus()

    def on_duplicate(self):
        if not self.sel or len(self.hud.widgets) + len(self.sel) > self.info.max_widgets:
            return
        self.snapshot()
        new = []
        for i in sorted(self.sel):
            c = self.hud.widgets[i].copy()
            c.x = min(self.info.width - 1, c.x + 6)
            c.y = min(self.info.height - 1, c.y + 6)
            self.hud.widgets.append(c)
            new.append(len(self.hud.widgets) - 1)
        self.sel = new
        self.changed()
        self.sync_list_selection()

    def on_copy(self):
        ws = [self.hud.widgets[i].to_json() for i in sorted(self.sel)]
        if ws:
            QApplication.clipboard().setText(json.dumps({"vial-armed-hud-widgets": ws}))

    def on_paste(self):
        try:
            data = json.loads(QApplication.clipboard().text())
            ws = [Widget.from_json(d) for d in data["vial-armed-hud-widgets"]]
        except Exception:
            return
        if not ws or len(self.hud.widgets) + len(ws) > self.info.max_widgets:
            return
        self.snapshot()
        start = len(self.hud.widgets)
        for w in ws:
            w.x = min(self.info.width - 1, w.x + 8)
            w.y = min(self.info.height - 1, w.y + 8)
        self.hud.widgets += ws
        self.sel = list(range(start, len(self.hud.widgets)))
        self.changed()
        self.sync_list_selection()

    def on_order(self, d):
        """ +1: one step towards the front (drawn later), -1: towards the back; ±99: all the way """
        if not self.sel:
            return
        n = len(self.hud.widgets)
        picked = [self.hud.widgets[i] for i in self.sel]
        order = list(range(n))
        moving = sorted(self.sel, reverse=d > 0)
        changed = False
        for i in moving:
            pos = order.index(i)
            target = max(0, min(n - 1, pos + d))
            step = 1 if target > pos else -1
            while pos != target and order[pos + step] not in self.sel:
                order[pos], order[pos + step] = order[pos + step], order[pos]
                pos += step
                changed = True
        if not changed:
            return
        self.snapshot()
        self.hud.widgets = [self.hud.widgets[i] for i in order]
        self.sel = [next(k for k, x in enumerate(self.hud.widgets) if x is w) for w in picked]
        self.changed()
        self.sync_list_selection()

    def on_delete(self):
        if not self.sel:
            return
        self.snapshot()
        for i in sorted(self.sel, reverse=True):
            del self.hud.widgets[i]
        self.sel = []
        self.changed()

    def on_nudge(self, dx, dy):
        if not self.sel:
            return
        self.snapshot()
        for w in self.selected():
            w.x = max(0, min(self.info.width - 1, w.x + dx))
            w.y = max(0, min(self.info.height - 1, w.y + dy))
        self.changed()

    # ---- arrange

    def boxes(self):
        out = []
        for i in self.sel:
            a = self.areas[i] if i < len(self.areas) else None
            if a:
                out.append((self.hud.widgets[i], a))
        return out

    def align(self, how):
        items = self.boxes()
        if not items:
            return
        if len(items) == 1:
            ref = (0, 0, self.info.width - 1, self.info.height - 1)
        else:
            ref = (min(a[0] for _, a in items), min(a[1] for _, a in items),
                   max(a[2] for _, a in items), max(a[3] for _, a in items))
        self.snapshot()
        for w, a in items:
            bw, bh = a[2] - a[0], a[3] - a[1]
            if how == "left":
                w.x = ref[0]
            elif how == "hcenter":
                w.x = (ref[0] + ref[2] - bw + 1) // 2
            elif how == "right":
                w.x = ref[2] - bw
            elif how == "top":
                w.y = ref[1]
            elif how == "vcenter":
                w.y = (ref[1] + ref[3] - bh + 1) // 2
            elif how == "bottom":
                w.y = ref[3] - bh
            w.x = max(0, w.x)
            w.y = max(0, w.y)
        self.changed()

    def distribute(self, axis):
        items = self.boxes()
        if len(items) < 3:
            return
        lo, hi = (0, 2) if axis == "x" else (1, 3)
        items.sort(key=lambda it: it[1][lo])
        start, end = items[0][1][lo], max(a[hi] for _, a in items) + 1
        total = sum(a[hi] - a[lo] + 1 for _, a in items)
        gap = (end - start - total) / (len(items) - 1)
        self.snapshot()
        pos = start
        for w, a in items:
            setattr(w, axis, int(round(pos)))
            pos += a[hi] - a[lo] + 1 + gap
        self.changed()

    def match(self, dim):
        items = self.boxes()
        if len(items) < 2:
            return
        ref = items[-1][1]  # the primary selection is last
        size = ref[2] - ref[0] + 1 if dim == "w" else ref[3] - ref[1] + 1
        self.edit(**{dim: size})

    def on_context(self, pos):
        m = QMenu()
        has = bool(self.sel)
        for text, fn, ok in ((tr("ArmedHud", "Duplicate\tCtrl+D"), self.on_duplicate, has),
                             (tr("ArmedHud", "Copy\tCtrl+C"), self.on_copy, has),
                             (tr("ArmedHud", "Paste\tCtrl+V"), self.on_paste, True),
                             (tr("ArmedHud", "Delete\tDel"), self.on_delete, has)):
            a = m.addAction(text, fn)
            a.setEnabled(ok)
        m.addSeparator()
        for text, d in ((tr("ArmedHud", "Bring to front"), 99), (tr("ArmedHud", "Bring forward\tCtrl+]"), 1),
                        (tr("ArmedHud", "Send backward\tCtrl+["), -1), (tr("ArmedHud", "Send to back"), -99)):
            m.addAction(text, lambda d=d: self.on_order(d)).setEnabled(has)
        m.addSeparator()
        al = m.addMenu(tr("ArmedHud", "Align"))
        al.setEnabled(has)
        for text, how in (("Left", "left"), ("Centre", "hcenter"), ("Right", "right"), ("Top", "top"),
                          ("Middle", "vcenter"), ("Bottom", "bottom")):
            al.addAction(tr("ArmedHud", text), lambda how=how: self.align(how))
        m.addAction(tr("ArmedHud", "Select all\tCtrl+A"), self.select_all)
        m.exec_(pos)

    def on_undo(self):
        if self.undo:
            self.redo.append(self.hud.copy())
            self.hud = self.undo.pop()
            self.sel = [i for i in self.sel if i < len(self.hud.widgets)]
            self.changed()

    def on_redo(self):
        if self.redo:
            self.undo.append(self.hud.copy())
            self.hud = self.redo.pop()
            self.sel = [i for i in self.sel if i < len(self.hud.widgets)]
            self.changed()

    def on_preset(self, fn):
        self.push_timer.stop()
        self.snapshot()
        if fn is None:
            try:
                self.io.hud_revert(factory=True)
                self.sent = self.io.hud_read(self.info)
                self.hud = Layout.unpack(self.sent, self.info)
            except Exception as e:
                self.lbl_error.setText("ERROR: {}".format(e))
                return
        else:
            self.hud = fn()
        self.sel = []
        self.changed()

    def on_import(self):
        path, _ = QFileDialog.getOpenFileName(None, tr("ArmedHud", "Import HUD layout"), "", "HUD layout (*.json)")
        if not path:
            return
        try:
            with open(path) as f:
                layout = Layout.from_json(f.read())
            layout.pack(self.info)
        except Exception as e:
            QMessageBox.warning(None, "", tr("ArmedHud", "Not a HUD layout this keyboard can show: {}").format(e))
            return
        self.snapshot()
        self.hud = layout
        self.sel = []
        self.changed()

    def on_export(self):
        path, _ = QFileDialog.getSaveFileName(None, tr("ArmedHud", "Export HUD layout"), "hud.json", "HUD layout (*.json)")
        if path:
            with open(path, "w") as f:
                f.write(self.hud.to_json())

    def push(self):
        if not self.loaded or self.io is None:
            return
        try:
            blob = self.hud.pack(self.info)
        except LayoutError as e:
            self.lbl_error.setText("ERROR: {}".format(e).upper())
            return
        if blob == self.sent:
            return
        try:
            self.io.hud_write(blob, self.sent)
            self.sent = blob
            self.lbl_error.setText("")
        except Exception as e:
            self.sent = None  # unknown now: send everything next time
            self.lbl_error.setText("ERROR: {}".format(e))

    def on_save(self):
        if not self.loaded:
            return
        self.push_timer.stop()
        self.push()
        try:
            self.io.hud_store()
            self.saved = self.hud.copy()
        except Exception as e:
            self.lbl_error.setText("ERROR: {}".format(e))
        self.refresh_state()

    def on_revert(self):
        self.push_timer.stop()
        try:
            self.io.hud_revert()
            self.sent = self.io.hud_read(self.info)
            self.hud = Layout.unpack(self.sent, self.info)
        except Exception as e:
            self.lbl_error.setText("ERROR: {}".format(e))
            return
        self.saved = self.hud.copy()
        self.undo, self.redo = [], []
        self.sel = [i for i in self.sel if i < len(self.hud.widgets)]
        self.refresh_all()

    # ---- panels

    def summary(self, wd):
        name = TYPE_NAMES.get(wd.type, "?")
        if wd.type == W_FRAME:
            what = FRAME_NAMES[wd.param] if wd.param < len(FRAME_NAMES) else ""
        else:
            what = self.source_name(wd.source) if wd.source else ""
            if wd.type in (W_TEXT, W_INDICATOR) and wd.label and wd.label != "{}":
                what = '"{}"'.format(wd.label) + ("  · " + what if what else "")
        return "{:<10} {}".format(name, what)

    def source_name(self, s):
        if S_HOST <= s < S_HOST + hud_agent.HOST_VALUES:
            return "H{} · {}".format(s - S_HOST + 1, hud_agent.slot_name(self.cfg, s - S_HOST))
        if S_HOST_TXT <= s < S_HOST_TXT + hud_agent.HOST_TEXTS:
            return "T{} · {}".format(s - S_HOST_TXT + 1, hud_agent.text_name(self.cfg, s - S_HOST_TXT))
        for src in self.sources:
            if src.id == s:
                return src.name
        return "#{}".format(s)

    def refresh_list(self):
        self.updating = True
        self.list.clear()
        n = len(self.hud.widgets)
        for i in range(n - 1, -1, -1):
            wd = self.hud.widgets[i]
            item = QListWidgetItem(self.summary(wd))
            item.setData(Qt.UserRole, i)
            if i < len(self.areas) and self.areas[i] is None:
                item.setForeground(QColor(armed.WARN))
                item.setToolTip(tr("ArmedHud", "Off the screen"))
            self.list.addItem(item)
        self.updating = False
        self.sync_list_selection()
        self.lbl_count.setText("{} / {}".format(n, self.info.max_widgets if self.info else 0))

    def refresh_buttons(self):
        n = len(self.hud.widgets)
        full = self.info is None or n >= self.info.max_widgets
        for b in self.add_buttons:
            b.setEnabled(self.loaded and not full)
        has = bool(self.sel)
        for b in (self.btn_dup, self.btn_up, self.btn_down, self.btn_del):
            b.setEnabled(has)
        k = len(self.sel)
        for name, b in self.arrange_buttons:
            b.setEnabled(k >= 3 if name.startswith("dist") else k >= 2 if name.startswith("same") else k >= 1)

    def refresh_status(self):
        parts = []
        if self.cursor[0] >= 0:
            parts.append("{}, {}".format(*self.cursor))
        wd = self.current()
        if len(self.sel) == 1 and wd is not None:
            a = self.areas[self.sel[0]] if self.sel[0] < len(self.areas) else None
            size = "{} × {}".format(a[2] - a[0] + 1, a[3] - a[1] + 1) if a else tr("ArmedHud", "OFF SCREEN")
            parts.append("{} · X {} Y {} · {}".format(TYPE_NAMES.get(wd.type, "?"), wd.x, wd.y, size))
        elif self.sel:
            parts.append(tr("ArmedHud", "{} SELECTED").format(len(self.sel)))
        hint = tr("ArmedHud", "Shift / Ctrl + click or drag a box: several · arrows nudge, Shift: 8 px · Alt: no snapping "
                              "· right click: more")
        self.lbl_status.setText("   ·   ".join(parts + [hint]) if parts else hint)

    def refresh_state(self):
        dirty = self.saved is not None and self.hud != self.saved
        self.lbl_state.setText('<span style="color:{}">■</span> {}'.format(
            armed.ACC if dirty else armed.OK,
            tr("ArmedHud", "ON THE KEYBOARD, NOT SAVED") if dirty else tr("ArmedHud", "SAVED")))
        self.lbl_state.setToolTip(tr("ArmedHud", "The keyboard already shows your changes; SAVE keeps them "
                                                 "after it is unplugged") if dirty else "")
        self.btn_save.setEnabled(dirty)
        self.btn_revert.setEnabled(dirty)
        self.btn_undo.setEnabled(bool(self.undo))
        self.btn_redo.setEnabled(bool(self.redo))

    def show_rows(self, keys):
        for k, (lbl, w) in self.rows.items():
            lbl.setVisible(k in keys)
            w.setVisible(k in keys)
        for title, ks in self.sections:
            title.setVisible(any(k in keys for k in ks))

    @staticmethod
    def common(ws, attr):
        vals = {getattr(w, attr) for w in ws}
        return vals.pop() if len(vals) == 1 else None

    def refresh_props(self):
        ws = self.selected()
        wd = self.current()
        self.props.setEnabled(bool(ws))
        if not ws:
            self.show_rows(())
            self.lbl_sel.show()
            self.lbl_sel.setText(tr("ArmedHud", "Nothing selected. Click a widget on the screen or in the list, or add "
                                                "one with the buttons above the screen."))
            self.lbl_hint.setText("")
            return
        self.updating = True
        types = {w.type for w in ws}
        if len(ws) > 1:
            self.lbl_sel.setText(tr("ArmedHud", "{} widgets: changes apply to all of them.").format(len(ws)))
            self.lbl_sel.show()
            keys = {"fg", "bg"}
            if types <= {W_TEXT, W_INDICATOR, W_TEXTBOX}:
                keys |= {"font"}
            if types <= {W_TEXT, W_TEXTBOX}:
                keys |= {"align"}
        else:
            self.lbl_sel.hide()
            t = wd.type
            keys = {"type", "geo", "fg"}
            if t != W_FRAME:
                keys |= {"source"}
            if t in (W_TEXT, W_INDICATOR):
                keys |= {"label"}
            if t in (W_TEXT, W_INDICATOR, W_TEXTBOX):
                keys |= {"font"}
            if t in (W_TEXT, W_TEXTBOX, W_BAR, W_GRAPH, W_INDICATOR):
                keys |= {"bg"}
            if t in (W_TEXT, W_TEXTBOX):
                keys |= {"align"}
            if t == W_TEXT:
                keys |= {"digits", "warn"}
            if t in (W_BAR, W_GRAPH):
                keys |= {"range", "warn"}
            if t == W_BAR:
                keys |= {"segments"}
            if t == W_GRAPH:
                keys |= {"period"}
            if t == W_INDICATOR:
                keys |= {"dot"}
            if t == W_FRAME:
                keys |= {"frame"}
            if t == W_TEXTBOX:
                keys |= {"lines"}
        self.show_rows(keys)

        fg, bg, font = self.common(ws, "fg"), self.common(ws, "bg"), self.common(ws, "font")
        self.fg.set_current(fg)
        self.bg.set_current(bg)
        self.font_combo.setCurrentIndex(font if font is not None and font < self.font_combo.count() else -1)
        al = {w.flags & F_ALIGN for w in ws}
        self.align_seg.set_current(al.pop() if len(al) == 1 else -1)
        if len(ws) == 1:
            self.fill_single(wd)
        self.updating = False

    def fill_single(self, wd):
        t = wd.type
        self.type_combo.setCurrentIndex(self.type_combo.findData(t))
        self.source_combo.clear()
        self.source_combo.addItem("—", S_NONE)
        model = self.source_combo.model()

        def group(title):
            self.source_combo.addItem(title, None)
            model.item(self.source_combo.count() - 1).setEnabled(False)

        kb = [s for s in self.sources if s.id < S_HOST and self.fits(t, s)]
        host = [s for s in self.sources if S_HOST <= s.id < S_HOST_TXT and self.fits(t, s)]
        txt = [s for s in self.sources if s.id >= S_HOST_TXT and self.fits(t, s)]
        for title, items in ((tr("ArmedHud", "— KEYBOARD"), kb), (tr("ArmedHud", "— THIS COMPUTER: NUMBERS"), host),
                             (tr("ArmedHud", "— THIS COMPUTER: TEXT"), txt)):
            if items:
                group(title)
                for s in items:
                    self.source_combo.addItem("   " + self.source_name(s.id), s.id)
        idx = self.source_combo.findData(wd.source)
        if idx < 0:
            self.source_combo.addItem(self.source_name(wd.source), wd.source)
            idx = self.source_combo.count() - 1
        self.source_combo.setCurrentIndex(idx)
        self.lbl_now.setVisible(bool(wd.source))
        self.label_edit.setText(wd.label or "")
        self.label_edit.setPlaceholderText(tr("ArmedHud", "e.g. CPU {}%  ({} is the value)") if t == W_TEXT
                                           else tr("ArmedHud", "the source's name"))
        self.btn_value.setVisible(t == W_TEXT)
        for k, sp in self.spin.items():
            sp.setValue(getattr(wd, k))
        dec = self.source_dec(wd)
        for sp, k in ((self.lo, "lo"), (self.hi, "hi"), (self.warn, "warn")):
            sp.setDecimals(dec)
            sp.setValue(getattr(wd, k) / 10 ** dec)
        self.chk_warn.setChecked(bool(wd.flags & F_WARN))
        self.warn.setEnabled(bool(wd.flags & F_WARN))
        self.below.setEnabled(bool(wd.flags & F_WARN))
        self.below.setCurrentIndex(1 if wd.flags & F_WARN_BELOW else 0)
        self.digits.setValue(wd.param & 0x0F)
        self.segments.setValue(wd.param)
        self.period.setValue((wd.param or 10) / 10)
        self.chk_fill.setChecked(bool(wd.flags & F_FILL))
        self.chk_dot.setChecked(bool(wd.flags & F_DOT))
        self.frame_style.setCurrentIndex(min(wd.param, len(FRAME_NAMES) - 1))
        self.corner.setValue(wd.lo)
        self.corner.setEnabled(wd.param == FRAME_BRACKETS)
        self.gap.setValue(wd.param)
        self.chk_tail.setChecked(bool(wd.flags & F_TAIL))
        hints = {
            W_TEXT: tr("ArmedHud", "No source: a fixed label. AUTO colour follows the source: accent above the base "
                                   "layer, green while charging, the link state's colour."),
            W_BAR: tr("ArmedHud", "Fills from the left between FROM and TO."),
            W_GRAPH: tr("ArmedHud", "Newest sample on the right; the keyboard keeps 64. {} graphs at most.").format(
                self.info.graphs if self.info else 4),
            W_INDICATOR: tr("ArmedHud", "Lit while the source is on. DOT ONLY with AUTO colour shows the source's "
                                        "colour, like the link dot."),
            W_BATTERY: tr("ArmedHud", "Always 25 × 11 px."),
            W_FRAME: tr("ArmedHud", "Redrawn whenever a widget over it changes."),
            W_TEXTBOX: tr("ArmedHud", "Wrapped between words. The keyboard's fonts have no Cyrillic: the agent spells "
                                      "it in Latin letters."),
        }
        self.lbl_hint.setText(hints.get(t, ""))

    @staticmethod
    def fits(t, s):
        if t in (W_BAR, W_GRAPH):
            return s.kind == K_NUMBER
        if t == W_TEXTBOX:
            return s.kind in (K_LINES, K_TEXT)
        if t == W_BATTERY:
            return s.kind == K_NUMBER
        return True

    # ---- agent

    def load_agent_rows(self):
        self.updating = True
        for i, (metric, cmd, interval, dec, live, used) in enumerate(self.value_rows):
            s = self.cfg["values"][i]
            metric.setCurrentIndex(max(0, metric.findData(s["metric"])))
            cmd.setText(s["command"])
            interval.setValue(s["interval"])
            dec.setValue(int(s.get("dec", 0)))
            is_cmd = s["metric"] == "command"
            cmd.setEnabled(is_cmd)
            interval.setEnabled(is_cmd)
            dec.setEnabled(bool(s["metric"]))
        for i, (cmd, interval, live, used) in enumerate(self.text_rows):
            s = self.cfg["texts"][i]
            cmd.setText(s["command"])
            interval.setValue(s["interval"])
        self.updating = False

    def on_value_cfg(self, i, what):
        if self.updating:
            return
        metric, cmd, interval, dec, live, used = self.value_rows[i]
        s = self.cfg["values"][i]
        old = s["metric"]
        s["metric"] = metric.currentData()
        s["command"] = cmd.text()
        s["interval"] = interval.value()
        if what == "metric" and s["metric"] in hud_agent.METRIC and s["metric"] != old:
            s["dec"] = hud_agent.METRIC[s["metric"]].dec
        else:
            s["dec"] = dec.value()
        self.load_agent_rows()
        if what == "metric" and s["metric"] == "command":
            cmd.setFocus()
        self.cfg_timer.start(CONFIG_SAVE_MS)

    def on_text_cfg(self, i):
        if self.updating:
            return
        cmd, interval, live, used = self.text_rows[i]
        self.cfg["texts"][i]["command"] = cmd.text()
        self.cfg["texts"][i]["interval"] = interval.value()
        self.cfg_timer.start(CONFIG_SAVE_MS)

    def set_text_command(self, i, command):
        self.text_rows[i][0].setText(command)
        self.on_text_cfg(i)

    def save_config(self):
        try:
            hud_agent.save_config(self.cfg)
        except OSError as e:
            self.lbl_error.setText("ERROR: {}".format(e))
        self.host.agent.reload(self.cfg)
        if self.loaded:
            self.refresh_list()
            self.refresh_props()

    def refresh_agent(self):
        color, text = self.host.status()
        self.lbl_agent.setText('<span style="color:{}">■</span> {}'.format(color, text))
        was, self.updating = self.updating, True
        self.chk_autostart.setChecked(os.path.exists(hud_agent.autostart_path()))
        self.updating = was
        uses = {}
        for w in self.hud.widgets:
            uses[w.source] = uses.get(w.source, 0) + 1
        for i, row in enumerate(self.value_rows):
            n = uses.get(S_HOST + i, 0)
            row[5].setText(tr("ArmedHud", "{} WIDGETS").format(n) if n else "—")
        for i, row in enumerate(self.text_rows):
            n = uses.get(S_HOST_TXT + i, 0)
            row[3].setText(tr("ArmedHud", "{} WIDGETS").format(n) if n else "—")

    def on_autostart(self, on):
        if self.updating:
            return
        try:
            hud_agent.set_autostart(on)
        except Exception as e:
            QMessageBox.warning(None, "", str(e))
        self.refresh_agent()
