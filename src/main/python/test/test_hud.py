import struct
import unittest

from hud_layout import Layout, Widget, LayoutError, W_TEXT, W_BAR, S_HOST, C_ACCENT, MAGIC
from hud_agent import to_ascii, first_number
from protocol.qmk_armed import HudInfo, hud_push_packets, hud_text_packets, CMD_ARMED, SUB_HUD_PUSH, SUB_HUD_TEXT


def info():
    # the IO Type 84 Pro's HUD_INFO reply
    data = bytes([0xAD, 18]) + struct.pack("<BBBHHBBBBBBHHBB", 1, 40, 24, 1476, 512, 16, 4, 160, 3, 8, 39, 320, 172, 4, 64)
    return HudInfo(data)


class TestHud(unittest.TestCase):

    def test_pack_roundtrip(self):
        lay = Layout([Widget(W_TEXT, x=8, y=3, w=50, label="IO//84"),
                      Widget(W_BAR, S_HOST, fg=C_ACCENT, x=46, y=31, w=150, h=13, hi=100, warn=90, param=10),
                      Widget(W_TEXT, S_HOST, x=200, y=30, w=48, label="{}%"),
                      Widget(W_TEXT, x=8, y=40, label="IO//84")])
        blob = lay.pack(info())
        self.assertEqual(len(blob), 1476)
        self.assertEqual(blob[:3], bytes([MAGIC, 1, 4]))
        self.assertEqual(Layout.unpack(blob, info()), lay)
        # equal labels are stored once
        self.assertEqual(blob[4 + 22:4 + 24], blob[4 + 3 * 24 + 22:4 + 3 * 24 + 24])

    def test_limits(self):
        with self.assertRaises(LayoutError):
            Layout([Widget() for _ in range(41)]).pack(info())
        with self.assertRaises(LayoutError):
            Layout([Widget(label="x" * 40 + str(i)) for i in range(20)]).pack(info())

    def test_json(self):
        lay = Layout([Widget(W_TEXT, S_HOST + 1, x=1, y=2, label="CPU {}%")])
        self.assertEqual(Layout.from_json(lay.to_json()), lay)

    def test_push_packets(self):
        p = hud_push_packets([(i, 1, i * 10) for i in range(6)], 5)
        self.assertEqual(len(p), 2)
        self.assertEqual(p[0][:4], bytes([CMD_ARMED, SUB_HUD_PUSH, 5, 4]))
        self.assertEqual(struct.unpack_from("<BBi", p[1], 4), (4, 1, 40))
        self.assertTrue(all(len(x) <= 32 for x in p))

    def test_text_packets(self):
        p = hud_text_packets(2, "a" * 60, 10, 160)
        self.assertEqual([x[4] for x in p], [0, 25, 50])
        self.assertEqual([x[6] for x in p], [0, 0, 1])
        self.assertEqual(b"".join(x[7:] for x in p), b"a" * 60)
        self.assertEqual(p[0][:4], bytes([CMD_ARMED, SUB_HUD_TEXT, 2, 10]))
        self.assertEqual(len(hud_text_packets(0, "", 10, 160)), 1)

    def test_ascii(self):
        self.assertEqual(to_ascii("Привет, мир — 23°C\t\x1b[31mok\x1b[0m"), "Privet, mir - 23C ok")
        self.assertEqual(first_number("temp: 63,5 C"), 63.5)
        self.assertIsNone(first_number("none"))

    def test_unknown_role_draws_as_text(self):
        import hud_render
        cv = hud_render.Canvas(8, 8, hud_render.DEFAULT_PALETTE)
        self.assertEqual(cv.hsv(200), hud_render.DEFAULT_PALETTE[4])
        self.assertEqual(cv.role(-1), cv.role(4))
