#pragma once

#include "hid/link.h"
#include "model/definition.h"
#include "model/dynamic.h"
#include "protocol/armed.h"
#include "protocol/vial.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include <optional>

// What a connect reads from the keyboard, as raw codes: loaded on the device thread (loadRaw), then turned
// into names on the GUI thread (KeyboardState::fromRaw), where the keycode registry lives.
struct RawKeyboard {
    QByteArray definitionJson;
    int viaProtocol = -1;
    qint64 vialProtocol = -1;
    std::optional<quint64> keyboardId;
    int layers = 0;
    QByteArray keymap;                       // layers * rows * cols big-endian codes
    QList<QPair<quint16, quint16>> encoders; // [layer * encoderCount + index] = (cw, ccw); missing ones are 0
    std::optional<quint32> layoutOptions;
    vial::MacroInfo macroInfo;
    QByteArray macros;
    vial::DynamicCounts dynamicCounts;
    QList<QByteArray> tapDances, combos, keyOverrides, altRepeatKeys;
    QSet<quint16> supportedSettings;
    QMap<quint16, QByteArray> settings;

    // lighting
    QString lighting;
    QMap<int, int> qmkLighting; // value id -> byte(s), e.g. QMK_RGBLIGHT_COLOR -> h | s << 8
    vial::VialRgbInfo vialRgb;
    QSet<quint16> vialRgbEffects;
    vial::VialRgbMode vialRgbMode;

    std::optional<armed::Info> armed;
};

// sideloadJson: the VIA definition to use instead of asking the keyboard for its own
RawKeyboard loadRaw(hid::Link &link, const std::optional<QByteArray> &sideloadJson);

struct KeyboardState {
    Definition def;
    int viaProtocol = -1;
    qint64 vialProtocol = -1;
    std::optional<quint64> keyboardId;
    int layers = 0;
    QList<QString> keymap;   // [layer][row][col] flattened; null where the layout has no key
    QList<QString> encoders; // [layer][index][direction] flattened; null where the layout has no encoder
    qint64 layoutOptions = -1;
    vial::MacroInfo macroInfo;
    QByteArray macros;
    vial::DynamicCounts dynamicCounts;
    QSet<QString> features;
    QList<dynamic::TapDance> tapDances;
    QList<dynamic::Combo> combos;
    QList<dynamic::KeyOverride> keyOverrides;
    QList<dynamic::AltRepeatKey> altRepeatKeys;
    QSet<quint16> supportedSettings;
    QMap<quint16, quint32> settings;
    QString lighting;
    QMap<int, int> qmkLighting;
    vial::VialRgbInfo vialRgb;
    QSet<quint16> vialRgbEffects;
    vial::VialRgbMode vialRgbMode;
    std::optional<armed::Info> armed;

    // rebuilds the keycode registry for this keyboard, so call it on the GUI thread
    static std::variant<KeyboardState, QString> fromRaw(const RawKeyboard &raw);

    int keyIndex(int layer, int row, int col) const { return (layer * def.rows + row) * def.cols + col; }
    int encoderIndex(int layer, int index, int direction) const
    {
        return (layer * def.encoderCount + index) * 2 + direction;
    }
    QString key(int layer, int row, int col) const { return keymap.value(keyIndex(layer, row, col)); }
    QString encoder(int layer, int index, int direction) const
    {
        return encoders.value(encoderIndex(layer, index, direction));
    }

    // the .vil layout file
    QByteArray saveLayout() const;
};

// What a .vil file changes on the keyboard, in the Python app's restore order
struct LayoutRestore {
    struct Key {
        int layer, row, col;
        QString code;
    };
    struct Encoder {
        int layer, index, direction;
        QString code;
    };
    QList<Key> keys;
    QList<Encoder> encoders;
    std::optional<quint32> layoutOptions;
    std::optional<QByteArray> macros; // the whole buffer, when it differs
    QList<QPair<int, dynamic::TapDance>> tapDances;
    QList<QPair<int, dynamic::Combo>> combos;
    QList<QPair<int, dynamic::KeyOverride>> keyOverrides;
    QList<QPair<int, dynamic::AltRepeatKey>> altRepeatKeys;
    QList<QPair<quint16, quint32>> settings;

    bool touchesReset() const; // QK_BOOT anywhere: the keyboard must be unlocked first
    bool empty() const;
};

// throws std::runtime_error on a broken file
LayoutRestore planRestore(const KeyboardState &kb, const QByteArray &vil);
// applies the plan to the model (the writes go to the keyboard separately)
void applyRestore(KeyboardState &kb, const LayoutRestore &plan);
