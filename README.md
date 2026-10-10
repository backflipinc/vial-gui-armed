### vial-gui-armed

A fork of [vial-gui](https://github.com/vial-kb/vial-gui) with the **Armed** theme — Hi-Tech / Military Black,
the design code of the [IO Type 84 Pro QMK firmware](https://github.com/backflipinc/io-type84-pro-qmk) on-screen UI:
black panels, amber accent, Rajdhani and Share Tech Mono, square keycaps and a numbered side menu.

Armed is the default theme; the stock Vial themes are still under **Theme**. Mockups live in [`design/`](design/).

---


# Docs and getting started

### Please visit [get.vial.today](https://get.vial.today/) to get started with Vial

Vial is an open-source cross-platform (Windows, Linux and Mac) GUI and a QMK fork for configuring your keyboard in real time.


![](https://get.vial.today/img/vial-win-1.png)


---


#### Releases

Visit https://get.vial.today/ to download a binary release of Vial.

#### Development

Python 3.6 is recommended (3.6 is the latest version that is officially supported by `fbs`).

Install dependencies:

```
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

To launch the application afterwards:

```
source venv/bin/activate
fbs run
```
