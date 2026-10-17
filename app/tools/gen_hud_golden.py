#!/usr/bin/env python3
"""Golden data for tst_hud: layouts packed and rendered by the Python app's hud_layout / hud_render.

QT_QPA_PLATFORM=offscreen venv/bin/python app/tools/gen_hud_golden.py  ->  app/tests/data/hud/
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "src", "main", "python"))

from PyQt5.QtWidgets import QApplication  # noqa: E402

from hud_layout import *  # noqa: E402,F401,F403
from hud_layout import Layout, Widget  # noqa: E402
from hud_presets import PRESETS  # noqa: E402
from hud_render import Renderer, QffFont, Sources, Value, DEFAULT_PALETTE  # noqa: E402

OUT = os.path.join(ROOT, "app", "tests", "data", "hud")


class Info:
    max_widgets = 40
    strings = 512
    layout_size = 4 + 24 * 40 + 512


class Stub(Sources):
    """ deterministic values: every source has a number, some have text and history """

    def value(self, source):
        if source in (S_TIME, S_DATE, S_LINK, S_LAYER):
            return Value(0, 0, C_ACCENT, True, {S_TIME: "13:37:42", S_DATE: "2026-10-17", S_LINK: "USB  2.4G",
                                                S_LAYER: "LAYER 1"}[source])
        if source == S_CHARGING:
            return Value(1, 0, C_OK)
        if source == S_BATTERY:
            return Value(73, 0, C_OK)
        if source == S_HOST + 7:
            return None
        if source == S_HOST + 8:
            return Value(0, 0, C_LINE, False)
        if source >= S_HOST_TXT:
            return Value(0, 0, C_TEXT, True)
        n = (source * 37) % 101
        return Value(n * 10 + 3 if source % 3 == 0 else n, 1 if source % 3 == 0 else 0, C_TEXT + source % 4, True)

    def host_text(self, slot):
        return ["Line one\nline two is a much longer line that has to wrap across the box width\n\nlast", "",
                "x" * 200, "tabs\tand ~ symbols {}[]"][slot]

    def history(self, source):
        return [Value((i * 13 + source * 7) % 101, 0) for i in range(80)]


def kitchen_sink():
    w = []
    for i, (align, fg) in enumerate(((0, C_TEXT), (1, C_ACCENT), (2, C_AUTO))):
        w.append(Widget(W_TEXT, S_HOST + i, font=i % 3, fg=fg, flags=align, x=4, y=2 + i * 22, w=150,
                        label="V{}={{}}u".format(i)))
    w += [
        Widget(W_TEXT, S_HOST + 3, font=0, fg=C_TEXT, flags=F_WARN, warn=10, x=160, y=2, w=70, param=3),
        Widget(W_TEXT, S_HOST + 4, font=0, fg=C_TEXT, flags=F_WARN | F_WARN_BELOW, warn=500, x=160, y=20, w=70),
        Widget(W_TEXT, S_HOST + 7, font=0, fg=C_TEXT, bg=C_PANEL, x=240, y=2, w=76, label="NA {}"),
        Widget(W_TEXT, S_HOST + 8, font=0, fg=C_AUTO, x=240, y=20, w=76),
        Widget(W_TEXT, font=1, fg=C_DIM, x=160, y=40, w=40, label="CLIPPED TEXT"),
        Widget(W_BAR, S_HOST + 1, fg=C_ACCENT, x=4, y=70, w=100, h=10),
        Widget(W_BAR, S_HOST + 2, fg=C_ACCENT, flags=F_WARN, warn=5, x=4, y=84, w=100, h=12, param=7, lo=-50, hi=150),
        Widget(W_BAR, S_HOST + 3, fg=C_OK, x=4, y=100, w=3, h=3),
        Widget(W_GRAPH, S_HOST + 5, fg=C_ACCENT, x=110, y=66, w=90, h=40),
        Widget(W_GRAPH, S_HOST + 6, fg=C_WARN, flags=F_FILL, x=206, y=66, w=110, h=40, hi=100),
        Widget(W_INDICATOR, S_HOST + 1, font=0, fg=C_AUTO, x=4, y=110, w=90, h=20, label="ON"),
        Widget(W_INDICATOR, S_HOST + 8, font=0, fg=C_OK, x=98, y=110, w=90, h=20),
        Widget(W_INDICATOR, S_HOST + 2, fg=C_AUTO, flags=F_DOT, x=192, y=114, w=8, h=8),
        Widget(W_INDICATOR, S_HOST + 2, fg=C_WARN, flags=F_DOT, x=204, y=114, w=8, h=8),
        Widget(W_BATTERY, S_BATTERY, fg=C_AUTO, x=220, y=112, w=25, h=11),
        Widget(W_FRAME, fg=C_LINE, x=250, y=108, w=30, h=20, param=FRAME_BRACKETS),
        Widget(W_FRAME, fg=C_AUTO, x=284, y=108, w=30, h=20, param=FRAME_BOX),
        Widget(W_FRAME, fg=C_DIM, x=250, y=132, w=60, h=1, param=FRAME_HLINE),
        Widget(W_FRAME, fg=C_DIM, x=316, y=100, w=1, h=60, param=FRAME_VLINE),
        Widget(W_FRAME, fg=C_PANEL, x=200, y=140, w=40, h=10, param=FRAME_PANEL),
        Widget(W_TEXTBOX, S_HOST_TXT, font=0, fg=C_TEXT, x=4, y=134, w=120, h=36, param=1),
        Widget(W_TEXTBOX, S_HOST_TXT, font=0, fg=C_TEXT, flags=F_TAIL | 2, x=128, y=134, w=70, h=36),
        Widget(W_TEXTBOX, S_HOST_TXT + 2, font=0, fg=C_ACCENT, x=250, y=140, w=60, h=30),
        Widget(W_TEXT, S_HOST + 9, font=0, x=400, y=10, w=50),
        Widget(W_NONE, x=10, y=10, w=10, h=10),
    ]
    return Layout(w)


def main():
    app = QApplication(sys.argv)  # noqa: F841
    fonts = []
    for i in range(3):
        with open(os.path.join(OUT, "font{}.qff".format(i)), "rb") as f:
            fonts.append(QffFont(f.read()))
    cases = [(name, fn()) for name, fn in PRESETS if fn] + [("KITCHEN SINK", kitchen_sink())]
    index = []
    for n, (name, layout) in enumerate(cases):
        r = Renderer(320, 172, fonts, DEFAULT_PALETTE, Stub())
        r.names = {S_HOST + 8: "HOST 9"}
        img = r.render(layout)
        png = "render{}.png".format(n)
        img.save(os.path.join(OUT, png))
        index.append({"name": name, "layout": json.loads(layout.to_json()), "packed": layout.pack(Info()).hex(),
                      "png": png})
    with open(os.path.join(OUT, "golden.json"), "w") as f:
        json.dump(index, f, indent=0)
    print(OUT)


if __name__ == "__main__":
    main()
