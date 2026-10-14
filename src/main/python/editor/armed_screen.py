# SPDX-License-Identifier: GPL-2.0-or-later
# Screen page for qmk-armed keyboards with a display: your own picture, GIF or video instead of the HUD
import logging
import os
from datetime import datetime

from PyQt5.QtCore import Qt, QTimer, QThread, pyqtSignal
from PyQt5.QtGui import QPixmap
from PyQt5.QtWidgets import QHBoxLayout, QVBoxLayout, QGridLayout, QLabel, QPushButton, QProgressBar, QFileDialog, \
    QSizePolicy

import screen_media
from editor.basic_editor import BasicEditor
from unlocker import Unlocker
from protocol.qmk_armed import V_SCREEN, V_CLOCK, V_ACCENT, V_SCREEN_OFF, F_SCREEN, F_CLOCK, F_DISPLAY, ArmedError
from vial_device import VialKeyboard
from widgets.armed_controls import BracketFrame, Segmented
from util import tr

SCREEN_MODES = ("HUD", "ANIMATION")
CLOCK_FORMATS = ("24H", "12H", "OFF")
CLOCK_SYNC_MS = 10 * 60 * 1000
FITS = ("CONTAIN", "COVER", "STRETCH")
FPS_CHOICES = (5, 10, 15, 20, 25)
UPLOAD_RATE = 26 * 1024  # bytes of stream per second over Vial's raw HID, measured
READ_RATE = 25 * 1024
IMAGE_FILTER = "Images and video (*.gif *.png *.jpg *.jpeg *.bmp *.webp *.mp4 *.mov *.mkv *.webm *.avi);;All files (*)"


def checked(progress, cancelled):
    """ progress callback that also stops a job when CANCEL is pressed """
    def report(done, total):
        progress(done, total)
        if cancelled():
            raise ArmedError("cancelled")
    return report


class Job(QThread):
    """ Runs fn(progress, cancelled) off the UI thread """

    progressed = pyqtSignal(int, int)
    done = pyqtSignal(object, str)  # result, error

    def __init__(self, fn):
        super().__init__()
        self.fn = fn
        self.cancel = False

    def run(self):
        try:
            result = self.fn(lambda d, t: self.progressed.emit(d, t), lambda: self.cancel)
        except Exception as e:
            self.done.emit(None, str(e) or e.__class__.__name__)
            return
        self.done.emit(result, "")


def kib(n):
    return "{} KIB".format((n + 1023) // 1024)


def duration(seconds):
    seconds = int(round(seconds))
    return "{}:{:02d}".format(seconds // 60, seconds % 60) if seconds >= 60 else "{} S".format(max(1, seconds))


class ArmedScreen(BasicEditor):

    def __init__(self):
        super().__init__()
        self.io = None
        self.info = None
        self.paths = []
        self.frames = []
        self.stream = None
        self.preview_images = []
        self.preview_idx = 0
        self.job = None
        self.job_label = ""

        # MAIN SCREEN
        main = BracketFrame("MAIN SCREEN")
        row = QHBoxLayout()
        self.mode = Segmented(SCREEN_MODES)
        self.mode.changed.connect(self.on_mode)
        row.addWidget(self.mode)
        row.addStretch(1)
        main.box.addLayout(row)
        note = QLabel(tr("ArmedScreen", "What the keyboard shows when the menu is closed. Also in its menu: DISPLAY > MAIN SCREEN."))
        note.setObjectName("armedHint")
        note.setWordWrap(True)
        main.box.addWidget(note)

        # DISPLAY: the on-screen UI's accent and the screen-off timeout
        self.display = BracketFrame("DISPLAY")
        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(8)
        for r, text in enumerate(("ACCENT", "SCREEN OFF")):
            lbl = QLabel(text)
            lbl.setObjectName("armedKey")
            grid.addWidget(lbl, r, 0)
        self.accent = Segmented()
        self.accent.changed.connect(lambda i: self.io.set(V_ACCENT, i))
        grid.addWidget(self.accent, 0, 1, Qt.AlignLeft)
        self.screen_off = Segmented()
        self.screen_off.changed.connect(lambda i: self.io.set(V_SCREEN_OFF, i))
        grid.addWidget(self.screen_off, 1, 1, Qt.AlignLeft)
        grid.setColumnStretch(2, 1)
        self.display.box.addLayout(grid)
        note = QLabel(tr("ArmedScreen", "The accent colours the menu and HUD widgets set to ACCENT. The screen turns off "
                                        "after this much idle time, a key press wakes it. Also in its menu: DISPLAY."))
        note.setObjectName("armedHint")
        note.setWordWrap(True)
        self.display.box.addWidget(note)

        # CLOCK
        self.clock = BracketFrame("CLOCK")
        row = QHBoxLayout()
        self.clock_fmt = Segmented(CLOCK_FORMATS)
        self.clock_fmt.changed.connect(self.on_clock_format)
        row.addWidget(self.clock_fmt)
        row.addStretch(1)
        self.btn_sync = QPushButton(tr("ArmedScreen", "SYNC NOW"))
        self.btn_sync.clicked.connect(self.sync_clock)
        row.addWidget(self.btn_sync)
        self.clock.box.addLayout(row)
        self.lbl_clock = QLabel()
        self.clock.box.addWidget(self.lbl_clock)
        note = QLabel(tr("ArmedScreen", "Shown on the HUD under WPM and battery. The keyboard's own clock chip keeps time "
                                        "through sleep and unplugging; Vial Armed sets it from this computer when it connects. "
                                        "Also in its menu: DISPLAY > CLOCK."))
        note.setObjectName("armedHint")
        note.setWordWrap(True)
        self.clock.box.addWidget(note)
        self.clock_timer = QTimer()
        self.clock_timer.setInterval(CLOCK_SYNC_MS)
        self.clock_timer.timeout.connect(self.sync_clock)

        # ON THE KEYBOARD
        stored = BracketFrame("ON THE KEYBOARD")
        self.lbl_stored = QLabel()
        stored.box.addWidget(self.lbl_stored)
        row = QHBoxLayout()
        self.btn_backup = QPushButton(tr("ArmedScreen", "SAVE BACKUP..."))
        self.btn_backup.clicked.connect(self.on_backup)
        self.btn_restore = QPushButton(tr("ArmedScreen", "RESTORE BACKUP..."))
        self.btn_restore.clicked.connect(self.on_restore)
        row.addWidget(self.btn_backup)
        row.addWidget(self.btn_restore)
        row.addStretch(1)
        stored.box.addLayout(row)
        note = QLabel(tr("ArmedScreen", "Stored where the stock firmware keeps its own animation, in the same format. "
                                        "A backup reads ~4 s per frame."))
        note.setObjectName("armedHint")
        note.setWordWrap(True)
        stored.box.addWidget(note)
        stored.box.addStretch(1)

        # NEW ANIMATION
        new = BracketFrame("NEW ANIMATION", accent=True)
        body = QHBoxLayout()
        body.setSpacing(18)

        self.preview = QLabel()
        self.preview.setObjectName("armedScreenPreview")
        self.preview.setAlignment(Qt.AlignCenter)
        self.preview.setStyleSheet("background: #000; border: 1px solid #3a3a3a;")
        self.preview_timer = QTimer()
        self.preview_timer.timeout.connect(self.on_preview_tick)
        body.addWidget(self.preview, 0, Qt.AlignTop)

        side = QVBoxLayout()
        side.setSpacing(10)
        row = QHBoxLayout()
        self.btn_open = QPushButton(tr("ArmedScreen", "OPEN FILES..."))
        self.btn_open.clicked.connect(self.on_open)
        row.addWidget(self.btn_open)
        self.lbl_file = QLabel(tr("ArmedScreen", "Picture, GIF or video. Several files make a slideshow."))
        self.lbl_file.setObjectName("armedHint")
        row.addWidget(self.lbl_file, 1)
        side.addLayout(row)

        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(8)
        for r, text in enumerate(("FIT", "VIDEO FPS", "SLIDE")):
            lbl = QLabel(text)
            lbl.setObjectName("armedKey")
            grid.addWidget(lbl, r, 0)
        self.fit = Segmented(FITS)
        self.fit.set_current(0)
        self.fit.changed.connect(self.reload)
        grid.addWidget(self.fit, 0, 1, Qt.AlignLeft)
        self.fps = Segmented([str(f) for f in FPS_CHOICES])
        self.fps.set_current(FPS_CHOICES.index(10))
        self.fps.changed.connect(self.reload)
        grid.addWidget(self.fps, 1, 1, Qt.AlignLeft)
        self.slide = Segmented(["1 S", "2 S", "3 S", "5 S"])
        self.slide.set_current(2)
        self.slide.changed.connect(self.reload)
        grid.addWidget(self.slide, 2, 1, Qt.AlignLeft)
        grid.setColumnStretch(2, 1)
        side.addLayout(grid)

        self.lbl_summary = QLabel()
        self.lbl_summary.setWordWrap(True)
        side.addWidget(self.lbl_summary)

        row = QHBoxLayout()
        self.btn_upload = QPushButton(tr("ArmedScreen", "UPLOAD TO KEYBOARD"))
        self.btn_upload.setDefault(True)
        self.btn_upload.clicked.connect(self.on_upload)
        row.addWidget(self.btn_upload)
        row.addStretch(1)
        side.addLayout(row)
        side.addStretch(1)
        body.addLayout(side, 1)
        new.box.addLayout(body)

        # progress, shared by every job
        self.progress_row = QHBoxLayout()
        self.lbl_job = QLabel()
        self.lbl_job.setObjectName("armedKey")
        self.bar = QProgressBar()
        self.bar.setRange(0, 1000)
        self.bar.setTextVisible(False)
        self.lbl_eta = QLabel()
        self.lbl_eta.setObjectName("armedHint")
        self.btn_cancel = QPushButton(tr("ArmedScreen", "CANCEL"))
        self.btn_cancel.clicked.connect(self.on_cancel)
        self.progress_row.addWidget(self.lbl_job)
        self.progress_row.addWidget(self.bar, 1)
        self.progress_row.addWidget(self.lbl_eta)
        self.progress_row.addWidget(self.btn_cancel)
        new.box.addLayout(self.progress_row)
        self.lbl_status = QLabel()
        self.lbl_status.setObjectName("armedHint")
        self.lbl_status.setWordWrap(True)
        new.box.addWidget(self.lbl_status)

        grid = QGridLayout()
        grid.setSpacing(18)
        grid.addWidget(main, 0, 0)
        grid.addWidget(self.display, 0, 1)
        grid.addWidget(self.clock, 1, 0, 1, 2)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        self.addLayout(grid)
        self.addStretch(1)

        # the animation lives on its own page, which shares this page's state
        self.animation = ArmedAnimation(self)
        grid = QGridLayout()
        grid.setSpacing(18)
        grid.addWidget(stored, 0, 0)
        grid.addWidget(new, 1, 0)
        self.animation.addLayout(grid)
        self.animation.addStretch(1)
        self.show_progress(False)

    # ---- device

    def valid(self):
        if not isinstance(self.device, VialKeyboard):
            return False
        armed = self.device.keyboard.armed
        return armed is not None and armed.has(F_SCREEN)

    def rebuild(self, device):
        super().rebuild(device)
        self.io = device.keyboard.armed if self.valid() else None
        self.clock_timer.stop()
        if self.io is None:
            return
        self.clock.setVisible(self.io.has(F_CLOCK))
        self.display.setVisible(self.io.has(F_DISPLAY) and bool(self.io.accents))
        self.accent.set_labels([c.name for c in self.io.accents])
        self.screen_off.set_labels([name for _, name in self.io.screen_off])
        if self.io.has(F_CLOCK):
            self.sync_clock()
            self.clock_timer.start()
        self.info = self.io.screen_info()
        w, h = (self.info.width, self.info.height) if self.info else (320, 172)
        self.preview.setFixedSize(w + 2, h + 2)
        self.load()
        self.reload()

    def activate(self):
        if self.io is not None and self.job is None:
            self.io.reload_values()
            self.info = self.io.screen_info()
            self.load()

    def deactivate(self):
        self.preview_timer.stop()

    def load(self):
        self.mode.set_current(self.io.get(V_SCREEN))
        self.clock_fmt.set_current(self.io.get(V_CLOCK))
        self.accent.set_current(self.io.get(V_ACCENT))
        self.screen_off.set_current(self.io.get(V_SCREEN_OFF))
        if self.info is None:
            self.lbl_stored.setText("—")
        elif self.info.frames:
            self.lbl_stored.setText("{} FRAMES  ·  UP TO {}".format(self.info.frames, self.info.max_frames))
        else:
            self.lbl_stored.setText("NO ANIMATION  ·  UP TO {} FRAMES".format(self.info.max_frames))
        self.update_buttons()

    def on_mode(self, idx):
        self.io.set(V_SCREEN, idx)

    def on_clock_format(self, idx):
        self.io.set(V_CLOCK, idx)

    def sync_clock(self):
        """ The keyboard counts whole seconds: send the time right after a second ticks over """
        QTimer.singleShot(1000 - datetime.now().microsecond // 1000, self.send_clock)

    def send_clock(self):
        if self.io is None or not self.io.has(F_CLOCK) or self.job is not None:
            return
        try:
            kb = self.io.clock_get()
            now = datetime.now()
            self.io.clock_set(now)
        except Exception as e:
            logging.warning("clock sync failed: %s", e)
            self.lbl_clock.setText(tr("ArmedScreen", "NOT SYNCED"))
            return
        text = tr("ArmedScreen", "SYNCED {}").format(now.strftime("%H:%M:%S"))
        if kb is not None:
            text += tr("ArmedScreen", "  ·  WAS {:+.0f} S OFF").format((kb - now.replace(microsecond=0)).total_seconds())
        self.lbl_clock.setText(text)

    # ---- new animation

    def on_open(self):
        paths, _ = QFileDialog.getOpenFileNames(None, tr("ArmedScreen", "Picture, GIF or video"), "", IMAGE_FILTER)
        if paths:
            self.paths = paths
            self.reload()

    def reload(self, *_):
        self.preview_timer.stop()
        self.frames = []
        self.stream = None
        if not self.paths or self.info is None:
            self.preview.setPixmap(QPixmap())
            self.lbl_summary.setText("")
            self.update_buttons()
            return
        names = ", ".join(os.path.basename(p) for p in self.paths)
        self.lbl_file.setText(names)
        info, paths = self.info, list(self.paths)
        fit = self.fit.group.checkedId()
        fps = FPS_CHOICES[self.fps.group.checkedId()]
        still = (1000, 2000, 3000, 5000)[self.slide.group.checkedId()]

        def work(progress, cancelled):
            frames = screen_media.load(paths, info.width, info.height, fit, info.max_frames, fps, still)
            if not frames:
                raise RuntimeError("nothing to show in this file")
            stream = screen_media.encode(frames, checked(progress, cancelled))
            return frames, stream

        self.start(tr("ArmedScreen", "PREPARING"), work, self.on_prepared, rate=None)

    def on_prepared(self, result):
        self.frames, self.stream = result
        w, h = self.info.width, self.info.height
        self.preview_images = [QPixmap.fromImage(screen_media.frame_image(f, w, h)) for f in self.frames]
        self.preview_idx = 0
        self.on_preview_tick()
        seconds = len(self.stream) / UPLOAD_RATE
        size = screen_media.stored_size(self.frames, w, h)
        self.lbl_summary.setText("{} FRAMES  ·  {} TO SEND ({} STORED)  ·  ~{}".format(
            len(self.frames), kib(len(self.stream)), kib(size), duration(seconds)))
        self.update_buttons()

    def on_preview_tick(self):
        if not self.preview_images:
            return
        self.preview.setPixmap(self.preview_images[self.preview_idx])
        delay = max(20, int(self.frames[self.preview_idx].delay_ms))
        self.preview_idx = (self.preview_idx + 1) % len(self.preview_images)
        if len(self.preview_images) > 1:
            self.preview_timer.start(delay)

    def upload(self, frames, stream, label):
        w, h = self.info.width, self.info.height
        size = screen_media.stored_size(frames, w, h)
        io = self.io

        def work(progress, cancelled):
            io.screen_upload(size, stream, progress, cancelled)
            io.set(V_SCREEN, 1)

        self.start(label, work, self.on_uploaded, rate=UPLOAD_RATE * size / max(1, len(stream)))

    def on_upload(self):
        if self.frames and self.stream:
            self.upload(self.frames, self.stream, tr("ArmedScreen", "UPLOADING"))

    def on_uploaded(self, _):
        self.lbl_status.setText(tr("ArmedScreen", "Uploaded. The keyboard now shows the animation."))
        self.info = self.io.screen_info()
        self.io.reload_values()
        self.load()

    # ---- backup

    def on_backup(self):
        if not self.info or not self.info.frames:
            return
        path, _ = QFileDialog.getSaveFileName(None, tr("ArmedScreen", "Save backup"), "screen-backup.bin", "Backup (*.bin)")
        if not path:
            return
        io, info = self.io, self.info

        def work(progress, cancelled):
            hdr = io.screen_read(0, screen_media.HEADER, cancelled=cancelled)
            body = io.screen_read(screen_media.HEADER, info.frames * info.frame_bytes, progress, cancelled)
            with open(path, "wb") as f:
                f.write(hdr + body)
            return path

        self.start(tr("ArmedScreen", "READING"), work,
                   lambda p: self.lbl_status.setText(tr("ArmedScreen", "Saved to {}").format(p)), rate=READ_RATE)

    def on_restore(self):
        path, _ = QFileDialog.getOpenFileName(None, tr("ArmedScreen", "Restore backup"), "", "Backup (*.bin)")
        if not path:
            return
        info = self.info
        with open(path, "rb") as f:
            blob = f.read()
        frames = screen_media.from_backup(blob, info.width, info.height, info.max_frames)
        if not frames:
            self.lbl_status.setText(tr("ArmedScreen", "Not a screen backup."))
            return

        def work(progress, cancelled):
            return frames, screen_media.encode(frames, checked(progress, cancelled))

        def encoded(result):
            self.upload(result[0], result[1], tr("ArmedScreen", "RESTORING"))

        self.start(tr("ArmedScreen", "PREPARING"), work, encoded, rate=None)

    # ---- jobs

    def start(self, label, fn, on_done, rate):
        if self.job is not None:
            return
        self.job = Job(fn)
        self.job_label = label
        self.job_rate = rate
        self.job_done = on_done
        self.job.progressed.connect(self.on_progress)
        self.job.done.connect(self.on_job_done)
        talks = rate is not None  # talks to the keyboard: keep device scanning off it
        if talks:
            Unlocker.global_main_window.set_busy(True)
        self.job_talks = talks
        self.lbl_job.setText(label)
        self.lbl_eta.setText("")
        self.lbl_status.setText("")
        self.bar.setValue(0)
        self.show_progress(True)
        self.update_buttons()
        self.job.start()

    def on_progress(self, done, total):
        self.bar.setValue(int(1000 * done / max(1, total)))
        if self.job_rate:
            self.lbl_eta.setText("{} / {}  ·  {} LEFT".format(kib(done), kib(total), duration((total - done) / self.job_rate)))

    def on_cancel(self):
        if self.job is not None:
            self.job.cancel = True

    def on_job_done(self, result, error):
        job, self.job = self.job, None
        job.wait()
        if self.job_talks:
            Unlocker.global_main_window.set_busy(False)
        self.show_progress(False)
        if error:
            self.lbl_status.setText("ERROR: " + error if error != "cancelled" else tr("ArmedScreen", "Cancelled."))
            if self.io is not None and self.job_talks:
                self.info = self.io.screen_info()
                self.load()
            self.update_buttons()
            return
        self.job_done(result)
        self.update_buttons()

    def show_progress(self, on):
        for w in (self.lbl_job, self.bar, self.lbl_eta, self.btn_cancel):
            w.setVisible(on)

    def update_buttons(self):
        idle = self.job is None
        # megabytes at ~15 ms per 28-byte packet: USB only
        bulk = idle and not getattr(self.device, "dongle", False)
        self.btn_open.setEnabled(idle)
        self.btn_upload.setEnabled(bulk and bool(self.frames) and self.stream is not None)
        self.btn_backup.setEnabled(bulk and bool(self.info and self.info.frames))
        self.btn_restore.setEnabled(bulk and self.info is not None)
        tip = "" if bulk or not idle else tr("ArmedScreen", "Connect the keyboard by USB to transfer animations")
        for btn in (self.btn_upload, self.btn_backup, self.btn_restore):
            btn.setToolTip(tip)
        self.mode.setEnabled(idle)
        self.clock_fmt.setEnabled(idle)
        self.accent.setEnabled(idle)
        self.screen_off.setEnabled(idle)
        self.btn_sync.setEnabled(idle)
        for seg in (self.fit, self.fps, self.slide):
            seg.setEnabled(idle)


class ArmedAnimation(BasicEditor):
    """ Screen > ANIMATION: the stored animation and a new upload; the work is done by ArmedScreen """

    def __init__(self, screen):
        super().__init__()
        self.screen = screen

    def valid(self):
        return self.screen.valid()

    def activate(self):
        self.screen.activate()

    def deactivate(self):
        self.screen.deactivate()
