# SPDX-License-Identifier: GPL-2.0-or-later
# Python port of qmk-armed's armed_theme.c renderer, keep the two in step
import random

from effects_preview import sqrt16, qadd8, qsub8, HITS_TO_REMEMBER
from lighting_theme import SOLID, GRADIENT, RAINBOW, PULSE, WAVE, SPARKLE, REACTIVE, RIPPLE, PAINT, ADD, MULTIPLY, \
    F_RADIAL, F_MIRROR, F_REVERSE, F_HIDDEN, C_CAPS, C_NO_CAPS, C_LAYER, C_NOT_LAYER

FRAME_MS = 16
B_M16 = (0, 49, 49, 41, 90, 27, 117, 10)


def sin8(theta):
    """ lib8tion's sin8_C, bit for bit """
    theta &= 0xFF
    offset = (255 - theta) if theta & 0x40 else theta
    offset &= 0x3F
    sec = offset & 0x0F
    if theta & 0x40:
        sec += 1
    s2 = (offset >> 4) * 2
    y = ((B_M16[s2 + 1] * sec) >> 4) + B_M16[s2]
    y = ((y + 128) & 0xFF) - 128
    if theta & 0x80:
        y = -y
    return (y + 128) & 0xFF


def hsv_rgb(h, s, v):
    """ QMK's hsv_to_rgb """
    if s == 0:
        return v, v, v
    region = h * 6 // 255
    rem = ((h * 2 - region * 85) * 3) & 0xFF
    p = (v * (255 - s)) >> 8
    q = (v * (255 - ((s * rem) >> 8))) >> 8
    t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8
    return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q), (v, t, p)][region]


def cos8(theta):
    return sin8(theta + 64)


def lerp8(a, b, t):
    return a + int((b - a) * t / 255)


def lerp_rgb(a, b, t):
    return tuple(lerp8(a[k], b[k], t) for k in range(3))


def tri8(x):
    return x * 2 if x < 128 else (255 - x) * 2


def dist8(dx, dy):
    return sqrt16(dx * dx + dy * dy)


class ThemeRenderer:

    def __init__(self, leds):
        self.leds = leds
        self.hits = []
        self.caps = False
        self.layer_on = set()
        self.spark = {}
        self.last_t = None
        self.steps = 0

    def hit(self, idx, t):
        led = self.leds[idx]
        self.hits.append((idx, led.x, led.y, t))
        self.hits = self.hits[-HITS_TO_REMEMBER:]

    def condition(self, cond):
        n = cond & 0x0F
        kind = cond & 0xF0
        if kind == 0:
            return self.caps if cond == C_CAPS else (not self.caps if cond == C_NO_CAPS else True)
        top = max(self.layer_on) if self.layer_on else 0
        if kind == C_LAYER:
            return n in self.layer_on if n else top == 0
        if kind == C_NOT_LAYER:
            return n not in self.layer_on if n else top != 0
        return True

    def frame(self, theme, t, v=255):
        steps = 1 if self.last_t is None else max(0, (t - self.last_t) // FRAME_MS)
        if self.last_t is None or steps:
            self.last_t = t
        frames = []
        for li, layer in enumerate(theme.layers):
            frames.append(self.setup(layer, t) if layer.type and not layer.flags & F_HIDDEN
                          and self.condition(layer.cond) else None)
        out = []
        for i, led in enumerate(self.leds):
            o = [0, 0, 0]
            for li, layer in enumerate(theme.layers):
                f = frames[li]
                if f is None or i not in layer.mask:
                    continue
                c, a = self.color(theme, li, layer, f, i, led, t, steps)
                a = a * layer.opacity // 255
                if not a:
                    continue
                for k in range(3):
                    if layer.blend == ADD:
                        o[k] = qadd8(o[k], c[k] * a // 255)
                    elif layer.blend == MULTIPLY:
                        o[k] = lerp8(o[k], o[k] * c[k] // 255, a)
                    else:
                        o[k] = lerp8(o[k], c[k], a)
            out.append(tuple(x * v // 255 for x in o))
        return out

    @staticmethod
    def setup(layer, t):
        f = {"phase": (t * layer.speed >> 8) & 0xFF}
        if layer.flags & F_RADIAL:
            cx, cy = layer.cx, layer.cy
            f["maxd"] = max(1, dist8(cx, cy), dist8(224 - cx, cy), dist8(cx, 64 - cy), dist8(224 - cx, 64 - cy))
        else:
            c, s = cos8(layer.dir) - 128, sin8(layer.dir) - 128
            f["c"], f["s"] = c, s
            f["e"] = max(1, 112 * abs(c) + 32 * abs(s))
        return f

    @staticmethod
    def position(layer, f, x, y):
        if layer.flags & F_RADIAL:
            p = dist8(x - layer.cx, y - layer.cy) * 255 // f["maxd"]
        else:
            num = ((x - 112) * f["c"] + (y - 32) * f["s"] + f["e"]) * 255
            p = int(num / (2 * f["e"]))
        p = max(0, min(255, p))
        return 255 - p if layer.flags & F_REVERSE else p

    @staticmethod
    def spread(layer, pos):
        return (pos * 128 // layer.size) & 0xFF if layer.size else 0

    def color(self, theme, li, layer, f, i, led, t, steps):
        tp = layer.type
        if tp == SOLID:
            return layer.a, 255
        if tp == GRADIENT:
            k = (self.spread(layer, self.position(layer, f, led.x, led.y)) - f["phase"]) & 0xFF
            if layer.flags & F_MIRROR:
                k = tri8(k)
            return lerp_rgb(layer.a, layer.b, k), 255
        if tp == RAINBOW:
            hue = (self.spread(layer, self.position(layer, f, led.x, led.y)) - f["phase"]) & 0xFF
            return hsv_rgb(hue, layer.param, 255), 255
        if tp == PULSE:
            k = sin8((f["phase"] - self.spread(layer, self.position(layer, f, led.x, led.y))) & 0xFF)
            return lerp_rgb(layer.a, layer.b, k), 255
        if tp == WAVE:
            pos = self.position(layer, f, led.x, led.y)
            center = tri8(f["phase"]) if layer.flags & F_MIRROR else f["phase"]
            d = abs(pos - center)
            if not layer.flags & F_MIRROR and d > 128:
                d = 256 - d
            w = layer.size // 2 + 1
            return layer.a, 0 if d >= w else 255 - d * 255 // w
        if tp == SPARKLE:
            lv, mix = self.spark.get((li, i), (0, 0))
            for _ in range(min(steps, 32)):
                if lv:
                    lv = qsub8(lv, 1 + (255 - layer.size) // 16)
                elif random.randrange(65536) < (layer.speed + 1) * 4:
                    lv, mix = 255, random.randrange(256)
            self.spark[(li, i)] = (lv, mix)
            return lerp_rgb(layer.a, layer.b, mix), lv
        if tp == REACTIVE:
            return layer.a, self.reactive(layer, i, led, t)
        if tp == RIPPLE:
            return layer.a, self.ripple(layer, led, t)
        if tp == PAINT:
            return theme.paint[i], 255
        return (0, 0, 0), 0

    def reactive(self, layer, i, led, t):
        fade = 100 + layer.size * 8
        best = 0
        for idx, x, y, th in self.hits:
            tick = min(65535, t - th)
            if tick >= fade:
                continue
            lvl = 255 - tick * 255 // fade
            if layer.param in (0, 255):
                if idx != i:
                    continue
            else:
                d = dist8(led.x - x, led.y - y)
                if d > layer.param:
                    continue
                lvl = lvl * (layer.param + 1 - d) // (layer.param + 1)
            best = max(best, lvl)
        return best

    def ripple(self, layer, led, t):
        w = 4 + layer.size // 8
        best = 0
        for _, x, y, th in self.hits:
            r = (min(65535, t - th) * (layer.speed + 8)) >> 8
            if r >= 255:
                continue
            d = abs(dist8(led.x - x, led.y - y) - r)
            if d >= w:
                continue
            best = max(best, (255 - d * 255 // w) * (255 - r) // 255)
        return best
