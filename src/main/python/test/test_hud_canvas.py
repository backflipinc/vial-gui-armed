import unittest

from PyQt5.QtCore import Qt, QPoint, QEvent
from PyQt5.QtGui import QImage, QMouseEvent
from PyQt5.QtTest import QTest
from PyQt5.QtWidgets import QApplication

from hud_layout import Layout, Widget, W_TEXT, W_BAR
from widgets.hud_canvas import HudCanvas

app = QApplication.instance() or QApplication(["test", "-platform", "offscreen"])


class TestHudCanvas(unittest.TestCase):

    def setUp(self):
        self.hud = Layout([Widget(W_TEXT, x=10, y=10, w=40, h=14), Widget(W_BAR, x=100, y=50, w=60, h=12)])
        self.c = HudCanvas()
        self.c.set_scale(2)
        self.edits = 0
        self.c.edited.connect(self.refresh)
        self.refresh()
        self.c.show()

    def refresh(self):
        self.edits += 1
        areas = [(w.x, w.y, w.x + w.w - 1, w.y + w.h - 1) for w in self.hud.widgets]
        self.c.set_content(QImage(320, 172, QImage.Format_RGB888), self.hud, areas)

    def at(self, x, y):
        return QPoint(1 + x * 2 + 1, 1 + y * 2 + 1)

    def move(self, pos, mods=Qt.NoModifier):
        # QTest.mouseMove doesn't reach widgets on the offscreen platform
        app.sendEvent(self.c, QMouseEvent(QEvent.MouseMove, pos, Qt.NoButton, Qt.LeftButton, mods))

    def paint(self):
        self.c.grab()  # runs paintEvent

    def test_click_empty_and_box_select(self):
        QTest.mousePress(self.c, Qt.LeftButton, Qt.NoModifier, self.at(200, 150))
        self.assertEqual(self.c.sel, [])
        self.paint()
        self.move(self.at(5, 5))
        self.paint()
        self.assertEqual(sorted(self.c.sel), [0, 1])
        QTest.mouseRelease(self.c, Qt.LeftButton, Qt.NoModifier, self.at(5, 5))
        self.paint()

    def test_select_move_snap_resize(self):
        QTest.mouseClick(self.c, Qt.LeftButton, Qt.NoModifier, self.at(20, 15))
        self.assertEqual(self.c.sel, [0])
        QTest.mousePress(self.c, Qt.LeftButton, Qt.NoModifier, self.at(20, 15))
        self.move(self.at(108, 17))  # left edge lands 2 px off the bar's: snaps
        self.paint()
        QTest.mouseRelease(self.c, Qt.LeftButton, Qt.NoModifier, self.at(108, 17))
        self.assertEqual(self.hud.widgets[0].x, 100)
        a = self.c.areas[0]
        QTest.mousePress(self.c, Qt.LeftButton, Qt.NoModifier, self.at(a[2], a[3]))
        self.move(self.at(a[2] + 20, a[3] + 5))
        QTest.mouseRelease(self.c, Qt.LeftButton, Qt.NoModifier, self.at(a[2] + 20, a[3] + 5))
        self.assertGreater(self.hud.widgets[0].w, 40)
        self.paint()

    def test_modifiers_keys_context(self):
        QTest.mouseClick(self.c, Qt.LeftButton, Qt.NoModifier, self.at(20, 15))
        QTest.mouseClick(self.c, Qt.LeftButton, Qt.ShiftModifier, self.at(110, 55))
        self.assertEqual(self.c.sel, [0, 1])
        QTest.mouseClick(self.c, Qt.LeftButton, Qt.ControlModifier, self.at(20, 15))
        self.assertEqual(self.c.sel, [1])
        nudges = []
        self.c.nudge.connect(lambda dx, dy: nudges.append((dx, dy)))
        QTest.keyClick(self.c, Qt.Key_Left, Qt.ShiftModifier)
        self.assertEqual(nudges, [(-8, 0)])
        menus = []
        self.c.context_requested.connect(menus.append)
        QTest.mouseClick(self.c, Qt.RightButton, Qt.NoModifier, self.at(20, 15))
        self.assertEqual((self.c.sel, len(menus)), ([0], 1))
        hits = []
        self.c.activated.connect(hits.append)
        QTest.mouseDClick(self.c, Qt.LeftButton, Qt.NoModifier, self.at(20, 15))
        self.assertEqual(hits, [0])
        self.c.show_bounds = True
        self.paint()


class TestHudList(unittest.TestCase):

    def test_drag_reorder(self):
        import time
        from editor.armed_hud import ArmedHud, AgentHost
        page = ArmedHud(AgentHost())
        page.hud = Layout([Widget(W_TEXT, label=str(i)) for i in range(3)])
        page.refresh_list()
        # the list shows the front widget first; what a drop does in Qt 5.9: take the item, insert it elsewhere
        item = page.list.takeItem(0)
        page.list.insertItem(2, item)
        t = time.time()
        while time.time() - t < 0.2:
            app.processEvents()
        self.assertEqual([w.label for w in page.hud.widgets], ["2", "0", "1"])
        self.assertEqual(len(page.undo), 1)
