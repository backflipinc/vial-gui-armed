#!/usr/bin/env python3
"""Golden data for tst_keycodes: what the Python app's Keycode answers, per Vial protocol.

venv/bin/python app/tools/gen_keycode_golden.py  ->  app/tests/data/keycodes-golden.json.xz
"""
import json
import lzma
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.environ.get("VIAL_ARMED_1X", ROOT), "src", "main", "python"))

from keycodes import keycodes as K  # noqa: E402

EXPRESSIONS = [
    "KC_A", "KC_NO", "KC_TRNS", "KC_TRANSPARENT", "KC_PERC", "LCTL(KC_A)", "LSFT(KC_5)", "C(S(KC_ESC))",
    "LT(2, KC_SPC)", "LT3(KC_B)", "LT15(KC_Z)", "MT(MOD_LCTL | MOD_LSFT, KC_ESC)", "LCTL_T(KC_A)", "ALL_T(KC_F1)",
    "HYPR(KC_X)", "MEH_T(KC_Y)", "TO(1)", "MO(3)", "DF(2)", "TG(0)", "OSL(1)", "TT(2)", "OSM(MOD_LSFT)",
    "LM(1, MOD_LALT)", "TD(5)", "0x7C00 | 5", "0x1234", "1234", "KC_A + 1", "2 ** 3 + 1", "-1 & 0xFF",
    "(KC_A | KC_B) ^ 3", "10 // 3", "10 % 3", "USER00", "M0", "M15", "QK_BOOT", "FN_MO13", "LT0(kc)",
    "MO(31)", "PDF(1)", "KC_NLCK", "SGUI(KC_S)", "RCS_T(KC_Q)", "nonsense", "KC_A(", "LT(1)", "MO(1, 2)",
    "0123", "1_000", "0b101", "0o17", "QK_LAYER_LOCK", "QK_LLCK", "CW_TOGG", "QK_REPEAT_KEY",
]


class FakeKeyboard:
    def __init__(self, protocol):
        self.vial_protocol = protocol
        self.layers = 4
        self.macro_count = 16
        self.tap_dance_count = 8
        self.custom_keycodes = None
        self.midi = "advanced"
        self.supported_features = {"caps_word", "persistent_default_layer"}


def main():
    out = {}
    for proto in (5, 6):
        K.recreate_keyboard_keycodes(FakeKeyboard(proto))
        kc = K.Keycode
        serialized = [kc.serialize(c) for c in range(0x10000)]
        ids = sorted(set(k.qmk_id for k in K.KEYCODES))
        out[str(proto)] = {
            "serialize": serialized,
            "deserialize": {e: kc.deserialize(e) for e in EXPRESSIONS},
            "label": {i: kc.label(i) for i in ids + ["LCTL(KC_A)", "LT1(KC_B)", "0x1234"]},
            "tooltip": {i: kc.tooltip(i) for i in ids + ["LCTL(KC_A)", "0x1234"]},
            "normalize": {i: kc.normalize(i) for i in ids[:400] + ["KC_PERC", "KC_NLCK", "LSFT(KC_5)"]},
            "hidden": sorted(k.qmk_id for k in K.KEYCODES if k.hidden),
            "is_mask": {i: kc.is_mask(i) for i in ["LCTL(KC_A)", "LT1(KC_B)", "MO(1)", "TD(2)", "KC_A"]},
        }
    path = os.path.join(ROOT, "app", "tests", "data", "keycodes-golden.json.xz")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with lzma.open(path, "wt", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False)
    print(path, os.path.getsize(path))


if __name__ == "__main__":
    main()
