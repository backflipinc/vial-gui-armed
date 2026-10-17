# Vial Armed

A fork of [vial-gui](https://github.com/vial-kb/vial-gui) for keyboards running
**[qmk-armed](https://github.com/backflipinc/qmk-armed)**: the Hi-Tech / Military Black look of the
IO Type 84 Pro's on-screen UI, plus pages for everything the firmware can do beyond stock Vial — every
RGB Matrix effect with a live preview, two-colour effects, the Fn overlay colour, the light bar, auto-sleep, your
own picture, GIF or video on the keyboard's screen, and a HUD you lay out yourself and feed with your computer's
metrics.

Version 2 is a native C++ / Qt 6 rewrite of the Python app: it starts three times faster, uses less than half the
memory, and never freezes while it talks to the keyboard — see [docs/benchmarks.md](docs/benchmarks.md).

![Lighting](docs/screenshots/lighting.png)

> [!IMPORTANT]
> The extra pages need a keyboard flashed with [qmk-armed](https://github.com/backflipinc/qmk-armed). Vial Armed
> recognises the firmware by its protocol and shows only what that keyboard reports: RGB Matrix effects on any
> qmk-armed keyboard with per-key RGB, COLOR 2, FN COLOR, the light bar and auto-sleep where the keyboard has them.
> So far the [IO Type 84 Pro Wireless](https://github.com/backflipinc/qmk-armed/tree/armed/keyboards/io/type84_pro)
> is the one keyboard that has them all. With any other Vial keyboard it works like regular Vial in the Armed theme.

## Features

| | |
|---|---|
| 🖤 **Armed theme** | Black panels, amber accent, Rajdhani and Share Tech Mono, square keycaps, numbered side menu — the design code of the keyboard's screen |
| 🌈 **Lighting** | Every effect the firmware has, including custom ones — on the IO Type 84 Pro, 7 **two-colour** effects (TWO TONE, GRADIENT 2, CROSSFADE 2, SCANNER 2, STARLIGHT 2, REACTIVE 2, RIPPLE 2); COLOR and COLOR 2, brightness, speed |
| 🎨 **Theme editor** | **Lighting > THEME EDITOR**: build your own lighting from up to 8 layers — solid, gradient, rainbow, pulse, wave, sparkle, reactive, ripple and per-key paint. Each layer lights all keys, the keys of a keymap layer (FN1, FN2, FN3: what's bound there and the keys that bring it up; with qmk-armed 1.9 the keyboard follows remaps by itself), or keys you click, drag or pick by group (letters, F-row, mods, arrows, WASD). Set the direction on a dial or spread from a key, blend layers, show one only with Caps Lock or while a keymap layer is active. Every change is live on the keyboard; undo / redo, presets, import / export as JSON |
| 👁️ **Live preview** | The selected effect runs on the keyboard layout with the firmware's own maths, LED positions and flags. Reactive effects press keys by themselves; click a key to press it |
| 🔦 **Fn overlay** | Pick the FN COLOR and hold **HOLD FN** to see which keys light up while Fn is held |
| 📏 **Light bar** | Mode (WPM, WPM bar, battery, link, scanner, off), colour and Caps Lock alert, with a 9-LED preview |
| ⚙️ **Device** | NKRO, Win lock, SnapTap mode, Bluetooth slot, auto-sleep — what the keyboard's own menu sets, shown for what the keyboard has |
| 🎞️ **Screen** | **Screen > SETTINGS**: the main screen (HUD or animation), the screen's accent colour and off timeout, the clock. **Screen > ANIMATION**: put a picture, GIF or video (with ffmpeg installed) on the keyboard's screen: fit, frame rate, slideshow, a preview of exactly what gets uploaded, progress with time left. Switch the main screen between the HUD and the animation; save and restore what's stored, including the stock animation. Sets the keyboard's clock from this computer when it connects, 24H / 12H / off |
| 📟 **HUD editor** | **Screen > HUD EDITOR**: lay out the keyboard's main screen from widgets — text with values, bars, graphs, indicators, the battery icon, frames and lines, multi-line text — on a preview drawn pixel for pixel like the keyboard draws it, with its own fonts and colour roles, so the result keeps the look of the firmware. Drag with smart guides, resize, nudge with arrows; select several and align, distribute or match their sizes; reorder in the widget list; copy / paste, undo / redo, presets (keyboard default, system monitor, console, big clock), import / export as JSON. Live on the keyboard as you edit, SAVE keeps it |
| 📡 **HUD agent** | Feeds the HUD with this computer's CPU / GPU load and temperature, RAM, VRAM, swap, network, disk, load average and laptop battery, plus the first number or the whole output of any command or script (Cyrillic is spelled in Latin letters). Runs inside Vial Armed, or without a window: `vial-armed --agent`, with a RUN AT LOGIN switch on Linux and Windows. Over USB and the 2.4 GHz dongle |
| ⌨️ **Everything from Vial** | Keymap and knobs on all layers, macros, tap dance, combos, key overrides, QMK settings, matrix tester |
| 🧭 **Status** | Link and lock state in the top bar, qmk-armed version, Vial protocol, layer count and keyboard UID in the footer |

| Keymap | Device |
|---|---|
| ![Keymap](docs/screenshots/keymap.png) | ![Device](docs/screenshots/device.png) |

| Themes | Screen |
|---|---|
| ![Themes](docs/screenshots/themes.png) | ![Screen](docs/screenshots/screen.png) |

| HUD: layout | HUD: agent |
|---|---|
| ![HUD layout](docs/screenshots/hud.png) | ![HUD agent](docs/screenshots/hud-agent.png) |

The original mockups are in [`design/`](design/); what the keyboard's own screen looks like, HUD layouts included, is in
[qmk-armed's mockup](https://github.com/backflipinc/qmk-armed/blob/armed/keyboards/io/type84_pro/docs/images/ui-mockup.png).

## Install

Download the latest build from [Releases](https://github.com/backflipinc/vial-gui-armed/releases):

| | |
|---|---|
| Linux | `vial-armed-<version>-x86_64.AppImage` — `chmod +x` and run; or `vial-armed_<version>_amd64.deb` (Debian / Ubuntu 22.04 and newer, installs to `/opt/vial-armed` with the udev rule below) |
| Windows | `vial-armed-<version>-setup.exe` (installer) or `vial-armed-<version>-win.zip` (portable) |
| macOS | `vial-armed-<version>-mac.dmg` |

The version is the release tag (`v2.0.0` → `2.0.0`); builds of untagged commits are named after the last tag and the
commit (`2.0.0+3.gabc1234`), and **About** shows the same. Settings and the HUD agent's configuration carry over from 1.x.

On Linux the keyboard needs a udev rule to be reachable without root. The .deb installs
[`70-vial-armed.rules`](app/packaging/linux/70-vial-armed.rules) (any Vial keyboard, the Vial bootloader and the
IO Type 84 Pro's dongle); with the AppImage, copy it to `/etc/udev/rules.d/` and run
`sudo udevadm control --reload-rules && sudo udevadm trigger`.

The IO Type 84 Pro (firmware 1.5+) is found by USB or through its **2.4 GHz dongle** (listed as `… [2.4G]`; mode switch
on 2.4 GHz). Over the dongle each request takes ~20 ms, so connecting takes a few seconds, and screen animations are
transferred over USB only. Bluetooth has no channel for it.

## How it works

Vial talks to the keyboard over Raw HID. VIA and VialRGB only know QMK's built-in effects and one colour,
and VialRGB shows custom effects as *Disable*. qmk-armed adds its own command, **`0xAD`** (unused by VIA and Vial),
that Vial Armed probes on connect:

| Command | |
|---|---|
| `INFO` | `ARMD` magic, protocol and firmware version, **feature flags**, list sizes, maximum brightness |
| `LIST` | effects (QMK id, flags, name), colour presets, light bar modes, sleep options, LEDs (matrix position, x/y, flags) |
| `GET` / `SET` | lighting on/off, effect, hue, saturation, brightness, speed, COLOR 2, FN COLOR, light bar mode / colour / Caps alert, auto-sleep, main screen, clock format, NKRO, Win lock, SnapTap mode, screen accent and timeout, Bluetooth slot |
| `SAVE` | write hue / saturation / brightness / speed to EEPROM (sliders apply them live without wearing the flash) |
| `SCREEN_*` | screen size and what's stored; upload an animation as a compressed stream (pipelined, ~26 KiB/s over Vial's 32-byte reports); read it back |
| `THEME_*` | theme slots and their layout; read, write live, save, revert |
| `CLOCK_*` | read and set the keyboard's local time, whole seconds |
| `HUD_*` | the HUD's sources, colour roles and fonts (for the preview); read, write live, store or revert the layout; push numbers and text from the host (no reply, so the agent never gets in the way) |

The feature flags decide what's shown: RGB Matrix opens the Lighting page and replaces the VialRGB one,
COLOR 2, FN COLOR and LIGHT BAR add their panels, auto-sleep, NKRO, SnapTap and the like fill the Device page, theme slots add THEME EDITOR to Lighting, a screen opens the Screen page with SETTINGS, HUD EDITOR and ANIMATION. Effect names, colours
and the LED layout come from the keyboard too, so the GUI hardcodes nothing about a particular board.
Without qmk-armed, Vial Armed falls back to the stock Vial lighting page.

Protocol spec and how to add features to a keyboard: [qmk-armed/docs/armed.md](https://github.com/backflipinc/qmk-armed/blob/armed/docs/armed.md).
Implementation here: [`protocol/armed.cpp`](app/src/protocol/armed.cpp), the animation encoder in
[`model/screenmedia.cpp`](app/src/model/screenmedia.cpp), themes in [`model/theme.cpp`](app/src/model/theme.cpp)
and the firmware's theme renderer in [`render/lighting.cpp`](app/src/render/lighting.cpp);
the HUD in [`model/hud.cpp`](app/src/model/hud.cpp), its renderer (Quantum Painter's fonts and colour maths)
in [`render/hudrender.cpp`](app/src/render/hudrender.cpp) and the agent in [`agent/hudagent.cpp`](app/src/agent/hudagent.cpp).

### HUD agent

While Vial Armed runs, it sends what the HUD editor's AGENT view lists: up to 16 numbers (H1–H16) and 4 texts (T1–T4).
Pick them as sources (H1–H16, T1–T4) in the LAYOUT view. Each number is a metric or a command whose first printed number is taken;
each text is a command whose output is shown (the end of it when it's longer than 159 characters). The settings
live in `~/.config/vial-armed/hud-agent.json` (`%APPDATA%\vial-armed` on Windows); commands are never stored on the
keyboard. To keep the HUD fed without the window, run `vial-armed --agent` (or tick RUN AT LOGIN): it finds the
keyboard by itself, sets its clock, follows changes to the settings, and steps aside while Vial Armed is open.
Closing the window keeps Vial Armed (and the agent) in the system tray; quit from the tray icon's menu or File > Exit,
or untick File > Minimize to tray on close.

## Development

C++20, Qt 6.5 or newer (CI builds with 6.8), CMake 3.21 and Ninja; hidapi and liblzma are taken from the system
or fetched by CMake. On Linux also libudev (for hotplug).

```sh
cmake -S app -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build        # unit tests, the UI against a virtual keyboard, parity with 1.x's data
./build/src/vial-armed
```

`app/src` holds the protocol and models (a static library the tests link), the pages and widgets; everything that
talks to the keyboard runs on a worker thread through `hid::Session`. Development aids: `VIAL_ARMED_PAGE="Screen/HUD
editor"` opens a page, `VIAL_ARMED_SCREENSHOT=out.png[:ms[:WxH]]` saves the window and quits,
`QT_FORCE_STDERR_LOGGING=1` prints the log, `build/tests/hwprobe [out.vil]` loads a connected keyboard headless.

Linux packages, as CI makes them: `app/packaging/linux/package.sh build dist <version>` (AppImage and .deb, Qt inside).
Releases are built by GitHub Actions ([`app.yml`](.github/workflows/app.yml)): pushing a `v2.*` tag builds Linux,
Windows and macOS, runs the tests and attaches the AppImage, .deb, installer, zip and dmg to a release.

## Roadmap

- A WebAssembly build for the browser (WebHID), from the same C++ code
- Pipelined reads when connecting to a qmk-armed keyboard over USB

## Credits

- [Vial](https://get.vial.today) / [vial-gui](https://github.com/vial-kb/vial-gui) — the application this is built on
- Fonts: [Rajdhani](https://fonts.google.com/specimen/Rajdhani) and
  [Share Tech Mono](https://fonts.google.com/specimen/Share+Tech+Mono) (SIL Open Font License, see `app/resources/fonts/`)

Not affiliated with IO by Red Square or the Vial project.

## License

GPL-2.0-or-later, like Vial — see [COPYING](COPYING).
