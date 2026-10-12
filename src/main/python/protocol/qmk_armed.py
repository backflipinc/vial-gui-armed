# SPDX-License-Identifier: GPL-2.0-or-later
# qmk-armed protocol (command 0xAD): settings VIA / Vial don't cover, reported per keyboard
import logging
import struct
from datetime import datetime, timedelta

CMD_ARMED = 0xAD
ARMED_PROTOCOL = 1

SUB_INFO, SUB_LIST, SUB_GET, SUB_SET, SUB_SAVE, \
    SUB_SCREEN_INFO, SUB_SCREEN_BEGIN, SUB_SCREEN_DATA, SUB_SCREEN_END, SUB_SCREEN_READ, \
    SUB_THEME_INFO, SUB_THEME_READ, SUB_THEME_WRITE, SUB_THEME_SAVE, SUB_THEME_REVERT, \
    SUB_CLOCK_GET, SUB_CLOCK_SET = range(17)
L_EFFECTS, L_COLORS, L_STRIP_MODES, L_SLEEP, L_LEDS = range(5)
L_COUNT = 5

V_ENABLED, V_EFFECT, V_COLOR2, V_FN_COLOR, V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS, V_SLEEP, \
    V_HUE, V_SAT, V_VAL, V_SPEED, V_SCREEN, V_CLOCK = range(14)

F_RGB_MATRIX = 1 << 0
F_COLOR2 = 1 << 1
F_FN_COLOR = 1 << 2
F_STRIP = 1 << 3
F_SLEEP = 1 << 4
F_SCREEN = 1 << 5
F_THEMES = 1 << 6
F_CLOCK = 1 << 7

FEATURE_VALUES = {
    F_RGB_MATRIX: (V_ENABLED, V_EFFECT, V_HUE, V_SAT, V_VAL, V_SPEED),
    F_COLOR2: (V_COLOR2,),
    F_FN_COLOR: (V_FN_COLOR,),
    F_STRIP: (V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS),
    F_SLEEP: (V_SLEEP,),
    F_SCREEN: (V_SCREEN,),
    F_CLOCK: (V_CLOCK,),
}

EFFECT_COLOR2 = 0x01
EFFECT_THEME = 0x02

UNHANDLED = 0xFF

MSG_LEN = 32
SCREEN_DATA_PAYLOAD = MSG_LEN - 4
SCREEN_READ_PAYLOAD = MSG_LEN - 7
SCREEN_WINDOW = 8  # requests in flight during an upload / read
THEME_PAYLOAD = MSG_LEN - 6


class ArmedError(Exception):
    pass


class ScreenInfo:

    def __init__(self, data):
        self.width, self.height, self.format, self.max_frames, self.frames, self.capacity, self.header = \
            struct.unpack_from("<HHBBBIH", data, 2)

    @property
    def frame_bytes(self):
        return self.width * self.height * 2


class ThemeInfo:

    def __init__(self, data):
        self.slots, self.layers, self.leds, self.size, self.format = struct.unpack_from("<BBBHB", data, 2)
        self.modes = list(data[8:8 + self.slots])


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

    @property
    def theme(self):
        return bool(self.flags & EFFECT_THEME)


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
        self.themes = None  # Theme per slot once read, shared by the Lighting and Themes pages

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

    # ---- clock (F_CLOCK): local wall time in whole seconds, the keyboard knows no time zones

    def clock_get(self):
        """ Keyboard's local time as a naive datetime, None if it was never set """
        data = self._send(SUB_CLOCK_GET)
        if data[0] == UNHANDLED or not data[2]:
            return None
        return datetime(1970, 1, 1) + timedelta(seconds=struct.unpack_from("<I", data, 3)[0])

    def clock_set(self, when):
        secs = int(round((when - datetime(1970, 1, 1)).total_seconds()))
        if self._send(SUB_CLOCK_SET, *struct.pack("<I", secs))[0] == UNHANDLED:
            raise ArmedError("clock not set")

    # ---- screen animation (F_SCREEN), see docs/armed.md in qmk-armed

    def screen_info(self):
        data = self._send(SUB_SCREEN_INFO)
        return None if data[0] == UNHANDLED else ScreenInfo(data)

    def _write(self, msg):
        self.keyboard.dev.write(b"\x00" + msg.ljust(MSG_LEN, b"\x00"))

    def _read(self, timeout_ms):
        data = bytes(self.keyboard.dev.read(MSG_LEN, timeout_ms=timeout_ms))
        if not data:
            raise ArmedError("the keyboard did not answer")
        return data

    def _pipeline(self, requests, on_reply, cancelled, timeout_ms):
        """ Sends requests with up to SCREEN_WINDOW in flight; replies come back in order """
        sent = 0
        pending = 0
        try:
            for i in range(len(requests)):
                while sent < len(requests) and pending < SCREEN_WINDOW:
                    self._write(requests[sent])
                    sent += 1
                    pending += 1
                reply = self._read(timeout_ms)
                pending -= 1
                on_reply(i, reply)
                if cancelled():
                    raise ArmedError("cancelled")
        except BaseException:
            for _ in range(pending):  # leave nothing in flight for the next command
                try:
                    self._read(2000)
                except Exception:
                    break
            raise

    def screen_read(self, offset, size, progress=lambda done, total: None, cancelled=lambda: False):
        reqs = [(o, min(SCREEN_READ_PAYLOAD, offset + size - o)) for o in range(offset, offset + size, SCREEN_READ_PAYLOAD)]
        out = bytearray()

        def reply(i, data):
            o, n = reqs[i]
            if data[0] != CMD_ARMED or data[1] != SUB_SCREEN_READ or struct.unpack_from("<I", data, 2)[0] != o:
                raise ArmedError("read failed at byte {}".format(o))
            out.extend(data[7:7 + n])
            progress(len(out), size)

        self._pipeline([struct.pack("<BBIB", CMD_ARMED, SUB_SCREEN_READ, o, n) for o, n in reqs], reply, cancelled, 5000)
        return bytes(out)

    def screen_upload(self, size, stream, progress=lambda done, total: None, cancelled=lambda: False):
        """ size: bytes the keyboard stores; stream: the encoded animation (screen_media.encode) """
        data = self._send(SUB_SCREEN_BEGIN, *struct.pack("<I", size))
        if data[0] == UNHANDLED:
            raise ArmedError("the keyboard refused the upload")
        chunks = [stream[i:i + SCREEN_DATA_PAYLOAD] for i in range(0, len(stream), SCREEN_DATA_PAYLOAD)]

        def reply(i, data):
            if data[1] != SUB_SCREEN_DATA or data[2] != i & 0xFF or data[3] != 0:
                raise ArmedError("upload failed at byte {}".format(struct.unpack_from("<I", data, 4)[0]))
            progress(struct.unpack_from("<I", data, 4)[0], size)

        try:
            # a 64 KiB flash erase can hold a reply for a second or two
            self._pipeline([struct.pack("<BBBB", CMD_ARMED, SUB_SCREEN_DATA, i & 0xFF, len(c)) + c
                            for i, c in enumerate(chunks)], reply, cancelled, 10000)
        except BaseException:
            self._send(SUB_SCREEN_END, 0)
            raise
        if self._send(SUB_SCREEN_END, 1)[0] == UNHANDLED:
            raise ArmedError("the keyboard rejected the finished upload")

    # ---- lighting themes (F_THEMES), see docs/armed.md in qmk-armed

    def theme_info(self):
        data = self._send(SUB_THEME_INFO)
        return None if data[0] == UNHANDLED else ThemeInfo(data)

    def load_themes(self):
        from lighting_theme import Theme
        info = self.theme_info()
        if info is None:
            return None
        self.themes = [Theme.unpack(self.theme_read(s, info.size), info.leds, info.layers) for s in range(info.slots)]
        self.theme_modes = info.modes
        return info

    def theme_read(self, slot, size):
        out = bytearray()
        for off in range(0, size, THEME_PAYLOAD):
            n = min(THEME_PAYLOAD, size - off)
            data = self._send(SUB_THEME_READ, slot, off & 0xFF, off >> 8, n)
            if data[0] == UNHANDLED:
                raise ArmedError("theme read failed at byte {}".format(off))
            out.extend(data[6:6 + n])
        return bytes(out)

    def theme_write(self, slot, off, payload):
        for i in range(0, len(payload), THEME_PAYLOAD):
            chunk = payload[i:i + THEME_PAYLOAD]
            o = off + i
            if self._send(SUB_THEME_WRITE, slot, o & 0xFF, o >> 8, len(chunk), *chunk)[0] == UNHANDLED:
                raise ArmedError("theme write failed at byte {}".format(o))

    def theme_save(self, slot):
        if self._send(SUB_THEME_SAVE, slot)[0] == UNHANDLED:
            raise ArmedError("the keyboard could not store the theme")

    def theme_revert(self, slot):
        self._send(SUB_THEME_REVERT, slot)
