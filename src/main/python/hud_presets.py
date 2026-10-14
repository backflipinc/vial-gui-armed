# SPDX-License-Identifier: GPL-2.0-or-later
# Starting points for the HUD page; host slots follow hud_agent.default_config() (H1 CPU load, H2 CPU temp, ...)
from hud_layout import Layout, Widget, W_TEXT, W_BAR, W_GRAPH, W_INDICATOR, W_BATTERY, W_FRAME, W_TEXTBOX, \
    FRAME_BRACKETS, FRAME_HLINE, C_DIM, C_LINE, C_TEXT, C_ACCENT, C_AUTO, F_WARN, F_FILL, F_TAIL, F_DOT, \
    S_LAYER, S_BATTERY, S_LINK, S_TIME, S_DATE, S_HOST, S_HOST_TXT


def preset_monitor():
    h = lambda i: S_HOST + i
    w = [
        Widget(W_TEXT, font=0, fg=C_ACCENT, x=8, y=3, w=50, label="IO//84"),
        Widget(W_TEXT, font=0, fg=C_DIM, x=62, y=3, w=60, label="SYS"),
        Widget(W_TEXT, S_TIME, font=0, fg=C_AUTO, flags=2, x=200, y=3, w=112),
        Widget(W_FRAME, fg=C_LINE, x=0, y=22, w=320, h=1, param=FRAME_HLINE),
    ]
    for r, (name, load, temp) in enumerate((("CPU", 0, 1), ("GPU", 3, 4), ("RAM", 2, None))):
        y = 30 + r * 22
        w.append(Widget(W_TEXT, font=0, fg=C_DIM, x=8, y=y, w=36, label=name))
        w.append(Widget(W_BAR, h(load), fg=C_ACCENT, flags=F_WARN, x=46, y=y + 1, w=150, h=13, hi=100, warn=90, param=10))
        w.append(Widget(W_TEXT, h(load), font=0, fg=C_TEXT, flags=2, x=200, y=y, w=48, label="{}%"))
        if temp is not None:
            w.append(Widget(W_TEXT, h(temp), font=0, fg=C_TEXT, flags=2 | F_WARN, x=252, y=y, w=60, warn=85,
                            label="{}C"))
    w += [
        Widget(W_FRAME, fg=C_DIM, x=4, y=96, w=206, h=50, param=FRAME_BRACKETS, lo=5),
        Widget(W_GRAPH, h(0), fg=C_ACCENT, flags=F_FILL, x=10, y=100, w=194, h=42, hi=100, param=10),
        Widget(W_TEXT, font=0, fg=C_DIM, x=218, y=100, w=24, label="DN"),
        Widget(W_TEXT, h(5), font=0, fg=C_TEXT, flags=2, x=244, y=100, w=68, label="{}M"),
        Widget(W_TEXT, font=0, fg=C_DIM, x=218, y=122, w=24, label="UP"),
        Widget(W_TEXT, h(6), font=0, fg=C_TEXT, flags=2, x=244, y=122, w=68, label="{}M"),
        Widget(W_FRAME, fg=C_LINE, x=0, y=150, w=320, h=1, param=FRAME_HLINE),
        Widget(W_INDICATOR, S_LINK, fg=C_AUTO, flags=F_DOT, x=8, y=158, w=6, h=6),
        Widget(W_TEXT, S_LINK, font=0, fg=C_TEXT, x=20, y=154, w=180, h=17),
        Widget(W_TEXT, S_LAYER, font=0, fg=C_AUTO, flags=2, x=204, y=154, w=108),
    ]
    return Layout(w)


def preset_console():
    return Layout([
        Widget(W_TEXT, font=0, fg=C_ACCENT, x=8, y=3, w=50, label="IO//84"),
        Widget(W_TEXT, font=0, fg=C_DIM, x=62, y=3, w=80, label="CONSOLE"),
        Widget(W_TEXT, S_TIME, font=0, fg=C_AUTO, flags=2, x=200, y=3, w=112),
        Widget(W_FRAME, fg=C_LINE, x=0, y=22, w=320, h=1, param=FRAME_HLINE),
        Widget(W_TEXTBOX, S_HOST_TXT, font=0, fg=C_TEXT, flags=F_TAIL, x=8, y=28, w=304, h=118, param=2),
        Widget(W_FRAME, fg=C_LINE, x=0, y=150, w=320, h=1, param=FRAME_HLINE),
        Widget(W_INDICATOR, S_LINK, fg=C_AUTO, flags=F_DOT, x=8, y=158, w=6, h=6),
        Widget(W_TEXT, S_LINK, font=0, fg=C_TEXT, x=20, y=154, w=180, h=17),
        Widget(W_TEXT, S_BATTERY, font=0, fg=C_AUTO, flags=2, x=204, y=154, w=76, label="{}%"),
        Widget(W_BATTERY, S_BATTERY, fg=C_ACCENT, x=287, y=157, w=25, h=11),
    ])


def preset_clock():
    return Layout([
        Widget(W_FRAME, fg=C_DIM, x=6, y=6, w=308, h=160, param=FRAME_BRACKETS, lo=8),
        Widget(W_TEXT, S_TIME, font=2, fg=C_AUTO, flags=1, x=10, y=30, w=300, h=46),
        Widget(W_TEXT, S_DATE, font=1, fg=C_DIM, flags=1, x=10, y=86, w=300, h=22),
        Widget(W_FRAME, fg=C_LINE, x=40, y=120, w=240, h=1, param=FRAME_HLINE),
        Widget(W_TEXT, S_LAYER, font=0, fg=C_AUTO, flags=1, x=10, y=130, w=150),
        Widget(W_TEXT, S_BATTERY, font=0, fg=C_AUTO, flags=1, x=160, y=130, w=150, label="BAT {}%"),
    ])


PRESETS = [
    ("KEYBOARD DEFAULT", None),
    ("SYSTEM MONITOR", preset_monitor),
    ("CONSOLE (HOST TEXT 1)", preset_console),
    ("BIG CLOCK", preset_clock),
]
