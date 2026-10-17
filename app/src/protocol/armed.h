#pragma once

#include "hid/link.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

#include <array>
#include <functional>
#include <optional>
#include <stdexcept>

// qmk-armed (command 0xAD): settings VIA / Vial don't cover; see docs/armed.md in qmk-armed.
// Synchronous, runs on the device's worker thread.
namespace armed {

using hid::Link;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr quint8 CMD_ARMED = 0xAD;
inline constexpr quint8 PROTOCOL = 1;
inline constexpr quint8 UNHANDLED = 0xFF;

enum Sub : quint8 {
    SUB_INFO, SUB_LIST, SUB_GET, SUB_SET, SUB_SAVE,
    SUB_SCREEN_INFO, SUB_SCREEN_BEGIN, SUB_SCREEN_DATA, SUB_SCREEN_END, SUB_SCREEN_READ,
    SUB_THEME_INFO, SUB_THEME_READ, SUB_THEME_WRITE, SUB_THEME_SAVE, SUB_THEME_REVERT,
    SUB_CLOCK_GET, SUB_CLOCK_SET, SUB_BOOTLOADER,
    SUB_HUD_INFO, SUB_HUD_SOURCE, SUB_HUD_VALUE, SUB_HUD_FONT, SUB_HUD_PALETTE, SUB_HUD_READ, SUB_HUD_WRITE,
    SUB_HUD_APPLY, SUB_HUD_REVERT, SUB_HUD_PUSH, SUB_HUD_TEXT,
};

enum ListKind : quint8 { L_EFFECTS, L_COLORS, L_STRIP_MODES, L_SLEEP, L_LEDS, L_SNAPTAP_MODES, L_ACCENTS, L_SCREEN_OFF };
inline constexpr int L_COUNT = 5; // sized in INFO; the later lists are read until the keyboard answers 0xFF
inline constexpr int LIST_MAX = 32;

enum Value : quint8 {
    V_ENABLED, V_EFFECT, V_COLOR2, V_FN_COLOR, V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS, V_SLEEP,
    V_HUE, V_SAT, V_VAL, V_SPEED, V_SCREEN, V_CLOCK, V_NKRO, V_GUI_LOCK, V_SNAPTAP, V_ACCENT, V_SCREEN_OFF,
    V_BT_SLOT,
};

enum Feature : quint16 {
    F_RGB_MATRIX = 1 << 0,
    F_COLOR2 = 1 << 1,
    F_FN_COLOR = 1 << 2,
    F_STRIP = 1 << 3,
    F_SLEEP = 1 << 4,
    F_SCREEN = 1 << 5,
    F_THEMES = 1 << 6,
    F_CLOCK = 1 << 7,
    F_HUD = 1 << 8,
    F_KEYMAP = 1 << 9,
    F_SNAPTAP = 1 << 10,
    F_DISPLAY = 1 << 11,
    F_BT_SLOT = 1 << 12,
};

inline constexpr int MSG_LEN = 32;
inline constexpr int SCREEN_DATA_PAYLOAD = MSG_LEN - 4;
inline constexpr int SCREEN_READ_PAYLOAD = MSG_LEN - 7;
inline constexpr int SCREEN_WINDOW = 8; // requests in flight during an upload / read
inline constexpr int THEME_PAYLOAD = MSG_LEN - 6;
inline constexpr int HUD_PAYLOAD = MSG_LEN - 5;
inline constexpr int HUD_FONT_PAYLOAD = MSG_LEN - 8;
inline constexpr int HUD_PUSH_SLOTS = 4;
inline constexpr int HUD_TEXT_PAYLOAD = MSG_LEN - 7;

struct Effect {
    quint8 qmkId = 0, flags = 0;
    QString name;
    bool usesColor2() const { return flags & 0x01; }
    bool theme() const { return flags & 0x02; }
};

struct Color {
    quint8 h = 0, s = 0;
    QString name;
    bool black() const { return h == 0 && s == 1; }
};

struct Led {
    quint8 row = 0, col = 0, x = 0, y = 0, flags = 0;
    bool modifier() const { return flags & 0x01; }
};

struct ScreenOff {
    quint16 seconds = 0;
    QString name;
};

struct Info {
    std::array<int, 3> version{};
    quint16 features = 0;
    int maxBrightness = 255;
    QList<Effect> effects;
    QList<Color> colors;
    QStringList stripModes;
    QList<int> sleepMinutes;
    QList<Led> leds;
    QStringList snaptapModes;
    QList<Color> accents;
    QList<ScreenOff> screenOff;
    QMap<int, quint8> values;

    bool has(quint16 f) const { return features & f; }
    QString versionString() const;
};

// Lists that only change with the firmware, kept on disk per keyboard and firmware version:
// ~150 requests saved on every connect (seconds through the dongle). Same files as the Python app.
class ListCache {
public:
    ListCache() = default; // disabled
    ListCache(const QString &dir, quint64 uid, const QString &version, quint16 features);
    QList<QByteArray> get(const QString &name, const std::function<QList<QByteArray>()> &fetch);

private:
    QString path_;
};

std::optional<Info> probe(Link &link, qint64 vialProtocol, std::optional<quint64> keyboardId);
QMap<int, quint8> readValues(Link &link, quint16 features);
void set(Link &link, quint8 vid, int value);
void save(Link &link);

// the keyboard's local wall time; it knows no time zones
std::optional<QDateTime> clockGet(Link &link);
void clockSet(Link &link, const QDateTime &local);

void bootloader(Link &link);

using Progress = std::function<void(qint64 done, qint64 total)>;
using Cancelled = std::function<bool()>;

struct ScreenInfo {
    int width = 0, height = 0, format = 0, maxFrames = 0, frames = 0;
    quint32 capacity = 0;
    int header = 0;
    int frameBytes() const { return width * height * 2; }
};
std::optional<ScreenInfo> screenInfo(Link &link);
QByteArray screenRead(Link &link, quint32 offset, quint32 size, const Progress &progress = {},
                      const Cancelled &cancelled = {});
// size: bytes the keyboard stores; stream: the encoded animation
void screenUpload(Link &link, quint32 size, const QByteArray &stream, const Progress &progress = {},
                  const Cancelled &cancelled = {});

struct ThemeInfo {
    int slotCount = 0, layers = 0, leds = 0, size = 0, format = 0;
    QList<int> modes;
};
std::optional<ThemeInfo> themeInfo(Link &link);
QByteArray themeRead(Link &link, int slot, int size);
void themeWrite(Link &link, int slot, int offset, const QByteArray &payload);
void themeSave(Link &link, int slot);
void themeRevert(Link &link, int slot);

struct HudInfo {
    int format = 0, maxWidgets = 0, widgetSize = 0, layoutSize = 0, strings = 0, hostValues = 0, hostTexts = 0,
        textLen = 0, fonts = 0, colors = 0, sources = 0, width = 0, height = 0, graphs = 0, graphPoints = 0;
};
struct HudSource {
    int id = 0, kind = 0;
    std::optional<int> dec; // nothing: the host decides (host slots)
    QString name;
};
struct HudValue {
    bool valid = false;
    int hint = 0, dec = 0;
    qint32 num = 0;
    QString text;
};
std::optional<HudInfo> hudInfo(Link &link);
QList<HudSource> hudSources(Link &link, const HudInfo &info, ListCache &cache);
std::optional<HudValue> hudValue(Link &link, int source);
QList<std::array<quint8, 3>> hudPalette(Link &link, const HudInfo &info);
// the keyboard's QFF font; pipelined over USB, one by one over the dongle
QByteArray hudFont(Link &link, int index, const Progress &progress = {});
// the font's first bytes: enough to tell whether a cached copy is still the same font
QByteArray hudFontHead(Link &link, int index);
QByteArray hudRead(Link &link, const HudInfo &info);
// sends what differs from `previous` (the layout on the keyboard), then shows it
void hudWrite(Link &link, const QByteArray &blob, const std::optional<QByteArray> &previous);
void hudStore(Link &link);
void hudRevert(Link &link, bool factory);

struct HostValue {
    int slot = 0, dec = 0;
    qint64 num = 0;
};
// the keyboard sends no reply to these
QList<QByteArray> hudPushPackets(const QList<HostValue> &values, int ttl);
QList<QByteArray> hudTextPackets(int slot, const QString &text, int ttl, int limit);

}
