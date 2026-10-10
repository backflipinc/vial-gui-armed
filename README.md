# Vial Armed

A fork of [vial-gui](https://github.com/vial-kb/vial-gui) for the
**[IO Type 84 Pro Wireless QMK firmware](https://github.com/backflipinc/io-type84-pro-qmk)**:
the Hi-Tech / Military Black look of the keyboard's on-screen UI, plus pages for everything the
firmware can do beyond stock Vial — two-colour effects with a live preview, the Fn overlay colour,
the light bar and auto-sleep.

![Lighting](docs/screenshots/lighting.png)

> [!IMPORTANT]
> For now Vial Armed is tailored to **one keyboard**: the IO by Red Square Type 84 Pro Wireless running
> the [custom QMK firmware](https://github.com/backflipinc/io-type84-pro-qmk). With any other Vial
> keyboard it works like regular Vial in the Armed theme — the extra pages only appear when the
> keyboard answers on the IO84 channel (see [How it works](#how-it-works)).

## Features

| | |
|---|---|
| 🖤 **Armed theme** | Black panels, amber accent, Rajdhani and Share Tech Mono, square keycaps, numbered side menu — the design code of the keyboard's screen. The stock Vial themes are still under **Theme** |
| 🌈 **Lighting** | Every effect the firmware has, including the 7 **two-colour** ones (TWO TONE, GRADIENT 2, CROSSFADE 2, SCANNER 2, STARLIGHT 2, REACTIVE 2, RIPPLE 2); COLOR and COLOR 2, brightness, speed |
| 👁️ **Live preview** | The selected effect runs on the keyboard layout with the firmware's own maths, LED positions and flags. Reactive effects press keys by themselves; click a key to press it |
| 🔦 **Fn overlay** | Pick the FN COLOR and hold **HOLD FN** to see which keys light up while Fn is held |
| 📏 **Light bar** | Mode (WPM, WPM bar, battery, link, scanner, off), colour and Caps Lock alert, with a 9-LED preview |
| 💤 **Device** | Auto-sleep timer in Bluetooth / 2.4 GHz: 1, 5, 10, 30 min or off |
| ⌨️ **Everything from Vial** | Keymap and knobs on all layers, macros, tap dance, combos, key overrides, QMK settings, matrix tester |
| 🧭 **Status** | Link and lock state in the top bar, Vial protocol, layer count and keyboard UID in the footer |

| Keymap | Device |
|---|---|
| ![Keymap](docs/screenshots/keymap.png) | ![Device](docs/screenshots/device.png) |

The original mockups are in [`design/`](design/).

## Install

Download the latest build from [Releases](https://github.com/backflipinc/vial-gui-armed/releases):

| | |
|---|---|
| Linux | `Vial-Armed-x86_64.AppImage` — `chmod +x` and run |
| Windows | `Vial-Armed-setup.exe` (installer) or `Vial-Armed-win.zip` (portable) |
| macOS | `Vial-Armed-mac.dmg` |

On Linux the keyboard needs a udev rule to be reachable without root — the firmware repository ships one:
[`udev/70-io-type84-pro.rules`](https://github.com/backflipinc/io-type84-pro-qmk/blob/main/udev/70-io-type84-pro.rules).

Set the keyboard's mode switch to **USB**: in Bluetooth / 2.4 GHz modes it is off the USB bus and Vial can't see it.

## How it works

Vial talks to the keyboard over Raw HID. VIA and VialRGB only know QMK's built-in effects and one colour,
so the firmware adds its own command, **`0x84`** (unused by VIA and Vial), that Vial Armed probes on connect:

| Sub-command | |
|---|---|
| `INFO` | `IO84` magic, protocol version, list sizes, maximum brightness |
| `LIST` | effects (QMK id + name), colour presets, light bar modes, sleep options, LEDs (matrix position, x/y, flags) |
| `GET` / `SET` | lighting on/off, effect, hue, saturation, brightness, speed, COLOR 2, FN COLOR, light bar mode / colour / Caps alert, auto-sleep |
| `SAVE` | write hue / saturation / brightness / speed to EEPROM (sliders apply them live without wearing the flash) |

Everything — effect names, colours, LED layout — comes from the keyboard, so the GUI hardcodes nothing
about it. If the keyboard doesn't answer, Vial Armed falls back to the stock Vial lighting page.
Implementation: [`protocol/io84.py`](src/main/python/protocol/io84.py) here,
[`io84_hid.c`](https://github.com/backflipinc/io-type84-pro-qmk/blob/main/keyboards/io/type84_pro/io84_hid.c) in the firmware.

## Development

Python 3.6 is what the release builds use (`fbs`); newer Python with PyQt5 5.15 works for development.

```sh
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
fbs run
```

Linux AppImage, the same way CI builds it (needs Docker):

```sh
./util/linux-builder/build-in-docker.sh   # -> util/linux-builder/output/Vial-x86_64.AppImage
```

Releases are built by GitHub Actions: pushing a `v*` tag builds Linux, Windows and macOS and attaches them to a release.

## Roadmap

- More of the on-screen menu: display accent and timeout, battery and link status
- Rename the app and the installers (they are still called Vial inside)

## Credits

- [Vial](https://get.vial.today) / [vial-gui](https://github.com/vial-kb/vial-gui) — the application this is built on
- Fonts: [Rajdhani](https://fonts.google.com/specimen/Rajdhani) and
  [Share Tech Mono](https://fonts.google.com/specimen/Share+Tech+Mono) (SIL Open Font License, see `src/main/resources/base/fonts/`)

Not affiliated with IO by Red Square or the Vial project.

## License

GPL-2.0-or-later, like Vial — see [COPYING](COPYING).
