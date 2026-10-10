# IO//84 Vial — design mockups

Hi-Tech / Military Black, the same design code as the keyboard's on-screen UI.

`mockups.html` holds all screens; `png/` has them rendered at 1280×800.

| Screen | |
|---|---|
| `keymap` | Layers, live remap, our keycodes in an **IO/84** palette tab |
| `device` | **IO/84 DEVICE** tab: SnapTap, colours, light bar, display, power, wireless |
| `lighting` | Effect list with the two-colour effects, HSV for COLOR / COLOR 2, live preview |
| `unlock` | Caps + Enter unlock with mirrored keys and progress |
| `danger` | Red confirm for bootloader / factory reset |

## Tokens

| | |
|---|---|
| Background | `#000`, panels `#0a0a0a`, desk `#1b1c1e` |
| Lines | `#2a2a2a`, `#3a3a3a` |
| Text | `#dcdcdc`, mid `#8f8f8f`, dim `#5c5c5c` |
| Accent | amber `#ffaa00` (follows the keyboard's DISPLAY > ACCENT) |
| Status | ok `#2fd05a`, danger `#ff3b2f` |
| Fonts | Rajdhani Bold — headings, nav; Share Tech Mono — everything else |
| Shapes | square corners, 1 px lines, corner brackets on framed panels, selected = amber border + left bar |

## Render

```sh
for s in keymap device lighting unlock danger; do
  chromium --headless=new --hide-scrollbars --window-size=1280,800 \
    --screenshot=png/$s.png "file://$PWD/mockups.html?s=$s"
done
```
