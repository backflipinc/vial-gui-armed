# SPDX-License-Identifier: GPL-2.0-or-later
# Device page: keyboard options from the on-screen menu that Vial doesn't cover (NKRO, Win lock, SnapTap,
# Bluetooth slot, auto-sleep), shown for what the keyboard reports; display options are under Screen
from PyQt5.QtCore import QTimer
from PyQt5.QtWidgets import QHBoxLayout, QLabel, QGridLayout

from editor.basic_editor import BasicEditor
from hud_layout import S_LINK, S_SNAPTAP
from protocol.qmk_armed import V_SLEEP, V_NKRO, V_GUI_LOCK, V_SNAPTAP, V_BT_SLOT, F_SLEEP, F_KEYMAP, F_SNAPTAP, \
    F_BT_SLOT, F_HUD
from vial_device import VialKeyboard
from widgets.armed_controls import BracketFrame, Segmented
from util import tr

STATUS_MS = 2000


class ArmedDevice(BasicEditor):

    def __init__(self):
        super().__init__()
        self.io = None
        self.sleep_order = []
        self.rows = {}
        self.status_timer = QTimer()
        self.status_timer.timeout.connect(self.refresh_status)

        self.keyboard_frame = BracketFrame("KEYBOARD")
        self.nkro = self.row(self.keyboard_frame, V_NKRO, "NKRO", ["OFF", "ON"],
                             "All keys at once instead of six. Also in the menu: SYSTEM > NKRO.")
        self.gui = self.row(self.keyboard_frame, V_GUI_LOCK, "WIN LOCK", ["OFF", "ON"],
                            "Turns the Win keys off, e.g. for games. Also SYSTEM > WIN LOCK.")
        self.snaptap = self.row(self.keyboard_frame, V_SNAPTAP, "SNAPTAP", [],
                                "When both keys of a pair are held: LAST — the last pressed wins, NEUTRAL — "
                                "neither. The switch on the case turns SnapTap on.")
        self.lbl_snaptap = self.status_label(self.keyboard_frame)

        self.wireless_frame = BracketFrame("WIRELESS")
        self.slot = self.row(self.wireless_frame, V_BT_SLOT, "BT SLOT", ["1", "2", "3", "4", "5"],
                             "Which paired host Bluetooth mode connects to. Pairing: WIRELESS > PAIR NOW in the "
                             "menu, or the pairing keys on the Fn layer.")
        self.lbl_link = self.status_label(self.wireless_frame)

        self.power_frame = BracketFrame("POWER")
        self.sleep = self.row(self.power_frame, V_SLEEP, "AUTO-SLEEP", [],
                              "In BT and 2.4 GHz the keyboard sleeps after this much idle time; a key press wakes it.")

        for f in (self.keyboard_frame, self.wireless_frame, self.power_frame):
            f.box.addStretch(1)
        grid = QGridLayout()
        grid.setSpacing(18)
        grid.addWidget(self.keyboard_frame, 0, 0)
        grid.addWidget(self.wireless_frame, 0, 1)
        grid.addWidget(self.power_frame, 1, 0)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        self.addLayout(grid)
        self.addStretch(1)

    def row(self, frame, vid, label, labels, hint):
        line = QHBoxLayout()
        lbl = QLabel(tr("ArmedDevice", label))
        lbl.setObjectName("armedKey")
        lbl.setFixedWidth(110)
        seg = Segmented(labels)
        seg.changed.connect(lambda i, vid=vid: self.on_value(vid, i))
        line.addWidget(lbl)
        line.addWidget(seg)
        line.addStretch(1)
        frame.box.addLayout(line)
        note = QLabel(tr("ArmedDevice", hint))
        note.setObjectName("armedHint")
        note.setWordWrap(True)
        frame.box.addWidget(note)
        self.rows[vid] = (lbl, seg, note)
        return seg

    def status_label(self, frame):
        lbl = QLabel()
        frame.box.addWidget(lbl)
        return lbl

    # ---- device

    def valid(self):
        if not isinstance(self.device, VialKeyboard):
            return False
        armed = self.device.keyboard.armed
        return armed is not None and any(armed.has(f) for f in (F_SLEEP, F_KEYMAP, F_SNAPTAP, F_BT_SLOT))

    def rebuild(self, device):
        super().rebuild(device)
        self.io = device.keyboard.armed if self.valid() else None
        if self.io is None:
            return
        io = self.io
        minutes = io.sleep_minutes
        # firmware order is the Fn+S cycle; show ascending with OFF last
        self.sleep_order = sorted(range(len(minutes)), key=lambda i: minutes[i] or 10 ** 6)
        self.sleep.set_labels(["{}M".format(minutes[i]) if minutes[i] else "OFF" for i in self.sleep_order])
        self.snaptap.set_labels(io.snaptap_modes)
        self.load()

    def load(self):
        io = self.io
        for vid, (lbl, seg, note) in self.rows.items():
            shown = vid in io.values and bool(seg.group.buttons())
            for w in (lbl, seg, note):
                w.setVisible(shown)
            if not shown:
                continue
            v = io.get(vid)
            if vid == V_SLEEP:
                v = self.sleep_order.index(v) if v in self.sleep_order else -1
            elif vid == V_BT_SLOT:
                v -= 1
            seg.set_current(v)
        self.keyboard_frame.setVisible(any(v in io.values for v in (V_NKRO, V_GUI_LOCK, V_SNAPTAP)))
        self.wireless_frame.setVisible(V_BT_SLOT in io.values)
        self.power_frame.setVisible(V_SLEEP in io.values)
        self.refresh_status()

    def refresh_status(self):
        """ live state from the HUD sources, where the keyboard has them """
        if self.io is None or not self.io.has(F_HUD):
            self.lbl_snaptap.hide()
            self.lbl_link.hide()
            return
        for lbl, source, fmt in ((self.lbl_snaptap, S_SNAPTAP, "SWITCH: {}"), (self.lbl_link, S_LINK, "NOW: {}")):
            try:
                r = self.io.hud_value(source)
            except Exception:
                r = None
            lbl.setVisible(r is not None)
            if r is not None:
                lbl.setText(tr("ArmedDevice", fmt).format(r[4] or ("ON" if r[3] else "OFF")))

    def activate(self):
        if self.io is not None:
            self.io.reload_values()
            self.load()
            self.status_timer.start(STATUS_MS)

    def deactivate(self):
        self.status_timer.stop()

    def on_value(self, vid, pos):
        if vid == V_SLEEP:
            pos = self.sleep_order[pos]
        elif vid == V_BT_SLOT:
            pos += 1
        self.io.set(vid, pos)
        self.refresh_status()
