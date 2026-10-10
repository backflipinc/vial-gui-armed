# SPDX-License-Identifier: GPL-2.0-or-later
# Device page for qmk-armed keyboards with auto-sleep

from PyQt5.QtWidgets import QHBoxLayout, QLabel, QGridLayout

from editor.basic_editor import BasicEditor
from protocol.qmk_armed import V_SLEEP, F_SLEEP
from vial_device import VialKeyboard
from widgets.armed_controls import BracketFrame, Segmented
from util import tr


class ArmedDevice(BasicEditor):

    def __init__(self):
        super().__init__()
        self.io = None
        self.sleep_order = []

        power = BracketFrame("POWER")

        row = QHBoxLayout()
        lbl = QLabel(tr("ArmedDevice", "AUTO-SLEEP"))
        lbl.setObjectName("armedKey")
        lbl.setFixedWidth(110)
        self.sleep = Segmented()
        self.sleep.changed.connect(self.on_sleep)
        row.addWidget(lbl)
        row.addWidget(self.sleep)
        row.addStretch(1)
        power.box.addLayout(row)

        note = QLabel(tr("ArmedDevice", "Sleeps after this much idle time, a key press wakes it."))
        note.setObjectName("armedHint")
        power.box.addWidget(note)
        power.box.addStretch(1)

        grid = QGridLayout()
        grid.setSpacing(18)
        grid.addWidget(power, 0, 0)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(2, 1)
        self.addLayout(grid)
        self.addStretch(1)

    def valid(self):
        if not isinstance(self.device, VialKeyboard):
            return False
        armed = self.device.keyboard.armed
        return armed is not None and armed.has(F_SLEEP)

    def rebuild(self, device):
        super().rebuild(device)
        self.io = device.keyboard.armed if self.valid() else None
        if self.io is None:
            return
        minutes = self.io.sleep_minutes
        # firmware order is the Fn+S cycle; show ascending with OFF last
        self.sleep_order = sorted(range(len(minutes)), key=lambda i: minutes[i] or 10 ** 6)
        self.sleep.set_labels(["{}M".format(minutes[i]) if minutes[i] else "OFF" for i in self.sleep_order])
        self.load()

    def load(self):
        idx = self.io.get(V_SLEEP)
        if idx in self.sleep_order:
            self.sleep.set_current(self.sleep_order.index(idx))

    def activate(self):
        if self.io is not None:
            self.io.reload_values()
            self.load()

    def on_sleep(self, pos):
        self.io.set(V_SLEEP, self.sleep_order[pos])
