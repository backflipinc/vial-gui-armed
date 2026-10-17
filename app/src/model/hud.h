#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>
#include <variant>

// HUD layout of a qmk-armed keyboard: the main screen as a list of widgets.
// Byte layout matches armed_hud_layout_t in qmk-armed's quantum/armed/armed.h (docs/armed.md).
namespace hud {

inline constexpr int FORMAT = 1;
inline constexpr quint8 MAGIC = 0xB7;
inline constexpr int HEADER = 4;
inline constexpr int WIDGET_SIZE = 24;
inline constexpr quint16 NO_LABEL = 0xFFFF;

enum Type : quint8 { W_NONE, W_TEXT, W_BAR, W_GRAPH, W_INDICATOR, W_BATTERY, W_FRAME, W_TEXTBOX };
enum Frame : quint8 { FRAME_BRACKETS, FRAME_BOX, FRAME_HLINE, FRAME_VLINE, FRAME_PANEL };
enum Role : quint8 { C_BG, C_PANEL, C_LINE, C_DIM, C_TEXT, C_ACCENT, C_OK, C_WARN, C_AUTO = 0x0F };
enum Flag : quint8 { F_ALIGN = 0x03, F_WARN = 0x04, F_WARN_BELOW = 0x08, F_FILL = 0x10, F_TAIL = 0x20, F_DOT = 0x40 };
enum Source : quint8 {
    S_NONE, S_LAYER, S_WPM, S_BATTERY, S_VOLTAGE, S_CHARGING, S_LINK, S_MODE, S_TIME, S_DATE, S_CAPS, S_NUM,
    S_SCROLL, S_NKRO, S_LIGHT, S_GUI_LOCK, S_SNAPTAP, S_EFFECT, S_UPTIME, S_BRIGHTNESS,
    S_HOST = 0x40,
    S_HOST_TXT = 0x60,
};
enum Kind : quint8 { K_TEXT, K_NUMBER, K_FLAG, K_LINES };

QString typeName(int type);
QString frameName(int frame);
QString roleName(int role);
QString alignName(int align);

struct Widget {
    int type = W_TEXT, source = S_NONE, font = 0, fg = C_TEXT, bg = C_BG, flags = 0;
    int x = 0, y = 0, w = 0, h = 0;
    int lo = 0, hi = 0, warn = 0;
    int param = 0;
    std::optional<QString> label;

    static Widget make(int type, int x, int y); // what a fresh widget of each type looks like
    int align() const { return flags & F_ALIGN; }
    bool isStatic() const { return type == W_FRAME || (type == W_TEXT && source == S_NONE); }
    bool operator==(const Widget &o) const = default;
};

struct Sizes {
    int maxWidgets = 40;
    int strings = 512;
    int layoutSize() const { return HEADER + WIDGET_SIZE * maxWidgets + strings; }
};

struct Layout {
    QList<Widget> widgets;

    bool operator==(const Layout &o) const = default;
    // the keyboard's binary form, or why it doesn't fit
    std::variant<QByteArray, QString> pack(const Sizes &s) const;
    static std::optional<Layout> unpack(const QByteArray &blob, const Sizes &s);
    // the "vial-armed-hud" export file
    QByteArray toJson() const;
    static std::variant<Layout, QString> fromJson(const QByteArray &json);
};

}
