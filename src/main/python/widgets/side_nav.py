# SPDX-License-Identifier: GPL-2.0-or-later

from PyQt5.QtCore import Qt, QSize, QRect
from PyQt5.QtGui import QPainter, QColor
from PyQt5.QtWidgets import QListWidget, QListWidgetItem, QStyledItemDelegate, QStyle

import armed


class NavDelegate(QStyledItemDelegate):

    def paint(self, qp, option, index):
        qp.save()
        selected = option.state & QStyle.State_Selected
        hover = option.state & QStyle.State_MouseOver
        r = option.rect.adjusted(8, 1, -8, -1)

        if selected:
            qp.setPen(QColor(armed.ACC))
            qp.drawRect(r.adjusted(0, 0, -1, -1))
            qp.fillRect(QRect(r.left(), r.top(), 4, r.height()), QColor(armed.ACC))
        elif hover:
            qp.setPen(QColor(armed.LINE2))
            qp.drawRect(r.adjusted(0, 0, -1, -1))

        qp.setFont(armed.mono(12))
        qp.setPen(QColor(armed.ACC if selected else armed.DIM))
        qp.drawText(r.adjusted(18, 0, 0, 0), Qt.AlignVCenter | Qt.AlignLeft, "{:02d}".format(index.row() + 1))

        qp.setFont(armed.head(17))
        qp.setPen(QColor(armed.ACC if selected else armed.TXT))
        qp.drawText(r.adjusted(46, 0, -6, 0), Qt.AlignVCenter | Qt.AlignLeft, index.data().upper())
        qp.restore()

    def sizeHint(self, option, index):
        return QSize(214, 42)


class SideNav(QListWidget):
    """ Numbered vertical navigation that drives a QTabWidget with its tab bar hidden """

    def __init__(self, tabs):
        super().__init__()
        self.tabs = tabs
        self.setObjectName("armedNav")
        self.setItemDelegate(NavDelegate(self))
        self.setFixedWidth(214)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.NoFocus)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.currentRowChanged.connect(self.on_row_changed)
        self.tabs.currentChanged.connect(self.on_tab_changed)

    def sync(self):
        self.blockSignals(True)
        self.clear()
        for i in range(self.tabs.count()):
            self.addItem(QListWidgetItem(self.tabs.tabText(i)))
        self.setCurrentRow(self.tabs.currentIndex())
        self.blockSignals(False)

    def on_row_changed(self, row):
        if row >= 0:
            self.tabs.setCurrentIndex(row)

    def on_tab_changed(self, index):
        self.blockSignals(True)
        self.setCurrentRow(index)
        self.blockSignals(False)
