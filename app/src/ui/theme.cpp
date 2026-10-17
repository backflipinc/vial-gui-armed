#include "ui/theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>

namespace ui::theme {

QString css(const QColor &c)
{
    return c.name();
}

void loadFonts()
{
    QFontDatabase::addApplicationFont(":/resources/base/fonts/ShareTechMono-Regular.ttf");
    QFontDatabase::addApplicationFont(":/resources/base/fonts/Rajdhani-Bold.ttf");
}

QFont mono(int px)
{
    QFont f(QString::fromLatin1(MONO));
    f.setPixelSize(px);
    f.setLetterSpacing(QFont::PercentageSpacing, 106);
    return f;
}

QFont head(int px)
{
    QFont f(QString::fromLatin1(HEAD));
    f.setPixelSize(px);
    f.setWeight(QFont::Bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 112);
    return f;
}

namespace {

QString stylesheet()
{
    QString s = QStringLiteral(R"(
* { font-family: "@MONO@"; font-size: 13px; }
QMainWindow, QDialog { background: @BG@; }
QWidget { color: @TXT@; }
QToolTip { background: @PANEL@; color: @TXT@; border: 1px solid @ACC@; padding: 4px 6px; }

QMenuBar { background: @BG@; border-bottom: 1px solid @LINE@; padding: 1px 6px; }
QMenuBar::item { background: transparent; color: @MID@; padding: 4px 10px; }
QMenuBar::item:selected, QMenuBar::item:pressed { color: @ACC@; background: @ACC_BG@; }
QMenu { background: @PANEL@; border: 1px solid @LINE2@; padding: 4px 0; }
QMenu::item { padding: 5px 26px 5px 22px; color: @TXT@; }
QMenu::item:selected { background: @ACC_BG@; color: @ACC@; }
QMenu::item:disabled { color: @DIM@; }
QMenu::separator { height: 1px; background: @LINE@; margin: 4px 0; }
QMenu::indicator { width: 8px; height: 8px; margin-left: 8px; border: 1px solid @LINE2@; }
QMenu::indicator:checked { background: @ACC@; border-color: @ACC@; }

QPushButton, QToolButton { background: @PANEL@; border: 1px solid @LINE2@; color: @TXT@; padding: 5px 12px; border-radius: 0; }
QPushButton:hover, QToolButton:hover { border-color: @ACC@; color: @ACC@; }
QPushButton:focus, QToolButton:focus { border-color: @ACC_D@; }
QPushButton:focus:hover, QToolButton:focus:hover { border-color: @ACC@; }
QPushButton:pressed, QToolButton:pressed { background: @ACC@; color: @BG@; }
QPushButton:checked, QToolButton:checked { background: @ACC_BG@; border-color: @ACC@; color: @ACC@; }
QPushButton:disabled, QToolButton:disabled { color: @DIM@; border-color: @LINE@; }
QPushButton:default { border-color: @ACC@; color: @ACC@; }
QPushButton::menu-indicator { image: none; width: 0; }

QComboBox { background: @PANEL@; border: 1px solid @LINE2@; padding: 4px 28px 4px 10px; color: @TXT@; border-radius: 0; }
QComboBox:hover, QComboBox:focus { border-color: @ACC@; }
QComboBox:disabled { color: @DIM@; border-color: @LINE@; }
QComboBox::drop-down { border: 0; width: 24px; }
QComboBox::down-arrow { image: url(:/resources/base/armed/down.svg); width: 10px; height: 6px; margin-right: 8px; }
QComboBox QAbstractItemView { background: @PANEL@; border: 1px solid @ACC@; selection-background-color: @ACC_BG@; selection-color: @ACC@; outline: 0; }

QLineEdit, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit { background: @KEY@; border: 1px solid @LINE2@; padding: 4px 6px; color: @TXT@; selection-background-color: @ACC@; selection-color: @BG@; border-radius: 0; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @ACC@; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { color: @DIM@; border-color: @LINE@; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { background: @PANEL2@; border: 0; border-left: 1px solid @LINE2@; width: 16px; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/resources/base/armed/up.svg); width: 8px; height: 5px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/resources/base/armed/down-small.svg); width: 8px; height: 5px; }

QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; border: 1px solid @LINE2@; background: @KEY@; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover, QCheckBox:focus::indicator, QRadioButton:focus::indicator { border-color: @ACC@; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: @ACC@; border-color: @ACC@; }
QCheckBox:disabled, QRadioButton:disabled { color: @DIM@; }

QSlider { min-height: 16px; }
QSlider::groove:horizontal { height: 12px; background: @KEY@; border: 1px solid @LINE2@; }
QSlider::sub-page:horizontal { background: @ACC@; border: 1px solid @LINE2@; margin: 2px 0 2px 2px; }
QSlider::handle:horizontal { background: @TXT@; width: 4px; margin: -5px 0; border: 1px solid @BG@; }
QSlider::handle:horizontal:hover, QSlider:focus::handle:horizontal { background: @ACC@; }

QProgressBar { background: @LINE@; border: 0; height: 14px; color: transparent; }
QProgressBar::chunk { background: @ACC@; width: 18px; margin-right: 4px; }

QScrollArea { background: transparent; border: 0; }
QScrollBar:vertical { background: @BG@; width: 8px; margin: 0; }
QScrollBar:horizontal { background: @BG@; height: 8px; margin: 0; }
QScrollBar::handle { background: @LINE2@; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: @ACC@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QTabWidget::pane { border: 1px solid @LINE@; top: -1px; background: @BG@; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab { background: @BG@; color: @DIM@; border: 1px solid @LINE@; border-left: 0; padding: 7px 13px; margin: 0; }
QTabBar::tab:first { border-left: 1px solid @LINE@; }
QTabBar::tab:hover { color: @TXT@; }
QTabBar::tab:selected { color: @ACC@; background: @ACC_BG@; border-bottom: 2px solid @ACC@; }
QTabBar::scroller { width: 24px; }

QGroupBox { border: 1px solid @LINE@; margin-top: 18px; padding: 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; color: @MID@; }

QTableView, QListView, QTreeView { background: @PANEL@; border: 1px solid @LINE@; gridline-color: @LINE@; selection-background-color: @ACC_BG@; selection-color: @ACC@; outline: 0; }
QHeaderView::section { background: @BG@; color: @MID@; border: 0; border-right: 1px solid @LINE@; border-bottom: 1px solid @LINE@; padding: 4px 8px; }

QSplitter::handle { background: @BG@; }
QSplitter::handle:vertical { height: 7px; border-top: 3px solid @BG@; border-bottom: 3px solid @BG@; background: @LINE@; }
QSplitter::handle:horizontal { width: 7px; border-left: 3px solid @BG@; border-right: 3px solid @BG@; background: @LINE@; }
QSplitter::handle:hover { background: @ACC@; }

#armedTop { background: @BG@; border-bottom: 1px solid @LINE2@; }
#armedLogo { color: @ACC@; font-family: "@HEAD@"; font-size: 24px; font-weight: bold; }
#armedLogoSub { color: @MID@; font-family: "@HEAD@"; font-size: 24px; font-weight: bold; }
#armedChip { border: 1px solid @LINE2@; color: @MID@; padding: 4px 10px; }
#armedChip[state="ok"] { color: @TXT@; }
#armedChip[state="busy"] { color: @ACC@; border-color: @ACC_D@; }
#armedChip[state="error"] { color: @WARN@; border-color: @WARN@; }
#armedLock[unlocked="true"] { border-color: @ACC_D@; color: @ACC@; }
#armedFoot { background: @BG@; border-top: 1px solid @LINE2@; }
#armedFoot QLabel { color: @DIM@; }
#armedFootAcc { color: @ACC@; }
#armedNav { background: @BG@; border: 0; border-right: 1px solid @LINE2@; padding: 10px 0; outline: 0; }
#armedCrumb { font-family: "@HEAD@"; font-size: 22px; font-weight: bold; color: @ACC@; }
#armedSubNav QPushButton { padding: 5px 14px; margin: 0; border-right-width: 0; color: @MID@; }
#armedSubNav QPushButton:last-child { border-right-width: 1px; }
#armedSubNav QPushButton:checked { background: @ACC@; color: @BG@; border-color: @ACC@; }
#armedHint { color: @DIM@; font-size: 12px; }
#armedEmptyTitle { font-family: "@HEAD@"; font-size: 28px; font-weight: bold; color: @TXT@; }
#armedEmptyText { color: @MID@; font-size: 14px; }
#armedToast { background: @PANEL@; border: 1px solid @LINE2@; border-left: 4px solid @ACC@; }
#armedToast[kind="error"] { border-left-color: @WARN@; }
#armedToast[kind="ok"] { border-left-color: @OK@; }
#armedBusyBar { background: transparent; }
)");
    const QList<QPair<QString, QString>> vars = {
        {"MONO", MONO}, {"HEAD", HEAD}, {"BG", css(BG)}, {"PANEL2", css(PANEL2)}, {"PANEL", css(PANEL)},
        {"KEY", css(KEY)}, {"LINE2", css(LINE2)}, {"LINE", css(LINE)}, {"DIM", css(DIM)}, {"MID", css(MID)},
        {"TXT", css(TXT)}, {"ACC_D", css(ACC_D)}, {"ACC_BG", css(ACC_BG)}, {"ACC", css(ACC)}, {"OK", css(OK)},
        {"WARN", css(WARN)},
    };
    for (const auto &[k, v] : vars)
        s.replace(QLatin1Char('@') + k + QLatin1Char('@'), v);
    return s;
}

}

void apply()
{
    QPalette p;
    p.setColor(QPalette::Window, BG);
    p.setColor(QPalette::WindowText, TXT);
    p.setColor(QPalette::Base, PANEL);
    p.setColor(QPalette::AlternateBase, PANEL2);
    p.setColor(QPalette::ToolTipBase, PANEL);
    p.setColor(QPalette::ToolTipText, TXT);
    p.setColor(QPalette::PlaceholderText, DIM);
    p.setColor(QPalette::Text, TXT);
    p.setColor(QPalette::Button, PANEL);
    p.setColor(QPalette::ButtonText, TXT);
    p.setColor(QPalette::BrightText, ACC);
    p.setColor(QPalette::Link, ACC);
    p.setColor(QPalette::Highlight, ACC);
    p.setColor(QPalette::HighlightedText, BG);
    for (auto role : {QPalette::ButtonText, QPalette::WindowText, QPalette::Text})
        p.setColor(QPalette::Disabled, role, DIM);
    p.setColor(QPalette::Disabled, QPalette::Light, BG);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(p);
    qApp->setStyleSheet(stylesheet());
}

}
