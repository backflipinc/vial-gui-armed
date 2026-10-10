# SPDX-License-Identifier: GPL-2.0-or-later
# qmk-armed protocol (command 0xAD): settings VIA / Vial don't cover, reported per keyboard
import logging
import struct

CMD_ARMED = 0xAD
ARMED_PROTOCOL = 1

SUB_INFO, SUB_LIST, SUB_GET, SUB_SET, SUB_SAVE = range(5)
L_EFFECTS, L_COLORS, L_STRIP_MODES, L_SLEEP, L_LEDS = range(5)
L_COUNT = 5

V_ENABLED, V_EFFECT, V_COLOR2, V_FN_COLOR, V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS, V_SLEEP, \
    V_HUE, V_SAT, V_VAL, V_SPEED = range(12)

F_RGB_MATRIX = 1 << 0
F_COLOR2 = 1 << 1
F_FN_COLOR = 1 << 2
F_STRIP = 1 << 3
F_SLEEP = 1 << 4

FEATURE_VALUES = {
    F_RGB_MATRIX: (V_ENABLED, V_EFFECT, V_HUE, V_SAT, V_VAL, V_SPEED),
    F_COLOR2: (V_COLOR2,),
    F_FN_COLOR: (V_FN_COLOR,),
    F_STRIP: (V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS),
    F_SLEEP: (V_SLEEP,),
}

EFFECT_COLOR2 = 0x01

UNHANDLED = 0xFF


def _cstr(data):
    return bytes(data).split(b"\x00")[0].decode("ascii", "replace")


class ArmedEffect:

    def __init__(self, qmk_id, flags, name):
        self.qmk_id = qmk_id
        self.flags = flags
        self.name = name

    @property
    def uses_color2(self):
        return bool(self.flags & EFFECT_COLOR2)


class ArmedColor:

    def __init__(self, h, s, name):
        self.h = h
        self.s = s
        self.name = name

    @property
    def black(self):
        return self.h == 0 and self.s == 1


class ArmedLed:

    def __init__(self, row, col, x, y, flags):
        self.row = row
        self.col = col
        self.x = x
        self.y = y
        self.flags = flags

    @property
    def modifier(self):
        return bool(self.flags & 0x01)


class QmkArmed:
    """ Keyboard running qmk-armed firmware; features tell which settings it has """

    def __init__(self, keyboard):
        self.keyboard = keyboard
        self.version = (0, 0, 0)
        self.features = 0
        self.effects = []
        self.colors = []
        self.strip_modes = []
        self.sleep_minutes = []
        self.leds = []
        self.max_brightness = 255
        self.values = dict()

    def _send(self, *payload):
        msg = struct.pack("B" * (len(payload) + 1), CMD_ARMED, *payload)
        return self.keyboard.usb_send(self.keyboard.dev, msg, retries=20)

    @classmethod
    def probe(cls, keyboard):
        """ Returns a QmkArmed instance if the firmware speaks the protocol, otherwise None """
        if keyboard.vial_protocol < 0:
            return None
        armed = cls(keyboard)
        try:
            data = armed._send(SUB_INFO)
        except Exception as e:
            logging.warning("qmk-armed probe failed: %s", e)
            return None
        if data[0] != CMD_ARMED or bytes(data[2:6]) != b"ARMD" or data[6] != ARMED_PROTOCOL:
            return None
        armed.version = tuple(data[7:10])
        armed.features = data[10] | (data[11] << 8)
        armed.max_brightness = data[12 + L_COUNT] or 255
        armed.load(data[12:12 + L_COUNT])
        return armed

    def has(self, feature):
        return bool(self.features & feature)

    @property
    def version_str(self):
        return "{}.{}.{}".format(*self.version)

    def _list(self, kind, count):
        out = []
        for idx in range(count):
            data = self._send(SUB_LIST, kind, idx)
            if data[0] == UNHANDLED:
                break
            out.append(data[4:])
        return out

    def load(self, counts):
        self.effects = [ArmedEffect(d[0], d[1], _cstr(d[2:])) for d in self._list(L_EFFECTS, counts[L_EFFECTS])]
        self.colors = [ArmedColor(d[0], d[1], _cstr(d[2:])) for d in self._list(L_COLORS, counts[L_COLORS])]
        self.strip_modes = [_cstr(d) for d in self._list(L_STRIP_MODES, counts[L_STRIP_MODES])]
        self.sleep_minutes = [d[0] for d in self._list(L_SLEEP, counts[L_SLEEP])]
        self.leds = [ArmedLed(d[0], d[1], d[2], d[3], d[4]) for d in self._list(L_LEDS, counts[L_LEDS])]
        self.reload_values()

    def reload_values(self):
        for feature, vids in FEATURE_VALUES.items():
            if not self.has(feature):
                continue
            for vid in vids:
                data = self._send(SUB_GET, vid)
                if data[0] != UNHANDLED:
                    self.values[vid] = data[3]

    def get(self, vid):
        return self.values.get(vid, 0)

    def set(self, vid, value):
        value = int(value) & 0xFF
        self.values[vid] = value
        self._send(SUB_SET, vid, value)

    def save(self):
        """ Persists HSV and speed, which are applied without EEPROM writes """
        self._send(SUB_SAVE)
