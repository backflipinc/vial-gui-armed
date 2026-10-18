#pragma once

#include "hid/link.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QSet>

#include <optional>

// VIA / Vial requests, synchronous: they run on the device's worker thread (hid::Session)
namespace vial {

using hid::Link;

enum : quint8 {
    CMD_VIA_GET_PROTOCOL_VERSION = 0x01,
    CMD_VIA_GET_KEYBOARD_VALUE = 0x02,
    CMD_VIA_SET_KEYBOARD_VALUE = 0x03,
    CMD_VIA_SET_KEYCODE = 0x05,
    CMD_VIA_LIGHTING_SET_VALUE = 0x07,
    CMD_VIA_LIGHTING_GET_VALUE = 0x08,
    CMD_VIA_LIGHTING_SAVE = 0x09,
    CMD_VIA_BOOTLOADER = 0x0B,
    CMD_VIA_MACRO_GET_COUNT = 0x0C,
    CMD_VIA_MACRO_GET_BUFFER_SIZE = 0x0D,
    CMD_VIA_MACRO_GET_BUFFER = 0x0E,
    CMD_VIA_MACRO_SET_BUFFER = 0x0F,
    CMD_VIA_GET_LAYER_COUNT = 0x11,
    CMD_VIA_KEYMAP_GET_BUFFER = 0x12,
    CMD_VIA_VIAL_PREFIX = 0xFE,

    VIA_LAYOUT_OPTIONS = 0x02,
    VIA_SWITCH_MATRIX_STATE = 0x03,

    QMK_BACKLIGHT_BRIGHTNESS = 0x09,
    QMK_BACKLIGHT_EFFECT = 0x0A,
    QMK_RGBLIGHT_BRIGHTNESS = 0x80,
    QMK_RGBLIGHT_EFFECT = 0x81,
    QMK_RGBLIGHT_EFFECT_SPEED = 0x82,
    QMK_RGBLIGHT_COLOR = 0x83,
    VIALRGB_GET_INFO = 0x40,
    VIALRGB_GET_MODE = 0x41,
    VIALRGB_GET_SUPPORTED = 0x42,
    VIALRGB_SET_MODE = 0x41,

    CMD_VIAL_GET_KEYBOARD_ID = 0x00,
    CMD_VIAL_GET_SIZE = 0x01,
    CMD_VIAL_GET_DEFINITION = 0x02,
    CMD_VIAL_GET_ENCODER = 0x03,
    CMD_VIAL_SET_ENCODER = 0x04,
    CMD_VIAL_GET_UNLOCK_STATUS = 0x05,
    CMD_VIAL_UNLOCK_START = 0x06,
    CMD_VIAL_UNLOCK_POLL = 0x07,
    CMD_VIAL_LOCK = 0x08,
    CMD_VIAL_QMK_SETTINGS_QUERY = 0x09,
    CMD_VIAL_QMK_SETTINGS_GET = 0x0A,
    CMD_VIAL_QMK_SETTINGS_SET = 0x0B,
    CMD_VIAL_QMK_SETTINGS_RESET = 0x0C,
    CMD_VIAL_DYNAMIC_ENTRY_OP = 0x0D,

    DYNAMIC_VIAL_GET_NUMBER_OF_ENTRIES = 0x00,
    DYNAMIC_VIAL_TAP_DANCE_GET = 0x01,
    DYNAMIC_VIAL_TAP_DANCE_SET = 0x02,
    DYNAMIC_VIAL_COMBO_GET = 0x03,
    DYNAMIC_VIAL_COMBO_SET = 0x04,
    DYNAMIC_VIAL_KEY_OVERRIDE_GET = 0x05,
    DYNAMIC_VIAL_KEY_OVERRIDE_SET = 0x06,
    DYNAMIC_VIAL_ALT_REPEAT_KEY_GET = 0x07,
    DYNAMIC_VIAL_ALT_REPEAT_KEY_SET = 0x08,
};

inline constexpr int kBufferChunk = 28;

inline constexpr int VIAL_PROTOCOL_ADVANCED_MACROS = 2;
inline constexpr int VIAL_PROTOCOL_MATRIX_TESTER = 3;
inline constexpr int VIAL_PROTOCOL_DYNAMIC = 4;
inline constexpr int VIAL_PROTOCOL_QMK_SETTINGS = 4;
inline constexpr int VIAL_PROTOCOL_EXT_MACROS = 5;
inline constexpr int VIAL_PROTOCOL_KEY_OVERRIDE = 5;

bool viaProtocolSupported(int via);
bool vialProtocolSupported(qint64 vial);

struct Identity {
    qint64 vialProtocol = -1; // unsigned on the wire: a VIA keyboard's 0xFFFFFFFF must stay unsupported
    quint64 keyboardId = 0;
};

int viaProtocol(Link &link);
Identity identity(Link &link);
QByteArray uid(Link &link);
// the keyboard's vial.json, decompressed
QByteArray definition(Link &link);
// the LZMA-compressed definition's size, and one 32-byte block of it
quint32 definitionSize(Link &link);
QByteArray definitionBlock(Link &link, quint32 block);
int layerCount(Link &link);

QByteArray keymapBuffer(Link &link, int size);
void setKeycode(Link &link, int layer, int row, int col, quint16 code);
QPair<quint16, quint16> encoder(Link &link, int layer, int index);
void setEncoder(Link &link, int layer, int index, int direction, quint16 code);
quint32 layoutOptions(Link &link);
void setLayoutOptions(Link &link, quint32 options);

struct MacroInfo {
    int count = 0;
    int memory = 0;
};
MacroInfo macroInfo(Link &link);
// exactly `count` NUL-terminated macros
QByteArray macroBuffer(Link &link, const MacroInfo &info);
void setMacroBuffer(Link &link, const QByteArray &data, int memory);

struct DynamicCounts {
    int tapDance = 0, combo = 0, keyOverride = 0, altRepeatKey = 0;
    quint8 featureBits = 0; // bit 0 caps word, bit 1 layer lock
};
DynamicCounts dynamicCounts(Link &link);
QByteArray dynamicEntry(Link &link, quint8 getOp, int index, int size);
void setDynamicEntry(Link &link, quint8 setOp, int index, const QByteArray &entry, int retries);

QSet<quint16> qmkSettingsSupported(Link &link);
// the setting's raw bytes, or nothing if the keyboard refused
std::optional<QByteArray> qmkSettingGet(Link &link, quint16 qsid);
int qmkSettingSet(Link &link, quint16 qsid, const QByteArray &value);
void qmkSettingsReset(Link &link);

struct UnlockStatus {
    bool unlocked = true;
    bool inProgress = false;
    QList<QPair<int, int>> keys; // row, col to hold
};
UnlockStatus unlockStatus(Link &link, int vialProtocol, int retries = 20);
void unlockStart(Link &link);
// [0] unlocked, [2] counter
QByteArray unlockPoll(Link &link);
void lock(Link &link);
QByteArray matrixState(Link &link);

QByteArray lightingGet(Link &link, const QByteArray &args, int retries = 20);
void lightingSet(Link &link, const QByteArray &args, int retries = 1);
void lightingSave(Link &link);

struct VialRgbInfo {
    int version = -1;
    int maxBrightness = -1;
};
VialRgbInfo vialRgbInfo(Link &link);
QSet<quint16> vialRgbSupported(Link &link);
struct VialRgbMode {
    quint16 mode = 0;
    quint8 speed = 0, h = 0, s = 0, v = 0;
};
VialRgbMode vialRgbMode(Link &link);
void setVialRgbMode(Link &link, const VialRgbMode &m);

void jumpToBootloader(Link &link);

}
