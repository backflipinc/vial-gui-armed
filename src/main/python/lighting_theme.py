# SPDX-License-Identifier: GPL-2.0-or-later
# qmk-armed lighting themes: the model, its wire format (docs/armed.md in qmk-armed), presets and key groups
import copy
import json
import struct

MAGIC = 0xA7
FORMAT = 1
NAME_LEN = 16
HEADER = 20
LAYER_HEAD = 20

SOLID, GRADIENT, RAINBOW, PULSE, WAVE, SPARKLE, REACTIVE, RIPPLE, PAINT = range(1, 10)
NORMAL, ADD, MULTIPLY = range(3)
F_RADIAL, F_MIRROR, F_REVERSE, F_HIDDEN = 0x01, 0x02, 0x04, 0x80
C_ALWAYS, C_CAPS, C_NO_CAPS, C_LAYER, C_NOT_LAYER = 0x00, 0x01, 0x02, 0x10, 0x20


class LayerType:

    def __init__(self, tid, name, hint, controls, defaults):
        self.id = tid
        self.name = name
        self.hint = hint
        self.controls = controls
        self.defaults = defaults


# controls: a, b = colours; dir = direction / radial; speed, size, param; mirror
TYPES = [
    LayerType(SOLID, "SOLID", "One colour", {"a"}, dict(a=(255, 170, 0))),
    LayerType(GRADIENT, "GRADIENT", "Colour A fades into colour B across the keys",
              {"a", "b", "dir", "speed", "size", "mirror"},
              dict(a=(255, 170, 0), b=(255, 0, 60), flags=F_MIRROR)),
    LayerType(RAINBOW, "RAINBOW", "Every hue, spread across the keys", {"dir", "speed", "size", "param"},
              dict(speed=24, param=255)),
    LayerType(PULSE, "PULSE", "Breathes between colour A and colour B", {"a", "b", "dir", "speed", "size"},
              dict(a=(255, 170, 0), b=(0, 0, 0), speed=40, size=0)),
    LayerType(WAVE, "WAVE", "A band of colour sweeps over the layers below", {"a", "dir", "speed", "size", "mirror"},
              dict(a=(255, 255, 255), speed=60, size=60)),
    LayerType(SPARKLE, "SPARKLE", "Random keys flash, in colours between A and B", {"a", "b", "speed", "size"},
              dict(a=(255, 255, 255), b=(255, 170, 0), speed=60, size=128)),
    LayerType(REACTIVE, "REACTIVE", "Keys light up when pressed and fade out", {"a", "size", "param"},
              dict(a=(255, 170, 0), size=64, param=0)),
    LayerType(RIPPLE, "RIPPLE", "Rings spread from every key press", {"a", "speed", "size"},
              dict(a=(0, 200, 255), speed=100, size=64)),
    LayerType(PAINT, "PAINT", "Your own colour on every key: pick a colour and click keys", {"paint"}, dict()),
]
TYPE_BY_ID = {t.id: t for t in TYPES}

BLENDS = ["NORMAL", "ADD", "MULTIPLY"]


class Layer:

    def __init__(self, ltype=SOLID, leds=()):
        self.type = ltype
        self.blend = NORMAL
        self.opacity = 255
        self.speed = 0
        self.dir = 0
        self.size = 128
        self.flags = 0
        self.cond = C_ALWAYS
        self.a = (255, 170, 0)
        self.b = (0, 0, 0)
        self.param = 255
        self.cx, self.cy = 112, 32
        self.mask = set(leds)
        for k, v in TYPE_BY_ID[ltype].defaults.items():
            setattr(self, k, v)

    @property
    def hidden(self):
        return bool(self.flags & F_HIDDEN)

    def flag(self, f):
        return bool(self.flags & f)

    def set_flag(self, f, on):
        self.flags = (self.flags | f) if on else (self.flags & ~f)

    def pack(self, n_leds):
        mask = bytearray((n_leds + 7) // 8)
        for i in self.mask:
            if 0 <= i < n_leds:
                mask[i // 8] |= 1 << (i % 8)
        return struct.pack("<8B3B3B3B3x", self.type, self.blend, self.opacity, self.speed, self.dir, self.size,
                           self.flags, self.cond, *self.a, *self.b, self.param, self.cx, self.cy) + bytes(mask)

    @classmethod
    def unpack(cls, data, n_leds):
        layer = cls.__new__(cls)
        (layer.type, layer.blend, layer.opacity, layer.speed, layer.dir, layer.size, layer.flags, layer.cond) = data[:8]
        layer.a, layer.b = tuple(data[8:11]), tuple(data[11:14])
        layer.param, layer.cx, layer.cy = data[14:17]
        mask = data[LAYER_HEAD:]
        layer.mask = {i for i in range(n_leds) if mask[i // 8] & (1 << (i % 8))}
        return layer

    def to_json(self, leds):
        t = TYPE_BY_ID.get(self.type)
        return {
            "type": t.name.lower() if t else self.type, "blend": BLENDS[self.blend].lower() if self.blend < 3 else 0,
            "opacity": self.opacity, "speed": self.speed, "direction": self.dir, "size": self.size,
            "param": self.param, "center": [self.cx, self.cy], "cond": self.cond,
            "radial": self.flag(F_RADIAL), "mirror": self.flag(F_MIRROR), "reverse": self.flag(F_REVERSE),
            "hidden": self.hidden, "a": hex_color(self.a), "b": hex_color(self.b),
            "keys": sorted(key_name(leds, i) for i in self.mask if i < len(leds)),
        }

    @classmethod
    def from_json(cls, d, leds):
        by_name = {t.name.lower(): t.id for t in TYPES}
        layer = cls(by_name.get(d.get("type"), SOLID))
        blend = d.get("blend", "normal")
        layer.blend = BLENDS.index(blend.upper()) if isinstance(blend, str) and blend.upper() in BLENDS else NORMAL
        for key, attr in (("opacity", "opacity"), ("speed", "speed"), ("direction", "dir"), ("size", "size"),
                          ("param", "param"), ("cond", "cond")):
            if key in d:
                setattr(layer, attr, int(d[key]) & 0xFF)
        if "center" in d:
            layer.cx, layer.cy = (int(v) & 0xFF for v in d["center"][:2])
        layer.flags = 0
        for key, f in (("radial", F_RADIAL), ("mirror", F_MIRROR), ("reverse", F_REVERSE), ("hidden", F_HIDDEN)):
            layer.set_flag(f, bool(d.get(key)))
        layer.a = parse_color(d.get("a"), layer.a)
        layer.b = parse_color(d.get("b"), layer.b)
        names = {key_name(leds, i): i for i in range(len(leds))}
        layer.mask = {names[k] for k in d.get("keys", []) if k in names}
        return layer


class Theme:

    def __init__(self, name="THEME", n_leds=0):
        self.name = name
        self.layers = []
        self.paint = [(0, 0, 0)] * n_leds

    def copy(self):
        return copy.deepcopy(self)

    def pack(self, n_leds, max_layers):
        name = self.name.encode("ascii", "replace")[:NAME_LEN - 1].ljust(NAME_LEN, b"\x00")
        layer_size = LAYER_HEAD + (n_leds + 7) // 8
        out = bytearray(struct.pack("<4B", MAGIC, FORMAT, min(len(self.layers), max_layers), 0) + name)
        for i in range(max_layers):
            out += self.layers[i].pack(n_leds) if i < len(self.layers) else bytes(layer_size)
        for i in range(n_leds):
            out += bytes(self.paint[i] if i < len(self.paint) else (0, 0, 0))
        return bytes(out)

    @classmethod
    def unpack(cls, data, n_leds, max_layers):
        theme = cls(n_leds=n_leds)
        if data[0] != MAGIC or data[1] != FORMAT:
            return theme
        theme.name = data[4:4 + NAME_LEN].split(b"\x00")[0].decode("ascii", "replace")
        layer_size = LAYER_HEAD + (n_leds + 7) // 8
        for i in range(min(data[2], max_layers)):
            off = HEADER + i * layer_size
            theme.layers.append(Layer.unpack(data[off:off + layer_size], n_leds))
        off = HEADER + max_layers * layer_size
        theme.paint = [tuple(data[off + i * 3:off + i * 3 + 3]) for i in range(n_leds)]
        return theme

    def to_json(self, leds):
        return json.dumps({
            "vial_armed_theme": FORMAT, "name": self.name,
            "layers": [layer.to_json(leds) for layer in self.layers],
            "paint": {key_name(leds, i): hex_color(c) for i, c in enumerate(self.paint) if c != (0, 0, 0)},
        }, indent=2)

    @classmethod
    def from_json(cls, text, leds, max_layers):
        d = json.loads(text)
        if "vial_armed_theme" not in d:
            raise ValueError("not a Vial Armed theme")
        theme = cls(str(d.get("name", "THEME"))[:NAME_LEN - 1], len(leds))
        theme.layers = [Layer.from_json(ld, leds) for ld in d.get("layers", [])][:max_layers]
        names = {key_name(leds, i): i for i in range(len(leds))}
        for k, c in d.get("paint", {}).items():
            if k in names:
                theme.paint[names[k]] = parse_color(c, (0, 0, 0))
        return theme


def hex_color(c):
    return "#{:02x}{:02x}{:02x}".format(*c)


def parse_color(s, default):
    try:
        s = s.lstrip("#")
        return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))
    except (AttributeError, ValueError, IndexError):
        return default


def key_name(leds, i):
    led = leds[i]
    return "{},{}".format(led.row, led.col) if led.row != 0xFF else "led{}".format(i)


LETTERS = {"KC_" + c for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}
NUMBERS = {"KC_" + c for c in "1234567890"} | {"KC_GRAVE", "KC_GRV", "KC_MINUS", "KC_MINS", "KC_EQUAL", "KC_EQL"}
FKEYS = {"KC_F{}".format(i) for i in range(1, 25)} | {"KC_ESCAPE", "KC_ESC"}
ARROWS = {"KC_UP", "KC_DOWN", "KC_LEFT", "KC_RIGHT", "KC_RGHT"}
WASD = {"KC_W", "KC_A", "KC_S", "KC_D"}

GROUPS = ["ALL", "LETTERS", "NUMBERS", "F-ROW", "MODS", "ARROWS", "WASD", "FN LAYER"]


def key_groups(leds, layout):
    def by_code(codes):
        return {i for i, led in enumerate(leds) if layout.get((0, led.row, led.col)) in codes}

    return {
        "ALL": set(range(len(leds))),
        "LETTERS": by_code(LETTERS),
        "NUMBERS": by_code(NUMBERS),
        "F-ROW": by_code(FKEYS),
        "MODS": {i for i, led in enumerate(leds) if led.modifier},
        "ARROWS": by_code(ARROWS),
        "WASD": by_code(WASD),
        "FN LAYER": fn_layer_keys(leds, layout),
    }


def fn_layer_keys(leds, layout, layer=1):
    """ Keys bound on the Fn layer, plus the key that holds it """
    hold = "MO({})".format(layer)
    return {i for i, led in enumerate(leds)
            if layout.get((layer, led.row, led.col)) not in (None, "KC_TRNS", "KC_NO")
            or layout.get((0, led.row, led.col)) == hold}


def _layer(ltype, keys="ALL", **kw):
    return (ltype, keys, kw)


PRESETS = [
    ("AMBER DRIFT", [_layer(GRADIENT, a=(255, 170, 0), b=(255, 40, 0), speed=16, flags=F_MIRROR)], {}),
    ("DEEP SEA", [_layer(GRADIENT, a=(0, 200, 255), b=(0, 20, 160), dir=64, speed=12, flags=F_MIRROR),
                  _layer(SPARKLE, a=(255, 255, 255), b=(120, 255, 255), speed=60)], {}),
    ("SHOCKWAVE", [_layer(SOLID, a=(20, 0, 60)), _layer(RIPPLE, a=(255, 170, 0), speed=100, size=96)], {}),
    ("SPECTRUM", [_layer(RAINBOW, speed=24)], {}),
    ("GAMER WASD", [_layer(SOLID, a=(30, 0, 0)), _layer(SOLID, "ARROWS", a=(255, 60, 0)),
                    _layer(PULSE, "WASD", a=(255, 0, 0), b=(255, 170, 0), speed=50),
                    _layer(REACTIVE, a=(255, 255, 255), size=40)], {}),
    ("TYPEWRITER", [_layer(SOLID, a=(255, 140, 40)), _layer(SOLID, "MODS", a=(120, 40, 0)),
                    _layer(REACTIVE, a=(255, 255, 255), size=80, param=24)], {}),
    ("AURORA", [_layer(GRADIENT, a=(0, 255, 120), b=(120, 0, 255), dir=32, speed=10, size=96, flags=F_MIRROR),
                _layer(WAVE, a=(255, 255, 255), dir=0, speed=20, size=40, opacity=110, blend=ADD)], {}),
    ("RADAR", [_layer(SOLID, a=(0, 20, 0)),
               _layer(WAVE, a=(0, 255, 60), speed=40, size=50, flags=F_RADIAL)], {}),
    ("CAPS ALERT", [_layer(GRADIENT, a=(0, 120, 255), b=(150, 0, 255), speed=14, flags=F_MIRROR),
                    _layer(PULSE, a=(255, 0, 0), b=(60, 0, 0), speed=90, cond=C_CAPS)], {}),
    ("FN MAP", [_layer(GRADIENT, a=(0, 120, 255), b=(0, 255, 160), speed=12, flags=F_MIRROR),
                _layer(SOLID, a=(0, 0, 0), opacity=220, cond=C_LAYER | 1),
                _layer(PULSE, "FN LAYER", a=(255, 170, 0), b=(255, 255, 255), speed=60, cond=C_LAYER | 1)], {}),
    ("BLANK", [], {}),
]


def build_preset(index, leds, layout):
    name, layers, _ = PRESETS[index]
    groups = key_groups(leds, layout)
    theme = Theme(name, len(leds))
    for ltype, keys, kw in layers:
        layer = Layer(ltype, groups.get(keys, groups["ALL"]))
        for k, v in kw.items():
            setattr(layer, k, v)
        theme.layers.append(layer)
    return theme
