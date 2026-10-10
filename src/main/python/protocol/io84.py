# SPDX-License-Identifier: GPL-2.0-or-later
# IO Type 84 Pro raw HID channel (command 0x84): lighting and device settings beyond VIA / VialRGB
import logging
import struct

CMD_IO84 = 0x84
IO84_PROTOCOL = 1

SUB_INFO, SUB_LIST, SUB_GET, SUB_SET, SUB_SAVE = range(5)
L_EFFECTS, L_COLORS, L_STRIP_MODES, L_SLEEP, L_LEDS = range(5)

V_ENABLED, V_EFFECT, V_COLOR2, V_FN_COLOR, V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS, V_SLEEP, \
    V_HUE, V_SAT, V_VAL, V_SPEED = range(12)

UNHANDLED = 0xFF


def _cstr(data):
    return bytes(data).split(b"\x00")[0].decode("ascii", "replace")


class IO84Effect:

    def __init__(self, qmk_id, name):
        self.qmk_id = qmk_id
        self.name = name


class IO84Color:

    def __init__(self, h, s, name):
        self.h = h
        self.s = s
        self.name = name

    @property
    def black(self):
        return self.h == 0 and self.s == 1


class IO84Led:

    def __init__(self, row, col, x, y, flags):
        self.row = row
        self.col = col
        self.x = x
        self.y = y
        self.flags = flags

    @property
    def modifier(self):
        return bool(self.flags & 0x01)


class IO84:
    """ Settings of the IO Type 84 Pro QMK firmware not covered by Vial """

    def __init__(self, keyboard):
        self.keyboard = keyboard
        self.effects = []
        self.colors = []
        self.strip_modes = []
        self.sleep_minutes = []
        self.leds = []
        self.max_brightness = 255
        self.values = dict()

    def _send(self, *payload):
        msg = struct.pack("B" * (len(payload) + 1), CMD_IO84, *payload)
        return self.keyboard.usb_send(self.keyboard.dev, msg, retries=20)

    @classmethod
    def probe(cls, keyboard):
        """ Returns an IO84 instance if the keyboard speaks the protocol, otherwise None """
        if keyboard.vial_protocol < 0:
            return None
        io = cls(keyboard)
        try:
            data = io._send(SUB_INFO)
        except Exception as e:
            logging.warning("IO84 probe failed: %s", e)
            return None
        if data[0] != CMD_IO84 or bytes(data[2:6]) != b"IO84" or data[6] != IO84_PROTOCOL:
            return None
        counts = data[7:12]
        io.max_brightness = data[12] or 255
        io.load(counts)
        return io

    def _list(self, kind, count):
        out = []
        for idx in range(count):
            data = self._send(SUB_LIST, kind, idx)
            if data[0] == UNHANDLED:
                break
            out.append(data[4:])
        return out

    def load(self, counts):
        self.effects = [IO84Effect(d[0], _cstr(d[1:])) for d in self._list(L_EFFECTS, counts[L_EFFECTS])]
        self.colors = [IO84Color(d[0], d[1], _cstr(d[2:])) for d in self._list(L_COLORS, counts[L_COLORS])]
        self.strip_modes = [_cstr(d) for d in self._list(L_STRIP_MODES, counts[L_STRIP_MODES])]
        self.sleep_minutes = [d[0] for d in self._list(L_SLEEP, counts[L_SLEEP])]
        self.leds = [IO84Led(d[0], d[1], d[2], d[3], d[4]) for d in self._list(L_LEDS, counts[L_LEDS])]
        self.reload_values()

    def reload_values(self):
        for vid in range(V_SPEED + 1):
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
