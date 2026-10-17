#!/usr/bin/env python3
"""Golden data for tst_definition: KLE geometry and Vial key roles as the Python app parses them.

venv/bin/python app/tools/gen_layout_golden.py  ->  app/tests/data/layouts/*.golden.json
"""
import glob
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "src", "main", "python"))

from kle_serial import Serial  # noqa: E402

FIELDS = ["x", "y", "width", "height", "x2", "y2", "width2", "height2", "rotation_x", "rotation_y",
          "rotation_angle", "decal", "ghost"]


def classify(payload):
    """ the key roles keyboard_comm.Keyboard.reload_layout assigns """
    kb = Serial().deserialize(payload["layouts"]["keymap"])
    keys = []
    for key in kb.keys:
        d = {f: getattr(key, f) for f in FIELDS}
        d["labels"] = key.labels
        d["role"] = "none"
        if key.labels[4] == "e":
            idx, direction = key.labels[0].split(",")
            d["role"] = "encoder"
            d["encoder"] = [int(idx), int(direction)]
        elif key.decal or (key.labels[0] and "," in key.labels[0]):
            row, col = 0, 0
            if key.labels[0] and "," in key.labels[0]:
                row, col = key.labels[0].split(",")
                row, col = int(row), int(col)
            d["role"] = "key"
            d["matrix"] = [row, col]
        d["layout"] = [-1, -1]
        if key.labels[8]:
            idx, opt = key.labels[8].split(",")
            d["layout"] = [int(idx), int(opt)]
        keys.append(d)
    return keys


def main():
    for path in sorted(glob.glob(os.path.join(ROOT, "app", "tests", "data", "layouts", "*.json"))):
        if path.endswith(".golden.json"):
            continue
        with open(path) as f:
            payload = json.load(f)
        out = path[:-5] + ".golden.json"
        with open(out, "w") as f:
            json.dump(classify(payload), f, indent=1)
        print(out)


if __name__ == "__main__":
    main()
