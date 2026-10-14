# SPDX-License-Identifier: GPL-2.0-or-later
# Draws a HUD layout the way qmk-armed's quantum/armed/armed_hud.c does on the keyboard, pixel for pixel:
# the keyboard's own QFF fonts, Quantum Painter's colour maths and RGB565.
import struct

from PyQt5.QtGui import QImage

from hud_layout import W_TEXT, W_BAR, W_GRAPH, W_INDICATOR, W_BATTERY, W_FRAME, W_TEXTBOX, W_NONE, \
    FRAME_BRACKETS, FRAME_BOX, FRAME_HLINE, FRAME_VLINE, FRAME_PANEL, C_BG, C_PANEL, C_LINE, C_DIM, C_TEXT, C_ACCENT, \
    C_OK, C_WARN, C_AUTO, F_WARN, F_WARN_BELOW, F_FILL, F_TAIL, F_DOT, S_CHARGING, S_HOST_TXT

GRAPH_POINTS = 64

# the IO Type 84 Pro's palette with the amber accent, until the keyboard tells its own
DEFAULT_PALETTE = [(0, 0, 0), (0, 0, 16), (0, 0, 40), (0, 0, 90), (0, 0, 220), (28, 255, 255), (85, 200, 200), (0, 220, 230)]


def cdiv(a, b):
    """ C integer division: truncates toward zero """
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def hsv_to_rgb(h, s, v):
    """ quantum/color.c hsv_to_rgb_nocie """
    if s == 0:
        return v, v, v
    region = h * 6 // 255
    remainder = ((h * 2 - region * 85) * 3) & 0xFF
    p = (v * (255 - s)) >> 8
    q = (v * (255 - ((s * remainder) >> 8))) >> 8
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8
    return {0: (v, t, p), 6: (v, t, p), 1: (q, v, p), 2: (p, v, t), 3: (p, q, v), 4: (t, p, v)}.get(region, (v, p, q))


def panel_rgb(hsv):
    """ what the ST7789 shows: through RGB565 and back """
    r, g, b = hsv_to_rgb(*hsv)
    r, g, b = r >> 3, g >> 2, b >> 3
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def interpolate(fg, bg, steps):
    """ qp_internal_interpolate_palette: index 0 = background, steps - 1 = foreground """
    hf, hb = fg[0], bg[0]
    if hf - hb >= 128:
        hb += 256
    elif hf - hb <= -128:
        hb -= 256
    out = []
    for i in range(steps):
        out.append(((cdiv((hf - hb) * i, steps - 1) + hb) & 0xFF,
                    (cdiv((fg[1] - bg[1]) * i, steps - 1) + bg[1]) & 0xFF,
                    (cdiv((fg[2] - bg[2]) * i, steps - 1) + bg[2]) & 0xFF))
    return out


class QffFont:
    """ Quantum Painter font (docs/quantum_painter_qff.md): ASCII table, greyscale, optional RLE """

    def __init__(self, data):
        data = bytes(data)
        if len(data) < 25 or data[5:8] != b"QFF":
            raise ValueError("not a QFF font")
        (self.size,) = struct.unpack_from("<I", data, 9)
        self.line_height = data[17]
        self.has_ascii = bool(data[18])
        (self.num_unicode,) = struct.unpack_from("<H", data, 19)
        fmt = data[21]
        self.compressed = data[23] == 1
        if fmt > 3:
            raise ValueError("palette fonts are not supported")
        self.bpp = 1 << fmt
        self.data = data
        pos = 25
        self.ascii = {}
        if self.has_ascii:
            for i in range(95):
                v = int.from_bytes(data[pos + 5 + i * 3:pos + 8 + i * 3], "little")
                self.ascii[0x20 + i] = (v & 0x3F, v >> 6)
            pos += 5 + 285
        if self.num_unicode:
            for i in range(self.num_unicode):
                o = pos + 5 + i * 6
                cp = int.from_bytes(data[o:o + 3], "little")
                v = int.from_bytes(data[o + 3:o + 6], "little")
                self.ascii[cp] = (v & 0x3F, v >> 6)
            pos += 5 + 6 * self.num_unicode
        self.data_start = pos + 5
        self.cache = {}

    def width(self, ch):
        g = self.ascii.get(ord(ch))
        return g[0] if g else None

    def textwidth(self, s):
        w = 0
        for ch in s:
            gw = self.width(ch)
            if gw is None:
                return 0  # qp_textwidth fails as a whole
            w += gw
        return w

    def glyph(self, ch):
        """ list of rows of palette indices """
        if ch in self.cache:
            return self.cache[ch]
        w, off = self.ascii[ord(ch)]
        n = w * self.line_height
        need = (n * self.bpp + 7) // 8
        src = self.data[self.data_start + off:]
        if self.compressed:
            raw = bytearray()
            i = 0
            while len(raw) < need and i < len(src):
                m = src[i]
                i += 1
                if m >= 128:  # qp_draw_codec.c reads m - 127 bytes, not m - 128 as the RLE doc says
                    raw += src[i:i + m - 127]
                    i += m - 127
                else:
                    raw += bytes([src[i]]) * m
                    i += 1
        else:
            raw = src[:need]
        px = []
        mask = (1 << self.bpp) - 1
        per = 8 // self.bpp
        for k in range(n):
            byte = raw[k // per] if k // per < len(raw) else 0
            px.append((byte >> ((k % per) * self.bpp)) & mask)
        rows = [px[r * w:(r + 1) * w] for r in range(self.line_height)]
        self.cache[ch] = (w, rows)
        return self.cache[ch]


class Value:
    """ armed_hud_value_t """

    def __init__(self, num=0, dec=0, hint=C_TEXT, valid=True, text=""):
        self.num = num
        self.dec = dec
        self.hint = hint
        self.valid = valid
        self.text = text


class Sources:
    """ What the preview shows; the HUD page fills it from the keyboard and the agent """

    def value(self, source):
        return None  # None = the keyboard doesn't have this source

    def host_text(self, slot):
        return None

    def history(self, source):
        return []  # Values, oldest first, one per second


class Canvas:

    def __init__(self, w, h, palette):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 3)
        self.palette = palette
        self.rgb = [panel_rgb(c) for c in palette]

    # unknown roles draw as TEXT, as on the keyboard
    def role(self, r):
        return self.rgb[r if 0 <= r < len(self.rgb) else C_TEXT]

    def hsv(self, r):
        return self.palette[r if 0 <= r < len(self.palette) else C_TEXT]

    def put(self, x, y, rgb):
        if 0 <= x < self.w and 0 <= y < self.h:
            o = (y * self.w + x) * 3
            self.px[o:o + 3] = bytes(rgb)

    def fill(self, x0, y0, x1, y1, role):
        if x1 < x0 or y1 < y0:
            return
        self.fill_rgb(x0, y0, x1, y1, self.role(role))

    def fill_rgb(self, x0, y0, x1, y1, rgb):
        x0, x1 = max(0, min(x0, x1)), min(self.w - 1, max(x0, x1))
        y0, y1 = max(0, min(y0, y1)), min(self.h - 1, max(y0, y1))
        if x1 < x0 or y1 < y0:
            return
        row = bytes(rgb) * (x1 - x0 + 1)
        for y in range(y0, y1 + 1):
            o = (y * self.w + x0) * 3
            self.px[o:o + len(row)] = row

    def outline(self, x0, y0, x1, y1, role):
        l, r, t, b = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)
        self.fill(l, t, r, t, role)
        self.fill(l, b, r, b, role)
        self.fill(l, t + 1, l, b - 1, role)
        self.fill(r, t + 1, r, b - 1, role)

    def line(self, x0, y0, x1, y1, role):
        """ qp_line: Bresenham, straight lines as rectangles """
        if x0 == x1 or y0 == y1:
            self.fill(min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), role)
            return
        rgb = self.role(role)
        x, y = x0, y0
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        e = dx + dy
        while x != x1 or y != y1:
            self.put(x, y, rgb)
            e2 = 2 * e
            if e2 >= dy:
                e += dy
                x += sx
            if e2 <= dx:
                e += dx
                y += sy
        self.put(x, y, rgb)

    def text(self, x, y, font, s, fg, bg):
        if not s or font is None:
            return
        pal = [panel_rgb(c) for c in interpolate(self.hsv(fg), self.hsv(bg), 1 << font.bpp)]
        for ch in s:
            if font.width(ch) is None:
                return  # qp_drawtext stops at a missing glyph
            w, rows = font.glyph(ch)
            for r, row in enumerate(rows):
                for c, idx in enumerate(row):
                    self.put(x + c, y + r, pal[idx])
            x += w

    def image(self):
        img = QImage(bytes(self.px), self.w, self.h, self.w * 3, QImage.Format_RGB888)
        return img.copy()


def format_num(v, digits):
    a = abs(v.num)
    p = 10 ** v.dec
    s = "{}{:0{}d}".format("-" if v.num < 0 else "", a // p, max(1, digits))
    if v.dec:
        s += ".{:0{}d}".format(a % p, v.dec)
    return s


def value_text(v, digits):
    if v.text:
        return v.text[:23]
    if not v.valid:
        return "--"
    return format_num(v, digits)


class Renderer:

    def __init__(self, width, height, fonts, palette, sources):
        self.w, self.h = width, height
        self.fonts = fonts  # QffFont per font index
        self.palette = palette
        self.sources = sources
        self.names = {}  # source id -> name, for indicator labels

    def font(self, idx):
        if idx < len(self.fonts) and self.fonts[idx]:
            return self.fonts[idx]
        return next((f for f in self.fonts if f), None)

    def value(self, source):
        if source == 0:
            return None
        return self.sources.value(source)

    def area(self, wd):
        if wd.x < 0 or wd.y < 0 or wd.x >= self.w or wd.y >= self.h:
            return None
        f = self.font(wd.font)
        w = wd.w if wd.w > 0 else self.w - wd.x
        h = wd.h if wd.h > 0 else (f.line_height if f else 14)
        return wd.x, wd.y, min(wd.x + w, self.w) - 1, min(wd.y + h, self.h) - 1

    def render(self, layout, selected=None):
        cv = Canvas(self.w, self.h, self.palette)
        self.cv = cv
        cv.fill(0, 0, self.w - 1, self.h - 1, C_BG)
        for i, wd in enumerate(layout.widgets):
            if wd.type == W_NONE:
                continue
            if wd.is_static:
                self.draw(i, wd, None, False)
                continue
            v = self.value(wd.source) or Value(valid=False, hint=C_LINE)
            self.draw(i, wd, v, wd.bg != C_BG)
        return cv.image()

    def draw(self, i, wd, v, clear):
        b = self.area(wd)
        if b is None:
            return
        if clear and wd.type != W_FRAME:
            self.cv.fill(b[0], b[1], b[2], b[3], wd.bg)
        {W_TEXT: self.draw_text, W_BAR: self.draw_bar, W_GRAPH: self.draw_graph, W_INDICATOR: self.draw_indicator,
         W_BATTERY: self.draw_battery, W_FRAME: self.draw_frame, W_TEXTBOX: self.draw_textbox}.get(
            wd.type, lambda *a: None)(wd, b, v)

    # ---- helpers mirroring armed_hud.c

    def fit(self, f, s, width):
        w = f.textwidth(s)
        while s and w > width:
            s = s[:-1]
            w = f.textwidth(s)
        return s, w

    def aligned(self, wd, b, width):
        a = wd.flags & 3
        if a == 1:
            return b[0] + cdiv(b[2] - b[0] + 1 - width, 2)
        if a == 2:
            return b[2] + 1 - width
        return b[0]

    def fg_role(self, wd, v):
        r = wd.fg
        if r == C_AUTO:
            r = v.hint if v else C_TEXT
        if v and v.valid and wd.flags & F_WARN:
            if (v.num <= wd.warn) if wd.flags & F_WARN_BELOW else (v.num >= wd.warn):
                r = C_WARN
        return r

    def expand(self, wd, v):
        val = value_text(v, wd.param & 0x0F)[:23] if v else ""
        label = wd.label if wd.label is not None else "{}"
        p = label.find("{}")
        if p < 0:
            return label[:47]
        return (label[:p] + val + label[p + 2:])[:47]

    def draw_text(self, wd, b, v):
        f = self.font(wd.font)
        if f is None:
            return
        s, w = self.fit(f, self.expand(wd, v), b[2] - b[0] + 1)
        self.cv.text(self.aligned(wd, b, w), b[1], f, s, self.fg_role(wd, v), wd.bg)

    def level(self, wd, v):
        if not v.valid:
            return 0
        lo, hi = wd.lo, wd.hi
        if hi <= lo:
            lo, hi = 0, 100 * 10 ** v.dec
        r = cdiv((v.num - lo) * 1000, hi - lo)
        return max(0, min(1000, r))

    def draw_bar(self, wd, b, v):
        cv = self.cv
        cv.outline(b[0], b[1], b[2], b[3], C_LINE)
        x0, x1, y0, y1 = b[0] + 2, b[2] - 2, b[1] + 2, b[3] - 2
        iw = x1 - x0 + 1
        if iw <= 0 or y1 < y0:
            return
        l = self.level(wd, v)
        fg = self.fg_role(wd, v)
        if wd.param < 2:
            cv.fill(x0, y0, x0 + iw * l // 1000 - 1, y1, fg)
            return
        n = wd.param
        on = (l * n + 500) // 1000
        for i in range(n):
            sx0, sx1 = x0 + iw * i // n, x0 + iw * (i + 1) // n - 2
            cv.fill(sx0, y0, max(sx0, sx1), y1, fg if i < on else C_LINE)

    def draw_graph(self, wd, b, v):
        cv = self.cv
        w, h = b[2] - b[0] + 1, b[3] - b[1]
        cv.line(b[0], b[3], b[2], b[3], C_LINE)
        pts = min(GRAPH_POINTS, w // 2)
        if pts < 2:
            return
        samples = [self.level(wd, s) * 255 // 1000 for s in self.sources.history(wd.source)][-GRAPH_POINTS:]
        n = min(len(samples), pts)
        samples = samples[len(samples) - n:]
        fg = self.fg_role(wd, v)
        px = py = 0
        for k, sv in enumerate(samples):
            x = b[0] + (w - 1) * (pts - n + k) // (pts - 1)
            y = b[3] - 1 - (h - 1) * sv // 255
            if wd.flags & F_FILL:
                nx = b[0] + (w - 1) * (pts - n + k + 1) // (pts - 1) - 1 if k + 1 < n else x
                cv.fill(x, y, max(x, nx), b[3] - 1, fg)
            elif k:
                cv.line(px, py, x, y, fg)
            px, py = x, y

    def draw_indicator(self, wd, b, v):
        cv = self.cv
        on = bool(v and v.valid and v.num)
        if wd.flags & F_DOT:
            c = (v.hint if v else C_TEXT) if wd.fg == C_AUTO else (wd.fg if on else C_LINE)
            cv.fill(b[0], b[1], b[2], b[3], c)
            return
        bg = C_PANEL if on else wd.bg
        dot = (C_ACCENT if wd.fg == C_AUTO else wd.fg) if on else C_LINE
        h = b[3] - b[1] + 1
        cv.fill(b[0], b[1], b[2], b[3], bg)
        cv.outline(b[0], b[1], b[2], b[3], C_LINE)
        cv.fill(b[0] + 4, b[1] + cdiv(h - 6, 2), b[0] + 9, b[1] + cdiv(h - 6, 2) + 5, dot)
        f = self.font(wd.font)
        if f is None:
            return
        s = wd.label if wd.label is not None else self.names.get(wd.source, "")
        s, _ = self.fit(f, s[:23], b[2] - b[0] - 18)
        cv.text(b[0] + 16, b[1] + cdiv(h - f.line_height - 1, 2), f, s, C_TEXT if on else C_DIM, bg)

    def draw_battery(self, wd, b, v):
        cv = self.cv
        c = self.sources.value(S_CHARGING)
        chg = bool(c and c.num)
        pct = v.num if v and v.valid else 0
        fg = C_ACCENT if wd.fg == C_AUTO else wd.fg
        x, y = b[0], b[1]
        cv.outline(x, y, x + 22, y + 10, C_TEXT)
        cv.fill(x + 23, y + 3, x + 24, y + 7, C_TEXT)
        n = (pct + 10) // 20
        for i in range(5):
            r = C_LINE
            if i < n:
                r = C_OK if chg else (fg if pct > 15 else C_WARN)
            cv.fill(x + 2 + i * 4, y + 2, x + 4 + i * 4, y + 8, r)

    def draw_frame(self, wd, b, v):
        cv = self.cv
        c = C_LINE if wd.fg == C_AUTO else wd.fg
        x0, y0, x1, y1 = b
        if wd.param == FRAME_BRACKETS:
            l = wd.lo if wd.lo > 0 else 6
            for a in ((x0, y0, x0 + l, y0), (x0, y0, x0, y0 + l), (x1 - l, y0, x1, y0), (x1, y0, x1, y0 + l),
                      (x0, y1, x0 + l, y1), (x0, y1 - l, x0, y1), (x1 - l, y1, x1, y1), (x1, y1 - l, x1, y1)):
                cv.line(*a, c)
        elif wd.param == FRAME_BOX:
            cv.outline(x0, y0, x1, y1, c)
        elif wd.param == FRAME_HLINE:
            cv.line(x0, y0, x1, y0, c)
        elif wd.param == FRAME_VLINE:
            cv.line(x0, y0, x0, y1, c)
        elif wd.param == FRAME_PANEL:
            cv.fill(x0, y0, x1, y1, c)

    def draw_textbox(self, wd, b, v):
        src = None
        if S_HOST_TXT <= wd.source < S_HOST_TXT + 4 and v and v.valid:
            src = self.sources.host_text(wd.source - S_HOST_TXT) or ""
        elif v:
            src = v.text
        f = self.font(wd.font)
        if src is None or f is None:
            return
        src = src[:159]
        pitch = f.line_height + wd.param
        width = b[2] - b[0] + 1
        fit_n = min((b[3] - b[1] + 1 + wd.param) // max(pitch, 1), 16)
        tail = bool(wd.flags & F_TAIL)
        if not fit_n:
            return
        lines = []
        pos = 0
        while pos < len(src) and (tail or len(lines) < fit_n):
            n = cut = 0
            w = 0
            while pos + n < len(src) and src[pos + n] != "\n" and n < 63:
                w += f.width(src[pos + n]) or 0
                if w > width and n:
                    n = cut or n
                    break
                n += 1
                if src[pos + n - 1] == " ":
                    cut = n
            if len(lines) == fit_n:
                lines.pop(0)
            lines.append(src[pos:pos + n])
            pos += n
            if pos < len(src) and src[pos] == "\n":
                pos += 1
        fg = self.fg_role(wd, v)
        for i, line in enumerate(lines):
            s, w = self.fit(f, line, width)
            self.cv.text(self.aligned(wd, b, w), b[1] + i * pitch, f, s, fg, wd.bg)
