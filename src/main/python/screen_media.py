# SPDX-License-Identifier: GPL-2.0-or-later
# Pictures, GIFs and video -> a qmk-armed screen animation (see docs/armed.md in qmk-armed):
# frames fitted to the screen, RGB565 low byte first, a 256-byte header, and the compressed upload stream.
# Plain Python and Qt: images and GIFs are read by Qt, video by ffmpeg when it is installed.
import math
import os
import shutil
import struct
import subprocess
import sys
from array import array

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QImage, QImageReader, QPainter, QColor

HEADER = 256
DELAY_UNIT_MS = 2      # header delay unit, as the stock configurator writes it
MAX_DELAY_MS = 255 * DELAY_UNIT_MS
FIT_CONTAIN, FIT_COVER, FIT_STRETCH = range(3)


class Frame:

    def __init__(self, pixels, delay_ms):
        self.pixels = pixels  # bytes, RGB565 low byte first, row by row
        self.delay_ms = delay_ms


def _rgb565(img, w, h):
    img = img.convertToFormat(QImage.Format_RGB16)
    ptr = img.constBits()
    ptr.setsize(img.byteCount())
    raw = bytes(ptr)
    stride = img.bytesPerLine()
    if stride != w * 2:
        raw = b"".join(raw[y * stride:y * stride + w * 2] for y in range(h))
    if sys.byteorder == "big":
        a = array("H", raw)
        a.byteswap()
        raw = a.tobytes()
    return raw


def fit(img, w, h, mode, background=Qt.black):
    canvas = QImage(w, h, QImage.Format_RGB32)
    canvas.fill(QColor(background))
    if mode == FIT_STRETCH:
        scaled = img.scaled(w, h, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)
    elif mode == FIT_COVER:
        scaled = img.scaled(w, h, Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
    else:
        scaled = img.scaled(w, h, Qt.KeepAspectRatio, Qt.SmoothTransformation)
    qp = QPainter(canvas)
    qp.drawImage((w - scaled.width()) // 2, (h - scaled.height()) // 2, scaled)
    qp.end()
    return canvas


def frame_image(frame, w, h):
    """ QImage of a frame, for previews """
    return QImage(frame.pixels, w, h, w * 2, QImage.Format_RGB16).copy()


def is_image(path):
    return QImageReader(path).canRead()


def ffmpeg_path():
    return shutil.which("ffmpeg")


def load_image(path, w, h, mode, max_frames, still_ms=1000):
    reader = QImageReader(path)
    frames = []
    while reader.canRead() and len(frames) < max_frames:
        img = reader.read()
        if img.isNull():
            break
        delay = reader.nextImageDelay()
        frames.append(Frame(_rgb565(fit(img, w, h, mode), w, h), delay if delay > 0 else still_ms))
        if reader.imageCount() == 1:
            break
    return frames


def load_video(path, w, h, mode, max_frames, fps):
    exe = ffmpeg_path()
    if exe is None:
        raise RuntimeError("ffmpeg is needed for video; install it and make sure it is in PATH")
    if mode == FIT_STRETCH:
        vf = "scale={w}:{h}"
    elif mode == FIT_COVER:
        vf = "scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h}"
    else:
        vf = "scale={w}:{h}:force_original_aspect_ratio=decrease,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2"
    vf = "fps={},".format(fps) + vf.format(w=w, h=h)
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    out = subprocess.run([exe, "-loglevel", "error", "-i", path, "-vf", vf, "-frames:v", str(max_frames),
                          "-f", "rawvideo", "-pix_fmt", "rgb565le", "-"],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=flags)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.decode("utf-8", "replace").strip() or "ffmpeg failed")
    size = w * h * 2
    data = out.stdout
    return [Frame(data[i:i + size], 1000 / fps) for i in range(0, len(data) - size + 1, size)]


def load(paths, w, h, mode, max_frames, fps=10, still_ms=1000):
    """ Frames from image / GIF / video files, one after another """
    frames = []
    for path in paths:
        left = max_frames - len(frames)
        if left <= 0:
            break
        if is_image(path):
            frames += load_image(path, w, h, mode, left, still_ms)
        else:
            frames += load_video(path, w, h, mode, left, fps)
    return stretch_delays(frames, max_frames)


def stretch_delays(frames, max_frames):
    """ A frame can stay up to 510 ms; longer ones are repeated (cheap: the repeats compress to nothing) """
    out = []
    for f in frames:
        n = max(1, math.ceil(f.delay_ms / MAX_DELAY_MS)) if len(frames) > 1 else 1
        for _ in range(n):
            out.append(Frame(f.pixels, f.delay_ms / n))
    return out[:max_frames]


def header(frames):
    h = bytearray(b"\xff" * HEADER)
    h[0] = len(frames)
    for i, f in enumerate(frames):
        h[1 + i] = max(0, min(255, round(f.delay_ms / DELAY_UNIT_MS)))
    return bytes(h)


def stored_size(frames, w, h):
    return HEADER + len(frames) * w * h * 2


def from_backup(blob, w, h, max_frames):
    """ Frames of a .bin saved by save backup (header + frames), cut to what fits """
    size = w * h * 2
    n = min((len(blob) - HEADER) // size, blob[0] if blob else 0, max_frames)
    return [Frame(blob[HEADER + i * size:HEADER + (i + 1) * size], blob[1 + i] * DELAY_UNIT_MS) for i in range(n)]


# ---- upload stream: control byte op << 6 | n (n = 0: 16-bit length follows) on 16-bit pixels

OP_LIT, OP_RUN, OP_PREV = 0, 1, 2
MAX_OP = 0xFFFF
CHUNK = 64


def _op(kind, n):
    return bytes([kind << 6 | n]) if n < 64 else bytes([kind << 6]) + struct.pack("<H", n)


def _words(raw):
    a = array("H")
    a.frombytes(raw)
    if sys.byteorder == "big":
        a.byteswap()
    return a


def _encode_words(cur, prev, raw):
    out = bytearray()
    n = len(cur)
    i = 0
    lit = -1

    def flush(end):
        nonlocal lit
        while lit >= 0 and lit < end:
            k = min(end - lit, MAX_OP)
            out.extend(_op(OP_LIT, k))
            out.extend(raw[2 * lit:2 * (lit + k)])
            lit += k
        lit = -1

    while i < n:
        pl = 0
        if prev is not None and cur[i] == prev[i]:
            j = i + 1
            while j + CHUNK <= n and cur[j:j + CHUNK] == prev[j:j + CHUNK]:
                j += CHUNK
            while j < n and cur[j] == prev[j]:
                j += 1
            pl = j - i
        v = cur[i]
        k = i + 1
        while k < n and cur[k] == v:
            k += 1
        rl = k - i
        if pl >= 2 and pl >= rl:
            flush(i)
            step = min(pl, MAX_OP)
            out.extend(_op(OP_PREV, step))
            i += step
        elif rl >= 3:
            flush(i)
            step = min(rl, MAX_OP)
            out.extend(_op(OP_RUN, step))
            out.extend(raw[2 * i:2 * i + 2])
            i += step
        else:
            if lit < 0:
                lit = i
            i += 1
    flush(n)
    return out


def encode(frames, progress=lambda done, total: None):
    """ The upload stream for header + frames; PREV refers to the previous frame """
    hdr = header(frames)
    out = bytearray(_encode_words(_words(hdr), None, hdr))
    prev = None
    for i, f in enumerate(frames):
        cur = _words(f.pixels)
        out.extend(_encode_words(cur, prev, f.pixels))
        prev = cur
        progress(i + 1, len(frames))
    return bytes(out)
