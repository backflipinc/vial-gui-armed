# SPDX-License-Identifier: GPL-2.0-or-later
# Hi-Tech / Military Black theme, same design code as the IO Type 84 Pro screen UI

import os

from PyQt5.QtGui import QFontDatabase, QFont, QColor

BG = "#000000"
PANEL = "#0a0a0a"
PANEL2 = "#111111"
KEY = "#070707"
LINE = "#2a2a2a"
LINE2 = "#3a3a3a"
DIM = "#5c5c5c"
MID = "#8f8f8f"
TXT = "#dcdcdc"
ACC = "#ffaa00"
ACC_D = "#6b4700"
ACC_BG = "#1a1100"
OK = "#2fd05a"
WARN = "#ff3b2f"

MONO = "Share Tech Mono"
HEAD = "Rajdhani"

palette = {
    "Window": BG,
    "WindowText": TXT,
    "Base": PANEL,
    "AlternateBase": PANEL2,
    "ToolTipBase": PANEL,
    "ToolTipText": TXT,
    "Text": TXT,
    "Button": PANEL,
    "ButtonText": TXT,
    "BrightText": ACC,
    "Link": ACC,
    "Highlight": ACC,
    "HighlightedText": BG,
    "Active.Button": PANEL,
    "Disabled.ButtonText": DIM,
    "Disabled.WindowText": DIM,
    "Disabled.Text": DIM,
    "Disabled.Light": BG,
}

_fonts_loaded = False
_icons = ""


def load_fonts(get_resource):
    global _fonts_loaded, _icons
    if _fonts_loaded:
        return
    try:
        _icons = os.path.dirname(get_resource(os.path.join("armed", "down.svg"))).replace("\\", "/")
    except Exception:
        pass
    for name in ("ShareTechMono-Regular.ttf", "Rajdhani-Bold.ttf"):
        try:
            path = get_resource(os.path.join("fonts", name))
        except Exception:
            continue
        QFontDatabase.addApplicationFont(path)
    _fonts_loaded = True


def mono(px=13):
    f = QFont(MONO)
    f.setPixelSize(px)
    f.setLetterSpacing(QFont.PercentageSpacing, 106)
    return f


def head(px=17):
    f = QFont(HEAD)
    f.setPixelSize(px)
    f.setWeight(QFont.Bold)
    f.setLetterSpacing(QFont.PercentageSpacing, 112)
    return f


def color(c):
    return QColor(c)


STYLESHEET = """
* {{ font-family: "{MONO}"; font-size: 13px; }}
QMainWindow, QDialog {{ background: {BG}; }}
QWidget {{ color: {TXT}; }}
QToolTip {{ background: {PANEL}; color: {TXT}; border: 1px solid {ACC}; padding: 4px 6px; }}

QMenuBar {{ background: {BG}; border-bottom: 1px solid {LINE}; padding: 1px 6px; }}
QMenuBar::item {{ background: transparent; color: {MID}; padding: 4px 10px; }}
QMenuBar::item:selected, QMenuBar::item:pressed {{ color: {ACC}; background: {ACC_BG}; }}
QMenu {{ background: {PANEL}; border: 1px solid {LINE2}; padding: 4px 0; }}
QMenu::item {{ padding: 5px 26px 5px 22px; color: {TXT}; }}
QMenu::item:selected {{ background: {ACC_BG}; color: {ACC}; }}
QMenu::item:disabled {{ color: {DIM}; }}
QMenu::separator {{ height: 1px; background: {LINE}; margin: 4px 0; }}
QMenu::indicator {{ width: 8px; height: 8px; margin-left: 8px; border: 1px solid {LINE2}; }}
QMenu::indicator:checked {{ background: {ACC}; border-color: {ACC}; }}

QPushButton, QToolButton {{ background: {PANEL}; border: 1px solid {LINE2}; color: {TXT}; padding: 5px 12px; border-radius: 0; }}
QPushButton:hover, QToolButton:hover {{ border-color: {ACC}; color: {ACC}; }}
QPushButton:pressed, QToolButton:pressed {{ background: {ACC}; color: {BG}; }}
QPushButton:checked, QToolButton:checked {{ background: {ACC_BG}; border-color: {ACC}; color: {ACC}; }}
QPushButton:disabled, QToolButton:disabled {{ color: {DIM}; border-color: {LINE}; }}
QPushButton:default {{ border-color: {ACC}; color: {ACC}; }}
SquareButton {{ padding: 0; }}
QPushButton:checked:disabled {{ background: {ACC_BG}; border-color: {ACC}; color: {ACC}; }}
QPushButton::menu-indicator {{ image: none; width: 0; }}

QComboBox {{ background: {PANEL}; border: 1px solid {LINE2}; padding: 4px 28px 4px 10px; color: {TXT}; border-radius: 0; }}
QComboBox:hover {{ border-color: {ACC}; }}
QComboBox::drop-down {{ border: 0; width: 24px; }}
QComboBox::down-arrow {{ image: url({ICONS}/down.svg); width: 10px; height: 6px; margin-right: 8px; }}
QComboBox QAbstractItemView {{ background: {PANEL}; border: 1px solid {ACC}; selection-background-color: {ACC_BG}; selection-color: {ACC}; outline: 0; }}

QLineEdit, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit {{ background: {KEY}; border: 1px solid {LINE2}; padding: 4px 6px; color: {TXT}; selection-background-color: {ACC}; selection-color: {BG}; border-radius: 0; }}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus {{ border-color: {ACC}; }}
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {{ background: {PANEL2}; border: 0; border-left: 1px solid {LINE2}; width: 16px; }}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {{ image: url({ICONS}/up.svg); width: 8px; height: 5px; }}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {{ image: url({ICONS}/down-small.svg); width: 8px; height: 5px; }}

QCheckBox, QRadioButton {{ spacing: 8px; }}
QCheckBox::indicator, QRadioButton::indicator {{ width: 12px; height: 12px; border: 1px solid {LINE2}; background: {KEY}; }}
QCheckBox::indicator:hover, QRadioButton::indicator:hover {{ border-color: {ACC}; }}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {{ background: {ACC}; border-color: {ACC}; }}

QSlider::groove:horizontal {{ height: 12px; background: {KEY}; border: 1px solid {LINE2}; }}
QSlider::sub-page:horizontal {{ background: {ACC}; border: 1px solid {LINE2}; margin: 2px 0 2px 2px; }}
QSlider::handle:horizontal {{ background: {TXT}; width: 4px; margin: -5px 0; border: 1px solid {BG}; }}
QSlider::handle:horizontal:hover {{ background: {ACC}; }}

QProgressBar {{ background: {LINE}; border: 0; height: 14px; color: transparent; }}
QProgressBar::chunk {{ background: {ACC}; width: 18px; margin-right: 4px; }}

QScrollArea {{ background: transparent; border: 0; }}
QScrollBar:vertical {{ background: {BG}; width: 8px; margin: 0; }}
QScrollBar:horizontal {{ background: {BG}; height: 8px; margin: 0; }}
QScrollBar::handle {{ background: {LINE2}; min-height: 30px; min-width: 30px; }}
QScrollBar::handle:hover {{ background: {ACC}; }}
QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
QScrollBar::add-page, QScrollBar::sub-page {{ background: none; }}

QTabWidget::pane {{ border: 1px solid {LINE}; top: -1px; background: {BG}; }}
QTabBar {{ qproperty-drawBase: 0; }}
QTabBar::tab {{ background: {BG}; color: {DIM}; border: 1px solid {LINE}; border-left: 0; padding: 7px 13px; margin: 0; }}
QTabBar::tab:first {{ border-left: 1px solid {LINE}; }}
QTabBar::tab:hover {{ color: {TXT}; }}
QTabBar::tab:selected {{ color: {ACC}; background: {ACC_BG}; border-bottom: 2px solid {ACC}; }}
QTabBar::scroller {{ width: 24px; }}

QGroupBox {{ border: 1px solid {LINE}; margin-top: 18px; padding: 10px; }}
QGroupBox::title {{ subcontrol-origin: margin; left: 8px; color: {MID}; }}

QTableView, QListView, QTreeView {{ background: {PANEL}; border: 1px solid {LINE}; gridline-color: {LINE}; selection-background-color: {ACC_BG}; selection-color: {ACC}; outline: 0; }}
QHeaderView::section {{ background: {BG}; color: {MID}; border: 0; border-right: 1px solid {LINE}; border-bottom: 1px solid {LINE}; padding: 4px 8px; }}

QMessageBox QLabel {{ color: {TXT}; }}

#armedTop {{ background: {BG}; border-bottom: 1px solid {LINE2}; }}
#armedLogo {{ color: {ACC}; font-size: 22px; }}
#armedLogoSub {{ color: {MID}; font-size: 22px; }}
#armedStatus {{ color: {TXT}; }}
#armedLock {{ border: 1px solid {LINE2}; color: {MID}; padding: 4px 10px; }}
#armedLock[unlocked="true"] {{ border-color: {ACC_D}; color: {ACC}; }}
#armedFoot {{ background: {BG}; border-top: 1px solid {LINE2}; }}
#armedFoot QLabel {{ color: {DIM}; }}
#armedFootAcc {{ color: {ACC}; }}

#armedNav {{ background: {BG}; border: 0; border-right: 1px solid {LINE2}; padding: 10px 0; outline: 0; }}
#armedNav::item {{ border: 1px solid transparent; margin: 1px 8px; padding: 0; color: {TXT}; }}
#armedNav::item:hover {{ border-color: {LINE2}; }}
#armedNav::item:selected {{ border-color: {ACC}; border-left: 4px solid {ACC}; background: {BG}; color: {ACC}; }}

#armedMain::pane {{ border: 0; }}
#armedCrumb {{ font-family: "{HEAD}"; font-size: 22px; font-weight: bold; color: {MID}; }}
#armedFrameTitle {{ font-family: "{HEAD}"; font-size: 17px; font-weight: bold; color: {TXT}; }}
#armedHint {{ color: {DIM}; font-size: 12px; }}
#armedKey {{ color: {MID}; }}
#armedSection {{ color: {ACC}; font-size: 12px; border-bottom: 1px solid {LINE}; padding: 6px 0 3px 0; }}
#armedSwatch {{ padding: 0; border: 1px solid {LINE2}; }}
#armedSwatch:checked {{ border: 2px solid {ACC}; }}
#armedTool {{ padding: 4px 8px; }}
#armedSeg QPushButton {{ padding: 4px 10px; margin: 0; border-right-width: 0; }}
#armedSeg QPushButton:last-child {{ border-right-width: 1px; }}
#armedSeg QPushButton:checked {{ background: {ACC}; color: {BG}; border-color: {ACC}; }}
#armedList {{ background: {BG}; border: 1px solid {LINE}; padding: 6px 0; outline: 0; }}
#armedHue::groove:horizontal {{ border: 0; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #f00, stop:0.17 #ff0, stop:0.33 #0f0, stop:0.5 #0ff, stop:0.67 #00f, stop:0.83 #f0f, stop:1 #f00); }}
#armedHue::sub-page:horizontal {{ background: transparent; border: 0; }}
#armedNoDevices {{ color: {MID}; font-size: 14px; }}
"""


def stylesheet():
    return STYLESHEET.format(ICONS=_icons, **globals())
