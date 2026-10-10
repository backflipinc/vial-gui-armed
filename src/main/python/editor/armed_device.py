# SPDX-License-Identifier: GPL-2.0-or-later
# Device page for keyboards with the IO84 channel

from PyQt5.QtWidgets import QHBoxLayout, QLabel, QGridLayout

from editor.basic_editor import BasicEditor
from protocol.io84 import V_SLEEP
from vial_device import VialKeyboard
from widgets.armed_controls import BracketFrame, Segmented
from util import tr


class ArmedDevice(BasicEditor):

    def __init__(self):
        super().__init__()
        self.io = None
        self.sleep_order = []

        power = BracketFrame("POWER")
        hint = QLabel(tr("ArmedDevice", "WIRELESS ONLY"))
        hint.setObjectName("armedHint")
        power.header.addWidget(hint)

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

        note = QLabel(tr("ArmedDevice", "Deep sleep in Bluetooth / 2.4 GHz after this much idle time.\n"
                                        "Any key, the knob or a switch wakes it."))
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
        return isinstance(self.device, VialKeyboard) and self.device.keyboard.io84 is not None

    def rebuild(self, device):
        super().rebuild(device)
        self.io = device.keyboard.io84 if self.valid() else None
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
