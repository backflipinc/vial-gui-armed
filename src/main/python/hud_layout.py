# SPDX-License-Identifier: GPL-2.0-or-later
# HUD layout of a qmk-armed keyboard: the main screen as a list of widgets.
# Byte layout matches armed_hud_layout_t in qmk-armed's quantum/armed/armed.h (docs/armed.md).
import json
import struct

FORMAT = 1
MAGIC = 0xB7
HEADER = 4
WIDGET = struct.Struct("<BBBBBBhhhhhhhBBH")
NO_LABEL = 0xFFFF

W_NONE, W_TEXT, W_BAR, W_GRAPH, W_INDICATOR, W_BATTERY, W_FRAME, W_TEXTBOX = range(8)
TYPE_NAMES = {W_TEXT: "TEXT", W_BAR: "BAR", W_GRAPH: "GRAPH", W_INDICATOR: "INDICATOR", W_BATTERY: "BATTERY",
              W_FRAME: "FRAME", W_TEXTBOX: "TEXT BOX"}

FRAME_BRACKETS, FRAME_BOX, FRAME_HLINE, FRAME_VLINE, FRAME_PANEL = range(5)
FRAME_NAMES = ("BRACKETS", "BOX", "H LINE", "V LINE", "PANEL")

C_BG, C_PANEL, C_LINE, C_DIM, C_TEXT, C_ACCENT, C_OK, C_WARN = range(8)
C_AUTO = 0x0F
COLOR_NAMES = {C_BG: "BG", C_PANEL: "PANEL", C_LINE: "LINE", C_DIM: "DIM", C_TEXT: "TEXT", C_ACCENT: "ACCENT",
               C_OK: "OK", C_WARN: "WARN", C_AUTO: "AUTO"}

F_ALIGN = 0x03
F_WARN = 0x04
F_WARN_BELOW = 0x08
F_FILL = 0x10
F_TAIL = 0x20
F_DOT = 0x40
ALIGN_NAMES = ("LEFT", "CENTER", "RIGHT")

S_NONE, S_LAYER, S_WPM, S_BATTERY, S_VOLTAGE, S_CHARGING, S_LINK, S_MODE, S_TIME, S_DATE, S_CAPS, S_NUM, S_SCROLL, \
    S_NKRO, S_LIGHT, S_GUI_LOCK, S_SNAPTAP, S_EFFECT, S_UPTIME, S_BRIGHTNESS = range(20)
S_HOST = 0x40
S_HOST_TXT = 0x60

K_TEXT, K_NUMBER, K_FLAG, K_LINES = range(4)

# what a fresh widget of each type looks like
NEW_WIDGET = {
    W_TEXT: dict(font=0, fg=C_TEXT, w=80, h=0, label="{}"),
    W_BAR: dict(fg=C_ACCENT, w=100, h=12),
    W_GRAPH: dict(fg=C_ACCENT, w=120, h=40, param=10),
    W_INDICATOR: dict(fg=C_ACCENT, w=99, h=20),
    W_BATTERY: dict(source=S_BATTERY, fg=C_ACCENT, w=25, h=11),
    W_FRAME: dict(fg=C_LINE, w=100, h=40, param=FRAME_BRACKETS),
    W_TEXTBOX: dict(fg=C_TEXT, w=200, h=60, flags=F_TAIL),
}


class Widget:

    FIELDS = ("type", "source", "font", "fg", "bg", "flags", "x", "y", "w", "h", "lo", "hi", "warn", "param", "label")

    def __init__(self, type=W_TEXT, source=S_NONE, font=0, fg=C_TEXT, bg=C_BG, flags=0, x=0, y=0, w=0, h=0,
                 lo=0, hi=0, warn=0, param=0, label=None):
        self.type = type
        self.source = source
        self.font = font
        self.fg = fg
        self.bg = bg
        self.flags = flags
        self.x, self.y, self.w, self.h = x, y, w, h
        self.lo, self.hi, self.warn = lo, hi, warn
        self.param = param
        self.label = label

    @classmethod
    def new(cls, type, x, y):
        w = cls(type=type, x=x, y=y)
        for k, v in NEW_WIDGET.get(type, {}).items():
            setattr(w, k, v)
        return w

    def copy(self):
        return Widget(**{k: getattr(self, k) for k in self.FIELDS})

    def __eq__(self, other):
        return isinstance(other, Widget) and all(getattr(self, k) == getattr(other, k) for k in self.FIELDS)

    @property
    def align(self):
        return self.flags & F_ALIGN

    def to_json(self):
        return {k: getattr(self, k) for k in self.FIELDS}

    @classmethod
    def from_json(cls, d):
        return cls(**{k: d[k] for k in cls.FIELDS if k in d})

    @property
    def is_static(self):
        return self.type == W_FRAME or (self.type == W_TEXT and self.source == S_NONE)


class LayoutError(Exception):
    pass


class Layout:

    def __init__(self, widgets=None):
        self.widgets = widgets or []

    def copy(self):
        return Layout([w.copy() for w in self.widgets])

    def __eq__(self, other):
        return isinstance(other, Layout) and self.widgets == other.widgets

    def pack(self, info):
        """ info: HudInfo of the keyboard (sizes) """
        if len(self.widgets) > info.max_widgets:
            raise LayoutError("too many widgets: {} of {}".format(len(self.widgets), info.max_widgets))
        strings = bytearray()
        offsets = {}
        body = bytearray()
        for w in self.widgets:
            label = NO_LABEL
            if w.label is not None:
                raw = w.label.encode("ascii", "replace")
                if raw not in offsets:
                    offsets[raw] = len(strings)
                    strings += raw + b"\x00"
                label = offsets[raw]
            body += WIDGET.pack(w.type, w.source, w.font, w.fg, w.bg, w.flags, w.x, w.y, w.w, w.h,
                                clamp16(w.lo), clamp16(w.hi), clamp16(w.warn), w.param & 0xFF, 0, label)
        if len(strings) > info.strings:
            raise LayoutError("labels too long: {} of {} bytes".format(len(strings), info.strings))
        body += b"\x00" * (WIDGET.size * (info.max_widgets - len(self.widgets)))
        strings += b"\x00" * (info.strings - len(strings))
        out = bytes([MAGIC, FORMAT, len(self.widgets), 0]) + bytes(body) + bytes(strings)
        assert len(out) == info.layout_size
        return out

    @classmethod
    def unpack(cls, blob, info):
        if len(blob) < info.layout_size or blob[0] != MAGIC or blob[1] != FORMAT or blob[2] > info.max_widgets:
            raise LayoutError("not a HUD layout")
        strings = blob[HEADER + WIDGET.size * info.max_widgets:info.layout_size]
        out = []
        for i in range(blob[2]):
            f = WIDGET.unpack_from(blob, HEADER + i * WIDGET.size)
            label = None
            if f[15] < len(strings):
                label = bytes(strings[f[15]:]).split(b"\x00")[0].decode("ascii", "replace")
            out.append(Widget(*f[:13], param=f[13], label=label))
        return cls(out)

    def to_json(self):
        return json.dumps({"vial-armed-hud": FORMAT, "widgets": [w.to_json() for w in self.widgets]}, indent=1)

    @classmethod
    def from_json(cls, text):
        d = json.loads(text)
        if d.get("vial-armed-hud") != FORMAT:
            raise LayoutError("not a Vial Armed HUD layout")
        return cls([Widget.from_json(w) for w in d["widgets"]])


def clamp16(v):
    return max(-32768, min(32767, int(v)))
