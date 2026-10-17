#!/usr/bin/env python3
"""Golden data for tst_macro: macro (de)serialization as the Python app does it.

venv/bin/python app/tools/gen_macro_golden.py  ->  app/tests/data/macro-golden.json
"""
import json
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.environ.get("VIAL_ARMED_1X", ROOT), "src", "main", "python"))

from keycodes import keycodes as K  # noqa: E402
from macro.macro_action_ui import tag_to_action  # noqa: E402
from protocol.macro import macro_deserialize_v1, macro_deserialize_v2  # noqa: E402

MACROS = [
    [["text", "Hello, world!"]],
    [["tap", "KC_A", "KC_B"], ["down", "KC_LSHIFT"], ["tap", "KC_1"], ["up", "KC_LSHIFT"]],
    [["tap", "LCTL(KC_C)"], ["delay", 250], ["tap", "LCTL(KC_V)"]],
    [["delay", 0], ["delay", 254], ["delay", 255], ["delay", 1000], ["delay", 65000]],
    [["tap", "KC_ENTER", "MO(1)", "LT1(KC_A)", "QK_BOOT", "KC_TRNS"]],
    [["text", "Привет"], ["tap", "KC_SPACE"]],
    [["down", "KC_LGUI"], ["tap", "KC_R"], ["up", "KC_LGUI"], ["text", "cmd\n"]],
]


class FakeKeyboard:
    def __init__(self, protocol):
        self.vial_protocol = protocol
        self.layers = 4
        self.macro_count = 16
        self.tap_dance_count = 8
        self.custom_keycodes = None
        self.midi = None
        self.supported_features = set()


def build(saved):
    out = []
    for act in saved:
        obj = tag_to_action[act[0]]()
        obj.restore(act)
        out.append(obj)
    return out


def main():
    random.seed(7)
    blobs = [bytes(random.randrange(256) for _ in range(random.randrange(1, 40))).replace(b"\x00", b"\x01")
             for _ in range(40)]
    blobs += [b"\x01\x05\x00\x7c", b"\x01\x05\x05\xff", b"\x01\x04\x02\x03", b"\x01", b"\x01\x01", b"\x02\x04", b"\x01\x09xx"]
    out = {}
    for proto in (1, 6):
        K.recreate_keyboard_keycodes(FakeKeyboard(proto))
        ser = []
        for m in MACROS:
            if proto < 2 and any(a[0] == "delay" for a in m):
                continue
            data = b"".join(a.serialize(proto) for a in build(m))
            ser.append({"macro": m, "hex": data.hex()})
        de = []
        for b in blobs:
            acts = macro_deserialize_v2(b) if proto >= 2 else macro_deserialize_v1(b)
            de.append({"hex": b.hex(), "macro": [a.save() for a in acts]})
        out[str(proto)] = {"serialize": ser, "deserialize": de}
    path = os.path.join(ROOT, "app", "tests", "data", "macro-golden.json")
    with open(path, "w") as f:
        json.dump(out, f, ensure_ascii=False, indent=0)
    print(path)


if __name__ == "__main__":
    main()
