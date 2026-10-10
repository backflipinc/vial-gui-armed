# SPDX-License-Identifier: GPL-2.0-or-later
# Python port of the QMK RGB Matrix effects and the IO Type 84 Pro two-colour effects, for previews
import math
import random

from PyQt5.QtGui import QColor

CENTER_X, CENTER_Y = 112, 32
HITS_TO_REMEMBER = 8

RAINBOW = {"CYCLE_ALL", "CYCLE_LEFT_RIGHT", "CYCLE_UP_DOWN", "RAINBOW_MOVING_CHEVRON", "CYCLE_OUT_IN",
           "CYCLE_PINWHEEL", "CYCLE_SPIRAL", "RAINBOW_BEACON", "DIGITAL_RAIN", "TYPING_HEATMAP"}
TWO_COLOR = {"TWO_TONE", "GRADIENT_2", "CROSSFADE_2", "SCANNER_2", "STARLIGHT_2", "REACTIVE_2", "RIPPLE_2"}
REACTIVE = {"SOLID_REACTIVE_SIMPLE", "SOLID_REACTIVE", "SOLID_REACTIVE_WIDE", "SOLID_REACTIVE_MULTIWIDE",
            "SOLID_REACTIVE_CROSS", "SOLID_REACTIVE_MULTICROSS", "SOLID_REACTIVE_NEXUS",
            "SOLID_REACTIVE_MULTINEXUS", "SPLASH", "MULTISPLASH", "SOLID_SPLASH", "SOLID_MULTISPLASH",
            "TYPING_HEATMAP", "REACTIVE_2", "RIPPLE_2"}


def effect_key(name):
    return name.strip().upper().replace(" ", "_")


def u8(x):
    return int(x) & 0xFF


def sin8(x):
    return int(round(128 + 127 * math.sin(u8(x) * math.pi / 128)))


def cos8(x):
    return sin8(u8(x) + 64)


def scale8(i, scale):
    return (int(i) * int(scale)) >> 8


def scale16by8(i, scale):
    return ((int(i) & 0xFFFF) * int(scale)) >> 8


def qadd8(a, b):
    return min(255, int(a) + int(b))


def qsub8(a, b):
    return max(0, int(a) - int(b))


def abs8(x):
    return abs(int(x))


def atan2_8(dy, dx):
    return u8(math.atan2(dy, dx) * 128 / math.pi)


def sqrt16(x):
    return min(255, int(math.sqrt(max(0, x))))


def hsv_rgb(h, s, v):
    c = QColor.fromHsv(int(u8(h) * 359 / 255), max(0, min(255, int(s))), max(0, min(255, int(v))))
    return c.red(), c.green(), c.blue()


def lerp_rgb(a, b, t):
    return tuple(int(a[k] + (b[k] - a[k]) * t / 255) for k in range(3))


class EffectPreview:
    """ Renders one frame of an effect for every LED: frame(t_ms) -> [(r, g, b), ...] """

    def __init__(self, leds):
        self.leds = leds
        self.effect = "SOLID_COLOR"
        self.h, self.s, self.v = 0, 255, 255
        self.speed = 128
        self.color2 = (170, 255)
        self.color2_black = False
        self.color1_black = False
        self.hits = []
        self.reset()

    def reset(self):
        n = len(self.leds)
        self.buf = [0] * n
        self.star = [0] * n
        self.drops = [0] * n
        self.rain_colors = None
        self.last_t = None
        self.last_drop = 0
        self.last_decay = 0

    def set_effect(self, name):
        key = effect_key(name)
        if key != self.effect:
            self.effect = key
            self.reset()

    def hit(self, idx, t):
        led = self.leds[idx]
        self.hits.append((idx, led.x, led.y, t))
        self.hits = self.hits[-HITS_TO_REMEMBER:]
        for j, other in enumerate(self.leds):
            if j == idx:
                self.buf[j] = qadd8(self.buf[j], 32)
            else:
                d = sqrt16((led.x - other.x) ** 2 + (led.y - other.y) ** 2)
                if d <= 40:
                    self.buf[j] = qadd8(self.buf[j], min(16, 40 - d))

    @property
    def reactive(self):
        return self.effect in REACTIVE

    def c1(self):
        return (0, 0, 0) if self.color1_black else hsv_rgb(self.h, self.s, self.v)

    def c2(self):
        return (0, 0, 0) if self.color2_black else hsv_rgb(self.color2[0], self.color2[1], self.v)

    def frame(self, t):
        dt = 0 if self.last_t is None else max(0, t - self.last_t)
        self.last_t = t
        fn = getattr(self, "fx_" + self.effect.lower(), None)
        if fn is None:
            fn = self.fx_solid_color
        out = fn(t, dt)
        if self.color1_black and self.effect not in RAINBOW and self.effect not in TWO_COLOR:
            out = [(0, 0, 0)] * len(self.leds)
        return out

    # runners

    def run_i(self, t, math_fn):
        time = u8(scale16by8(t, qadd8(self.speed // 4, 1)))
        return [hsv_rgb(*math_fn(i, led, time)) for i, led in enumerate(self.leds)]

    def run_dist(self, t, math_fn, speed_div=2):
        time = u8(scale16by8(t, self.speed // speed_div))
        out = []
        for led in self.leds:
            dx, dy = led.x - CENTER_X, led.y - CENTER_Y
            out.append(hsv_rgb(*math_fn(dx, dy, sqrt16(dx * dx + dy * dy), time)))
        return out

    def hit_ticks(self, t):
        return [(idx, x, y, scale16by8(t - th, qadd8(self.speed, 1))) for idx, x, y, th in self.hits]

    def run_reactive(self, t, math_fn):
        max_tick = 65535 // qadd8(self.speed, 1)
        out = []
        for i in range(len(self.leds)):
            tick = max_tick
            for idx, _, _, th in reversed(self.hits):
                if idx == i and t - th < tick:
                    tick = t - th
                    break
            out.append(hsv_rgb(*math_fn(scale16by8(min(tick, 65535), qadd8(self.speed, 1)))))
        return out

    def run_splash(self, t, math_fn, multi):
        ticks = self.hit_ticks(t)
        if not multi:
            ticks = ticks[-1:]
        out = []
        for led in self.leds:
            h, s, v = self.h, self.s, 0
            for _, x, y, tick in ticks:
                dx, dy = led.x - x, led.y - y
                h, s, v = math_fn(h, s, v, dx, dy, sqrt16(dx * dx + dy * dy), tick)
            out.append(hsv_rgb(h, s, scale8(v, self.v)))
        return out

    # stock effects

    def fx_solid_color(self, t, dt):
        return [hsv_rgb(self.h, self.s, self.v)] * len(self.leds)

    def fx_breathing(self, t, dt):
        return self.run_i(t, lambda i, led, time: (self.h, self.s, scale8(abs8(sin8(time // 2) - 128) * 2, self.v)))

    def fx_band_sat(self, t, dt):
        def m(i, led, time):
            s = self.s - abs(scale8(led.x, 228) + 28 - time) * 8
            return self.h, scale8(max(0, s), self.s), self.v
        return self.run_i(t, m)

    def fx_band_val(self, t, dt):
        def m(i, led, time):
            v = self.v - abs(scale8(led.x, 228) + 28 - time) * 8
            return self.h, self.s, scale8(max(0, v), self.v)
        return self.run_i(t, m)

    def fx_cycle_all(self, t, dt):
        return self.run_i(t, lambda i, led, time: (time, self.s, self.v))

    def fx_cycle_left_right(self, t, dt):
        return self.run_i(t, lambda i, led, time: (led.x - time, self.s, self.v))

    def fx_cycle_up_down(self, t, dt):
        return self.run_i(t, lambda i, led, time: (led.y - time, self.s, self.v))

    def fx_rainbow_moving_chevron(self, t, dt):
        return self.run_i(t, lambda i, led, time: (self.h + abs8(led.y - CENTER_Y) + led.x - time, self.s, self.v))

    def fx_cycle_out_in(self, t, dt):
        return self.run_dist(t, lambda dx, dy, dist, time: (3 * dist // 2 + time, self.s, self.v))

    def fx_cycle_pinwheel(self, t, dt):
        return self.run_dist(t, lambda dx, dy, dist, time: (atan2_8(dy, dx) + time, self.s, self.v))

    def fx_cycle_spiral(self, t, dt):
        return self.run_dist(t, lambda dx, dy, dist, time: (dist - time - atan2_8(dy, dx), self.s, self.v))

    def fx_rainbow_beacon(self, t, dt):
        time = scale16by8(t, self.speed // 4)
        cos_v, sin_v = cos8(time) - 128, sin8(time) - 128
        return [hsv_rgb(self.h + ((led.y - CENTER_Y) * 2 * sin_v + (led.x - CENTER_X) * 2 * cos_v) // 128,
                        self.s, self.v) for led in self.leds]

    def fx_hue_breathing(self, t, dt):
        return self.run_i(t, lambda i, led, time: (self.h + scale8(abs8(sin8(time // 2) - 128) * 2, 12), self.s, self.v))

    def fx_hue_pendulum(self, t, dt):
        return self.run_i(t, lambda i, led, time: (self.h + scale8(abs8(sin8(time) + led.x - 128) * 2, 12), self.s, self.v))

    def fx_hue_wave(self, t, dt):
        return self.run_i(t, lambda i, led, time: (self.h + scale8(abs8(led.x - time), 24), self.s, self.v))

    def fx_raindrops(self, t, dt):
        if self.rain_colors is None:
            self.rain_colors = [self.raindrop() for _ in self.leds]
        if t - self.last_drop > max(20, 400 - self.speed):
            self.last_drop = t
            self.rain_colors[random.randrange(len(self.leds))] = self.raindrop()
        return list(self.rain_colors)

    def raindrop(self):
        return hsv_rgb(self.h + 32 * random.randrange(3), self.s, self.v)

    def fx_solid_reactive_simple(self, t, dt):
        return self.run_reactive(t, lambda off: (self.h, self.s, scale8(255 - min(off, 255), self.v)))

    def fx_solid_reactive(self, t, dt):
        return self.run_reactive(t, lambda off: (self.h + scale8(255 - min(off, 255), 64), self.s, self.v))

    @staticmethod
    def _wide(h, s, v, dx, dy, dist, tick):
        return h, s, qadd8(v, 255 - min(255, tick + dist * 5))

    @staticmethod
    def _cross(h, s, v, dx, dy, dist, tick):
        effect = tick + dist
        adx, ady = min(255, abs(dx) * 16), min(255, abs(dy) * 16)
        effect += min(adx, ady)
        return h, s, qadd8(v, 255 - min(255, effect))

    def _nexus(self, h, s, v, dx, dy, dist, tick):
        effect = tick - dist
        if effect < 0 or effect > 255 or dist > 72 or ((dx > 8 or dx < -8) and (dy > 8 or dy < -8)):
            effect = 255
        return self.h + dy // 4, s, qadd8(v, 255 - effect)

    @staticmethod
    def _splash(h, s, v, dx, dy, dist, tick):
        effect = tick - dist
        effect = 255 if effect < 0 or effect > 255 else effect
        return h + effect, s, qadd8(v, 255 - effect)

    @staticmethod
    def _solid_splash(h, s, v, dx, dy, dist, tick):
        effect = tick - dist
        effect = 255 if effect < 0 or effect > 255 else effect
        return h, s, qadd8(v, 255 - effect)

    def fx_solid_reactive_wide(self, t, dt):
        return self.run_splash(t, self._wide, False)

    def fx_solid_reactive_multiwide(self, t, dt):
        return self.run_splash(t, self._wide, True)

    def fx_solid_reactive_cross(self, t, dt):
        return self.run_splash(t, self._cross, False)

    def fx_solid_reactive_multicross(self, t, dt):
        return self.run_splash(t, self._cross, True)

    def fx_solid_reactive_nexus(self, t, dt):
        return self.run_splash(t, self._nexus, False)

    def fx_solid_reactive_multinexus(self, t, dt):
        return self.run_splash(t, self._nexus, True)

    def fx_splash(self, t, dt):
        return self.run_splash(t, self._splash, False)

    def fx_multisplash(self, t, dt):
        return self.run_splash(t, self._splash, True)

    def fx_solid_splash(self, t, dt):
        return self.run_splash(t, self._solid_splash, False)

    def fx_solid_multisplash(self, t, dt):
        return self.run_splash(t, self._solid_splash, True)

    def fx_digital_rain(self, t, dt):
        rows = {}
        for i, led in enumerate(self.leds):
            rows.setdefault(led.row, []).append(i)
        if t - self.last_drop > 90:
            self.last_drop = t
            for i, led in sorted(enumerate(self.leds), key=lambda e: -e[1].row):
                if self.drops[i] == 255:
                    self.drops[i] = 200
                    below = [j for j in rows.get(led.row + 1, []) if abs(self.leds[j].x - led.x) < 8]
                    if below:
                        self.drops[below[0]] = 255
            top = rows.get(min(rows), [])
            if top and random.random() < 0.5:
                self.drops[random.choice(top)] = 255
        out = []
        for i in range(len(self.leds)):
            d = self.drops[i]
            if d and d != 255:
                self.drops[i] = max(0, d - max(1, dt // 6))
            g = scale8(min(d, 255), self.v)
            out.append((scale8(g, 160), g, scale8(g, 160)) if d == 255 else (0, g, 0))
        return out

    def fx_typing_heatmap(self, t, dt):
        if t - self.last_decay >= 25:
            steps = (t - self.last_decay) // 25
            self.last_decay = t
            self.buf = [qsub8(b, steps) for b in self.buf]
        return [hsv_rgb(170 - qsub8(b, 85), self.s, scale8((qadd8(170, b) - 170) * 3, self.v)) for b in self.buf]

    # IO Type 84 Pro two-colour effects (rgb_matrix_kb.inc)

    def fx_two_tone(self, t, dt):
        c1, c2 = self.c1(), self.c2()
        return [c2 if led.modifier else c1 for led in self.leds]

    def fx_gradient_2(self, t, dt):
        c1, c2 = self.c1(), self.c2()
        shift = scale16by8(t, self.speed // 8 + 1)
        out = []
        for led in self.leds:
            pos = u8(led.x * 255 // 224 + shift)
            tt = pos * 2 if pos < 128 else (255 - pos) * 2
            out.append(lerp_rgb(c1, c2, tt))
        return out

    def fx_crossfade_2(self, t, dt):
        tt = sin8(scale16by8(t, self.speed // 4 + 1))
        return [lerp_rgb(self.c1(), self.c2(), tt)] * len(self.leds)

    def fx_scanner_2(self, t, dt):
        c1, c2 = self.c1(), self.c2()
        ph = u8(scale16by8(t, self.speed // 4 + 1))
        tri = ph * 2 if ph < 128 else (255 - ph) * 2
        pos = tri * 224 // 255
        out = []
        for led in self.leds:
            d = abs(led.x - pos)
            out.append(lerp_rgb(c1, c2, 0 if d >= 48 else 255 - d * 255 // 48))
        return out

    def fx_starlight_2(self, t, dt):
        c1, c2 = self.c1(), self.c2()
        spark = self.speed // 32 + 1
        for _ in range(max(1, dt // 16)):
            for i in range(len(self.leds)):
                if self.star[i]:
                    self.star[i] = qsub8(self.star[i], 3)
                elif random.randrange(256) < spark and random.randrange(256) < 64:
                    self.star[i] = 255
        return [lerp_rgb(c1, c2, s) for s in self.star]

    def fx_reactive_2(self, t, dt):
        c1, c2 = self.c1(), self.c2()
        max_tick = 65535 // qadd8(self.speed, 1)
        out = []
        for i in range(len(self.leds)):
            tick = max_tick
            for idx, _, _, th in reversed(self.hits):
                if idx == i and t - th < tick:
                    tick = t - th
                    break
            offset = scale16by8(min(tick, 65535), qadd8(self.speed, 1))
            out.append(lerp_rgb(c1, c2, 0 if offset > 255 else 255 - offset))
        return out

    def fx_ripple_2(self, t, dt):
        c1, c2 = self.c1(), self.c2()
        ticks = self.hit_ticks(t)
        out = []
        for led in self.leds:
            tt = 0
            for _, x, y, tick in ticks:
                if tick > 255 + 64:
                    continue
                dist = sqrt16((led.x - x) ** 2 + (led.y - y) ** 2)
                d = abs(tick - dist)
                fade = 0 if tick > 255 else 255 - tick
                if d < 24:
                    tt = qadd8(tt, scale8(255 - d * 10, fade))
            out.append(lerp_rgb(c1, c2, tt))
        return out


class StripPreview:
    """ The 9-LED light bar under the screen (strip.c) """

    LEDS = 9

    def __init__(self):
        self.mode = "WPM"
        self.color = (0, 255)
        self.color_black = False
        self.caps = False

    def base(self, v=255):
        return (0, 0, 0) if self.color_black else hsv_rgb(self.color[0], self.color[1], v)

    @staticmethod
    def dim(c, t):
        return tuple(scale8(x, t) for x in c)

    def bar(self, n256, color):
        out = []
        for i in range(self.LEDS):
            lo = i * 256
            t = 255 if n256 >= lo + 256 else (n256 - lo if n256 > lo else 0)
            out.append(self.dim(color, t))
        return out

    def frame(self, t):
        if self.caps:
            return [self.base()] * self.LEDS
        mode = effect_key(self.mode)
        wpm = int(70 + 60 * math.sin(t / 2300.0))
        if mode == "WPM":
            return [self.dim(self.base(), min(255, wpm * 255 // 200))] * self.LEDS
        if mode == "WPM_BAR":
            return self.bar(min(wpm, 108) * self.LEDS * 256 // 108, self.base())
        if mode == "BATTERY":
            return self.bar(80 * self.LEDS * 256 // 100, hsv_rgb(85, 255, 255))
        if mode == "LINK":
            return [hsv_rgb(128, 255, 160)] * self.LEDS
        if mode == "SCANNER":
            ph = (t // 3) % 512
            pos = ph if ph < 256 else 511 - ph
            x = pos * (self.LEDS - 1) * 16 // 255
            out = []
            for i in range(self.LEDS):
                d = abs(i * 16 - x)
                out.append(self.dim(self.base(), 0 if d >= 24 else 255 - d * 255 // 24))
            return out
        return [(0, 0, 0)] * self.LEDS
