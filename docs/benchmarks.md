# Vial Armed 2.0 vs 1.8: measurements

Vial Armed 2.0 is a rewrite of the Python/PyQt5 app (1.x) in C++20 and Qt 6. Both versions were measured
on the same machine and with the same keyboard: an IO Type 84 Pro on qmk-armed 1.8.0 over USB, Linux
(CachyOS), Qt 6.12 for the C++ build and the 1.8.0 Python sources. Start times are the median of three
runs, the rest single runs; package sizes are the 1.8.0 release files and a local 2.0 build.

| | 1.8 (Python) | 2.0 (C++) | |
|---|---|---|---|
| Start to the first frame of the window | 560 ms | 175 ms | 3.2× faster |
| CPU time to start and load the keyboard | 3.5 s | 0.22 s | 16× less |
| Memory after loading (RSS) | 145 MiB | 57 MiB | 2.5× less |
| CPU while idle in the tray (HUD agent off) | 2.0 % | 0.9 % | |
| Full keyboard load over USB | 1.0 s | 1.0 s | limited by USB round trips |
| HUD preview, one frame (320 × 172) | 13.3 ms | 0.08 ms | 160× faster |
| Screen animation encode, 152 frames | ~3.5 s | 26 ms | 130× faster |
| Linux AppImage | 40 MB | 42 MB | Qt 6 and ICU instead of Qt 5 |
| Linux .deb | 45 MB | 31 MB | |

What else changed for speed:

- All keyboard traffic runs on a worker thread in priority order (edits before polls before background
  work), so the window never waits on the device. Queued writes of the same value replace each other:
  a dragged slider sends only its last position.
- Over the 2.4 GHz dongle the retry policy is the one 1.8 settled on (at least 5 attempts, 150 → 600 ms
  timeouts, stale replies flushed), and the device list shows wired keyboards at once instead of waiting
  for the dongle probe.
- Quitting cancels the transfer in flight instead of waiting for it.
- The HUD agent samples this computer's metrics only while it feeds a keyboard or the HUD editor is
  open.

Not measured yet: a full load over the 2.4 GHz dongle (8.6 s in 1.5.0, bound by the radio).

How the figures were taken: the 2.0 timings come from `VIAL_ARMED_SCREENSHOT=file:0`, which quits after the
first frame, and from the benchmark slots of `tst_hud` and `tst_screenmedia` in a Release build; the 1.8
start time comes from the same flow driven through `main.py`'s `MainWindow`. Memory and CPU were read from
`/proc/<pid>` 10 and 30 seconds after start.
