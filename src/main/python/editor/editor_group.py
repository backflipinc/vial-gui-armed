# SPDX-License-Identifier: GPL-2.0-or-later
# Several pages under one side menu entry, picked with a switch on top (Lighting: effects / themes, ...)
from PyQt5.QtWidgets import QHBoxLayout, QStackedWidget, QWidget

from editor.basic_editor import BasicEditor
from widgets.armed_controls import Segmented


class EditorGroup(BasicEditor):

    def __init__(self, parts):
        """ parts: [(editor, label)] """
        super().__init__()
        self.parts = parts
        self.shown = []  # indexes of the parts this keyboard has
        self.current = 0
        self.active = False

        self.switch = Segmented()
        self.switch.changed.connect(self.on_switch)
        row = QHBoxLayout()
        row.addWidget(self.switch)
        row.addStretch(1)
        self.stack = QStackedWidget()
        for editor, _ in parts:
            holder = QWidget()
            holder.setLayout(editor)
            self.stack.addWidget(holder)
        self.addLayout(row)
        self.addSpacing(6)
        self.addWidget(self.stack, 1)

    def valid(self):
        return any(editor.valid() for editor, _ in self.parts)

    def rebuild(self, device):
        super().rebuild(device)
        for editor, _ in self.parts:
            editor.rebuild(device)
        self.shown = [i for i, (editor, _) in enumerate(self.parts) if editor.valid()]
        self.switch.set_labels([self.parts[i][1] for i in self.shown])
        self.switch.setVisible(len(self.shown) > 1)
        if self.current not in self.shown and self.shown:
            self.current = self.shown[0]
        self.stack.setCurrentIndex(self.current)
        if self.current in self.shown:
            self.switch.set_current(self.shown.index(self.current))

    def select(self, editor):
        """ shows the part holding `editor` """
        for i, (e, _) in enumerate(self.parts):
            if e is editor and i in self.shown:
                self.on_switch(self.shown.index(i))
                self.switch.set_current(self.shown.index(i))

    def on_switch(self, pos):
        new = self.shown[pos]
        if new == self.current:
            return
        if self.active:
            self.parts[self.current][0].deactivate()
        self.current = new
        self.stack.setCurrentIndex(new)
        if self.active:
            self.parts[new][0].activate()

    def activate(self):
        self.active = True
        self.parts[self.current][0].activate()

    def deactivate(self):
        self.active = False
        self.parts[self.current][0].deactivate()

    def on_container_clicked(self):
        self.parts[self.current][0].on_container_clicked()
