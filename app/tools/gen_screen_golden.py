#!/usr/bin/env python3
"""Golden data for tst_screenmedia: the Python app's upload stream encoder.

venv/bin/python app/tools/gen_screen_golden.py  ->  app/tests/data/screen-golden.json.xz
"""
import json
import lzma
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.environ.get("VIAL_ARMED_1X", ROOT), "src", "main", "python"))

import screen_media as SM  # noqa: E402

W, H = 64, 40


def frames():
    random.seed(3)
    out = []
    flat = bytes([0x34, 0x12]) * (W * H)
    out.append(SM.Frame(flat, 3))
    noise = bytes(random.randrange(256) for _ in range(W * H * 2))
    out.append(SM.Frame(noise, 5))
    mixed = bytearray(noise)
    mixed[200:900] = bytes([7, 7]) * 350
    mixed[1000:1004] = bytes([1, 2, 1, 2])
    out.append(SM.Frame(bytes(mixed), 1000))
    out.append(SM.Frame(bytes(mixed), 40))
    big = bytearray(mixed)
    for i in range(0, len(big), 97):
        big[i] ^= 0xFF
    out.append(SM.Frame(bytes(big), 0))
    return out


def main():
    fs = frames()
    stretched = SM.stretch_delays(fs, 100)
    data = {
        "w": W, "h": H,
        "frames": [{"hex": f.pixels.hex(), "delay": f.delay_ms} for f in fs],
        "stretched": [f.delay_ms for f in stretched],
        "header": SM.header(stretched).hex(),
        "stream": SM.encode(stretched).hex(),
        "big_run": SM.encode([SM.Frame(bytes([9, 9]) * 70000, 20)]).hex(),
    }
    path = os.path.join(ROOT, "app", "tests", "data", "screen-golden.json.xz")
    with lzma.open(path, "wt") as f:
        json.dump(data, f)
    print(path, os.path.getsize(path))


if __name__ == "__main__":
    main()
