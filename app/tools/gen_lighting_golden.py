#!/usr/bin/env python3
"""Golden data for tst_lighting: effect previews, light bar and theme frames from the Python app.

QT_QPA_PLATFORM=offscreen venv/bin/python app/tools/gen_lighting_golden.py  ->  app/tests/data/lighting-golden.json.xz
"""
import json
import lzma
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "src", "main", "python"))

from PyQt5.QtWidgets import QApplication  # noqa: E402

import lighting_theme as LT  # noqa: E402
from effects_preview import EffectPreview, StripPreview  # noqa: E402
from protocol.qmk_armed import ArmedLed  # noqa: E402
from theme_render import ThemeRenderer  # noqa: E402

EFFECTS = ["SOLID_COLOR", "BREATHING", "BAND_SAT", "BAND_VAL", "CYCLE_ALL", "CYCLE_LEFT_RIGHT", "CYCLE_UP_DOWN",
           "RAINBOW_MOVING_CHEVRON", "CYCLE_OUT_IN", "CYCLE_PINWHEEL", "CYCLE_SPIRAL", "RAINBOW_BEACON",
           "HUE_BREATHING", "HUE_PENDULUM", "HUE_WAVE", "SOLID_REACTIVE_SIMPLE", "SOLID_REACTIVE",
           "SOLID_REACTIVE_WIDE", "SOLID_REACTIVE_MULTIWIDE", "SOLID_REACTIVE_CROSS", "SOLID_REACTIVE_MULTICROSS",
           "SOLID_REACTIVE_NEXUS", "SOLID_REACTIVE_MULTINEXUS", "SPLASH", "MULTISPLASH", "SOLID_SPLASH",
           "SOLID_MULTISPLASH", "TYPING_HEATMAP", "TWO_TONE", "GRADIENT_2", "CROSSFADE_2", "SCANNER_2", "REACTIVE_2",
           "RIPPLE_2", "UNKNOWN EFFECT"]
TIMES = [0, 16, 250, 700, 1500, 4000, 12345, 70000]
HITS = [(5, 100), (40, 400), (41, 650), (70, 1400)]


def frames(make, render):
    out = []
    obj = make()
    hits = list(HITS)
    for t in TIMES:
        while hits and hits[0][1] <= t:
            idx, th = hits.pop(0)
            obj.hit(idx, th)
        out.append([list(c) for c in render(obj, t)])
    return out


def themes(leds, layout):
    out = [(name, LT.build_preset(i, leds, layout)) for i, (name, _, _) in enumerate(LT.PRESETS)
           if name not in ("DEEP SEA",)]
    t = LT.Theme("MIX", len(leds))
    all_leds = set(range(len(leds)))
    specs = [
        (LT.SOLID, dict(a=(10, 20, 30))),
        (LT.GRADIENT, dict(a=(255, 0, 0), b=(0, 0, 255), dir=200, speed=7, size=60, flags=LT.F_REVERSE)),
        (LT.RAINBOW, dict(speed=50, size=0, param=128, blend=LT.ADD, opacity=128)),
        (LT.PULSE, dict(a=(0, 255, 0), b=(255, 0, 255), dir=90, speed=33, size=200, blend=LT.MULTIPLY)),
        (LT.WAVE, dict(a=(255, 255, 0), speed=77, size=30, flags=LT.F_MIRROR | LT.F_RADIAL, cx=30, cy=50)),
        (LT.REACTIVE, dict(a=(255, 0, 0), size=10, param=40)),
        (LT.RIPPLE, dict(a=(0, 0, 255), speed=0, size=200, blend=LT.ADD)),
        (LT.PAINT, dict()),
        (LT.SOLID, dict(a=(255, 255, 255), cond=LT.C_CAPS, opacity=200)),
        (LT.SOLID, dict(a=(9, 9, 9), cond=LT.C_NOT_LAYER, flags=LT.F_HIDDEN)),
    ]
    for ltype, kw in specs:
        layer = LT.Layer(ltype, all_leds if ltype != LT.PAINT else set(range(0, len(leds), 3)))
        for k, v in kw.items():
            setattr(layer, k, v)
        t.layers.append(layer)
    t.paint = [((i * 40) % 256, (i * 90) % 256, (i * 7) % 256) for i in range(len(leds))]
    out.append(("MIX", t))
    return out


def main():
    app = QApplication(sys.argv)  # noqa: F841
    with open(os.path.join(ROOT, "app", "tests", "data", "leds-type84.json")) as f:
        leds = [ArmedLed(*e) for e in json.load(f)]
    layout = {(0, 0, 1): "KC_F1", (0, 2, 2): "KC_W", (0, 3, 1): "KC_A", (1, 2, 2): "KC_UP", (0, 5, 9): "MO(1)",
              (0, 5, 13): "KC_LEFT", (0, 3, 3): "KC_D", (0, 1, 2): "KC_2"}
    out = {"effects": {}, "strip": {}, "themes": {}}
    for e in EFFECTS:
        def make(e=e):
            p = EffectPreview(leds)
            p.set_effect(e)
            p.h, p.s, p.v, p.speed, p.color2 = 20, 230, 200, 140, (150, 200)
            return p
        out["effects"][e] = frames(make, lambda p, t: p.frame(t))
    for mode in ["WPM", "WPM BAR", "BATTERY", "LINK", "SCANNER", "OFF"]:
        s = StripPreview()
        s.mode, s.color = mode, (30, 220)
        out["strip"][mode] = [[list(c) for c in s.frame(t)] for t in TIMES]
    for name, theme in themes(leds, layout):
        for caps, layers_on in ((False, set()), (True, {1})):
            def make():
                r = ThemeRenderer(leds)
                r.caps, r.layer_on = caps, layers_on
                return r
            key = "{}|{}".format(name, int(caps))
            out["themes"][key] = {"theme": theme.pack(len(leds), 16).hex(),
                                  "frames": frames(make, lambda r, t: r.frame(theme, t, 230))}
    path = os.path.join(ROOT, "app", "tests", "data", "lighting-golden.json.xz")
    with lzma.open(path, "wt") as f:
        json.dump(out, f)
    print(path, os.path.getsize(path))


if __name__ == "__main__":
    main()
