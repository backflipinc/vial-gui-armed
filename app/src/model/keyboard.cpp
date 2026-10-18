#include "model/keyboard.h"

#include "core/bytes.h"
#include "core/lzma.h"
#include "keycodes/keycodes.h"
#include "model/macro.h"
#include "model/qmksettings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <stdexcept>

using keycodes::Registry;

namespace {

// the compressed definition as kept by an earlier connect, checked against its size and last block
QByteArray cachedDefinition(hid::Link &link, quint64 uid, const QString &dir)
{
    const quint32 size = vial::definitionSize(link);
    if (!size)
        throw hid::LinkError("the keyboard has no definition");
    const quint32 last = (size - 1) / hid::kMsgLen;
    const QByteArray tail = vial::definitionBlock(link, last).left(int(size - last * hid::kMsgLen));
    const QString path = QStringLiteral("%1/definition-%2-%3.xz").arg(dir).arg(uid, 16, 16, QLatin1Char('0')).arg(size);
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && f.size() == size) {
            const QByteArray payload = f.readAll();
            if (payload.endsWith(tail))
                if (auto json = lzma::decompress(payload))
                    return *json;
        }
    }
    QByteArray payload;
    payload.reserve(int(size));
    for (const QByteArray &d : vial::definitionBlocks(link, 0, last))
        payload += d;
    payload += tail;
    auto json = lzma::decompress(payload);
    if (!json)
        throw hid::LinkError("the keyboard definition is corrupt");
    QDir().mkpath(dir);
    QSaveFile w(path);
    if (w.open(QIODevice::WriteOnly) && w.write(payload) == payload.size())
        w.commit();
    return *json;
}

}

void loadParts(hid::Link &link, RawKeyboard &raw, int parts)
{
    parts &= AllParts & ~raw.parts;
    // qmk-armed packs several entries in a reply; ask unless the keyboard is known not to run it
    bool packed = !(raw.parts & ArmedPart) || raw.armed;
    auto entries = [&](int kind, QList<QByteArray> &list, quint8 op, int count, int size) {
        if (!(parts & kind) || !count)
            return;
        if (packed && raw.vialProtocol >= 0) {
            if (auto e = armed::readEntries(link, op, count, size)) {
                list = *e;
                return;
            }
            packed = false;
        }
        list = vial::dynamicEntries(link, op, count, size);
    };
    const vial::DynamicCounts &counts = raw.dynamicCounts;
    entries(TapDanceEntries, raw.tapDances, vial::DYNAMIC_VIAL_TAP_DANCE_GET, counts.tapDance, dynamic::TapDance::kSize);
    entries(ComboEntries, raw.combos, vial::DYNAMIC_VIAL_COMBO_GET, counts.combo, dynamic::Combo::kSize);
    entries(KeyOverrideEntries, raw.keyOverrides, vial::DYNAMIC_VIAL_KEY_OVERRIDE_GET, counts.keyOverride,
            dynamic::KeyOverride::kSize);
    entries(AltRepeatKeyEntries, raw.altRepeatKeys, vial::DYNAMIC_VIAL_ALT_REPEAT_KEY_GET, counts.altRepeatKey,
            dynamic::AltRepeatKey::kSize);

    if ((parts & MacrosPart) && raw.macroInfo.memory)
        raw.macros = vial::macroBuffer(link, raw.macroInfo);

    if ((parts & SettingsPart) && raw.vialProtocol >= vial::VIAL_PROTOCOL_QMK_SETTINGS) {
        raw.supportedSettings = vial::qmkSettingsSupported(link);
        QList<quint16> known;
        for (quint16 qsid : std::as_const(raw.supportedSettings))
            if (qmksettings::Catalog::instance().supported(qsid))
                known.append(qsid);
        std::sort(known.begin(), known.end());
        raw.settings = vial::qmkSettingsGet(link, known);
    }

    if (parts & LightingPart) {
        const bool rgblight = raw.lighting == QLatin1String("qmk_rgblight")
            || raw.lighting == QLatin1String("qmk_backlight_rgblight");
        const bool backlight = raw.lighting == QLatin1String("qmk_backlight")
            || raw.lighting == QLatin1String("qmk_backlight_rgblight");
        if (raw.lighting == QLatin1String("vialrgb")) {
            raw.vialRgb = vial::vialRgbInfo(link);
            if (raw.vialRgb.version != 1)
                throw hid::LinkError(QStringLiteral("Unsupported VialRGB protocol (%1), update your Vial version to latest")
                                         .arg(raw.vialRgb.version).toStdString());
            raw.vialRgbEffects = vial::vialRgbSupported(link);
            raw.vialRgbMode = vial::vialRgbMode(link);
        }
        auto value = [&](quint8 id) { return bytes::u8(vial::lightingGet(link, bytes::of({id})), 2); };
        if (rgblight) {
            for (quint8 id : {vial::QMK_RGBLIGHT_BRIGHTNESS, vial::QMK_RGBLIGHT_EFFECT, vial::QMK_RGBLIGHT_EFFECT_SPEED})
                raw.qmkLighting[id] = value(id);
            QByteArray c = vial::lightingGet(link, bytes::of({vial::QMK_RGBLIGHT_COLOR}));
            raw.qmkLighting[vial::QMK_RGBLIGHT_COLOR] = bytes::u8(c, 2) | bytes::u8(c, 3) << 8;
        }
        if (backlight) {
            for (quint8 id : {vial::QMK_BACKLIGHT_BRIGHTNESS, vial::QMK_BACKLIGHT_EFFECT})
                raw.qmkLighting[id] = value(id);
        }
    }

    if (parts & ArmedPart)
        raw.armed = armed::probe(link, raw.vialProtocol, raw.keyboardId);

    raw.parts |= parts;
}

RawKeyboard loadRaw(hid::Link &link, const std::optional<QByteArray> &sideloadJson, int parts,
                    const QString &definitionCache)
{
    RawKeyboard raw;
    raw.viaProtocol = vial::viaProtocol(link);
    if (sideloadJson) {
        raw.definitionJson = *sideloadJson;
    } else {
        auto id = vial::identity(link);
        raw.vialProtocol = id.vialProtocol;
        raw.keyboardId = id.keyboardId;
        raw.definitionJson = raw.keyboardId && !definitionCache.isEmpty()
            ? cachedDefinition(link, *raw.keyboardId, definitionCache)
            : vial::definition(link);
    }
    if (!vial::viaProtocolSupported(raw.viaProtocol) || !vial::vialProtocolSupported(raw.vialProtocol))
        throw std::runtime_error(QStringLiteral("Unsupported protocol version!\nVIA %1, Vial %2: update Vial Armed "
                                                "or the keyboard's firmware.")
                                     .arg(raw.viaProtocol).arg(raw.vialProtocol).toStdString());

    auto parsed = Definition::parse(raw.definitionJson);
    if (auto *err = std::get_if<QString>(&parsed))
        throw std::runtime_error(err->toStdString());
    const Definition &def = std::get<Definition>(parsed);

    raw.layers = vial::layerCount(link);
    raw.macroInfo = vial::macroInfo(link);
    raw.lighting = def.lighting;
    if (raw.vialProtocol >= vial::VIAL_PROTOCOL_DYNAMIC)
        raw.dynamicCounts = vial::dynamicCounts(link);

    raw.keymap = vial::keymapBuffer(link, raw.layers * def.rows * def.cols * 2);
    raw.encoders.resize(raw.layers * def.encoderCount);
    QList<QPair<int, int>> which;
    for (int layer = 0; layer < raw.layers; ++layer)
        for (int idx : def.encoderIndexes)
            which.append({layer, idx});
    const auto codes = vial::encoders(link, which);
    for (int i = 0; i < which.size(); ++i)
        raw.encoders[which[i].first * def.encoderCount + which[i].second] = codes[i];
    if (!def.layoutLabels.isUndefined() && !def.layoutLabels.isNull())
        raw.layoutOptions = vial::layoutOptions(link);

    loadParts(link, raw, parts);
    return raw;
}

std::variant<KeyboardState, QString> KeyboardState::fromRaw(const RawKeyboard &raw)
{
    auto parsed = Definition::parse(raw.definitionJson);
    if (auto *err = std::get_if<QString>(&parsed))
        return *err;

    KeyboardState kb;
    kb.def = std::get<Definition>(parsed);
    kb.viaProtocol = raw.viaProtocol;
    kb.vialProtocol = raw.vialProtocol;
    kb.keyboardId = raw.keyboardId;
    kb.layers = raw.layers;
    kb.macroInfo = raw.macroInfo;
    kb.dynamicCounts = raw.dynamicCounts;
    kb.lighting = raw.lighting;
    kb.layoutOptions = raw.layoutOptions ? qint64(*raw.layoutOptions) : -1;

    if (raw.vialProtocol >= vial::VIAL_PROTOCOL_DYNAMIC) {
        if (raw.dynamicCounts.featureBits & 0x01)
            kb.features.insert(QStringLiteral("caps_word"));
        if (raw.dynamicCounts.featureBits & 0x02)
            kb.features.insert(QStringLiteral("layer_lock"));
        if (raw.vialProtocol >= vial::VIAL_PROTOCOL_KEY_OVERRIDE)
            kb.features.insert(QStringLiteral("persistent_default_layer"));
        if (raw.dynamicCounts.altRepeatKey)
            kb.features.insert(QStringLiteral("repeat_key"));
    }

    keycodes::KeyboardTraits traits;
    traits.protocol = kb.vialProtocol;
    traits.layers = kb.layers;
    traits.macroCount = kb.macroInfo.count;
    traits.tapDanceCount = kb.dynamicCounts.tapDance;
    traits.customKeycodes = kb.def.customKeycodes;
    traits.midi = kb.def.midi;
    traits.features = kb.features;
    Registry &reg = Registry::instance();
    reg.rebuild(traits);

    const Definition &d = kb.def;
    kb.keymap.resize(kb.layers * d.rows * d.cols);
    for (int layer = 0; layer < kb.layers; ++layer) {
        for (const auto &[row, col] : d.matrix) {
            int i = kb.keyIndex(layer, row, col);
            kb.keymap[i] = reg.serialize(bytes::be16(raw.keymap, i * 2));
        }
    }
    kb.encoders.resize(kb.layers * d.encoderCount * 2);
    for (int layer = 0; layer < kb.layers; ++layer) {
        for (int idx : d.encoderIndexes) {
            auto pair = raw.encoders.value(layer * d.encoderCount + idx);
            kb.encoders[kb.encoderIndex(layer, idx, 0)] = reg.serialize(pair.first);
            kb.encoders[kb.encoderIndex(layer, idx, 1)] = reg.serialize(pair.second);
        }
    }
    kb.applyParts(raw);
    return kb;
}

void KeyboardState::applyParts(const RawKeyboard &raw)
{
    const int parts = raw.parts & ~loaded;
    auto entries = [&](int kind, auto &list, const QList<QByteArray> &blobs, auto unpack) {
        if (!(parts & kind))
            return;
        list.clear();
        for (const auto &e : blobs)
            list.append(unpack(e));
    };
    entries(TapDanceEntries, tapDances, raw.tapDances, dynamic::TapDance::unpack);
    entries(ComboEntries, combos, raw.combos, dynamic::Combo::unpack);
    entries(KeyOverrideEntries, keyOverrides, raw.keyOverrides, dynamic::KeyOverride::unpack);
    entries(AltRepeatKeyEntries, altRepeatKeys, raw.altRepeatKeys, dynamic::AltRepeatKey::unpack);
    if (parts & MacrosPart)
        macros = raw.macros;
    if (parts & SettingsPart) {
        supportedSettings = raw.supportedSettings;
        settings.clear();
        for (auto it = raw.settings.begin(); it != raw.settings.end(); ++it)
            settings[it.key()] = qmksettings::Catalog::instance().deserialize(it.key(), it.value());
    }
    if (parts & LightingPart) {
        qmkLighting = raw.qmkLighting;
        vialRgb = raw.vialRgb;
        vialRgbEffects = raw.vialRgbEffects;
        vialRgbMode = raw.vialRgbMode;
    }
    if (parts & ArmedPart)
        armed = raw.armed;
    loaded |= parts;
}

QByteArray KeyboardState::saveLayout() const
{
    QJsonArray layout;
    for (int l = 0; l < layers; ++l) {
        QJsonArray layer;
        for (int r = 0; r < def.rows; ++r) {
            QJsonArray row;
            for (int c = 0; c < def.cols; ++c) {
                QString k = key(l, r, c);
                row.append(k.isNull() ? QJsonValue(-1) : QJsonValue(k));
            }
            layer.append(row);
        }
        layout.append(layer);
    }
    QJsonArray encoderLayout;
    for (int l = 0; l < layers; ++l) {
        QJsonArray layer;
        for (int e = 0; e < def.encoderCount; ++e) {
            QString cw = encoder(l, e, 0), ccw = encoder(l, e, 1);
            layer.append(QJsonArray{cw.isNull() ? QJsonValue(-1) : QJsonValue(cw),
                                    ccw.isNull() ? QJsonValue(-1) : QJsonValue(ccw)});
        }
        encoderLayout.append(layer);
    }
    QJsonArray macros;
    for (const auto &m : macro::deserializeAll(this->macros, macroInfo.count, vialProtocol))
        macros.append(macro::save(m));
    QJsonArray td, combo, ko, ark;
    for (const auto &e : tapDances)
        td.append(e.save());
    for (const auto &e : combos)
        combo.append(e.save());
    for (const auto &e : keyOverrides)
        ko.append(e.save());
    for (const auto &e : altRepeatKeys)
        ark.append(e.save());
    QJsonObject settingsObj;
    for (auto it = settings.begin(); it != settings.end(); ++it)
        settingsObj.insert(QString::number(it.key()), qint64(it.value()));

    QJsonObject o{
        {"version", 1},
        {"uid", "@UID@"},
        {"layout", layout},
        {"encoder_layout", encoderLayout},
        {"layout_options", layoutOptions},
        {"macro", macros},
        {"vial_protocol", vialProtocol},
        {"via_protocol", viaProtocol},
        {"tap_dance", td},
        {"combo", combo},
        {"key_override", ko},
        {"alt_repeat_key", ark},
        {"settings", settingsObj},
    };
    // a 64-bit id would lose digits as a JSON double
    QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    json.replace("\"@UID@\"", keyboardId ? QByteArray::number(*keyboardId) : QByteArray("-1"));
    return json;
}

bool LayoutRestore::touchesReset() const
{
    const QString reset = QLatin1String(keycodes::kResetKeycode);
    if (macros)
        return true; // the Python app unlocks for any macro change
    for (const auto &k : keys)
        if (k.code == reset)
            return true;
    for (const auto &e : encoders)
        if (e.code == reset)
            return true;
    for (const auto &[i, t] : tapDances)
        if (std::find(t.keys.begin(), t.keys.end(), reset) != t.keys.end())
            return true;
    for (const auto &[i, c] : combos)
        if (c.output == reset)
            return true;
    for (const auto &[i, k] : keyOverrides)
        if (k.replacement == reset)
            return true;
    for (const auto &[i, a] : altRepeatKeys)
        if (a.keycode == reset || a.altKeycode == reset)
            return true;
    return false;
}

bool LayoutRestore::empty() const
{
    return keys.isEmpty() && encoders.isEmpty() && !layoutOptions && !macros && tapDances.isEmpty()
        && combos.isEmpty() && keyOverrides.isEmpty() && altRepeatKeys.isEmpty() && settings.isEmpty();
}

LayoutRestore planRestore(const KeyboardState &kb, const QByteArray &vil)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(vil, &err);
    if (!doc.isObject())
        throw std::runtime_error(QStringLiteral("not a layout file: %1").arg(err.errorString()).toStdString());
    const QJsonObject data = doc.object();
    const Registry &reg = Registry::instance();
    auto normalized = [&reg](const QJsonValue &v) {
        return v.isString() ? reg.normalize(v.toString()) : reg.serialize(v.toInt());
    };

    LayoutRestore plan;
    const QJsonArray layout = data.value("layout").toArray();
    for (int l = 0; l < layout.size() && l < kb.layers; ++l) {
        const QJsonArray layer = layout[l].toArray();
        for (int r = 0; r < layer.size() && r < kb.def.rows; ++r) {
            const QJsonArray row = layer[r].toArray();
            for (int c = 0; c < row.size() && c < kb.def.cols; ++c) {
                QString cur = kb.key(l, r, c);
                if (cur.isNull())
                    continue;
                QString code = normalized(row[c]);
                if (code != cur)
                    plan.keys.append({l, r, c, code});
            }
        }
    }
    const QJsonArray encoders = data.value("encoder_layout").toArray();
    for (int l = 0; l < encoders.size() && l < kb.layers; ++l) {
        const QJsonArray layer = encoders[l].toArray();
        for (int e = 0; e < layer.size() && e < kb.def.encoderCount; ++e) {
            const QJsonArray pair = layer[e].toArray();
            for (int dir = 0; dir < 2; ++dir) {
                QString cur = kb.encoder(l, e, dir);
                if (cur.isNull())
                    continue;
                QString code = normalized(pair.at(dir));
                if (code != cur)
                    plan.encoders.append({l, e, dir, code});
            }
        }
    }
    qint64 opts = data.value("layout_options").toInteger(-1);
    if (kb.layoutOptions != -1 && opts != -1 && opts != kb.layoutOptions)
        plan.layoutOptions = quint32(opts);

    if (data.value("macro").isArray()) {
        QList<macro::Macro> macros;
        for (const auto &m : data.value("macro").toArray())
            macros.append(macro::restore(m.toArray()));
        while (macros.size() < kb.macroInfo.count)
            macros.append(macro::Macro{});
        macros = macros.mid(0, kb.macroInfo.count);
        QByteArray buf = macro::serializeAll(macros, kb.macroInfo.count, kb.vialProtocol).left(kb.macroInfo.memory);
        if (buf != kb.macros)
            plan.macros = buf;
    }

    auto entries = [&](const char *key, int count, auto current, auto &out, auto restore) {
        const QJsonArray arr = data.value(QLatin1String(key)).toArray();
        for (int i = 0; i < arr.size() && i < count; ++i) {
            auto e = restore(arr[i]);
            if (!(e == current[i]))
                out.append({i, e});
        }
    };
    entries("tap_dance", kb.dynamicCounts.tapDance, kb.tapDances, plan.tapDances, dynamic::TapDance::restore);
    entries("combo", kb.dynamicCounts.combo, kb.combos, plan.combos, dynamic::Combo::restore);
    entries("key_override", kb.dynamicCounts.keyOverride, kb.keyOverrides, plan.keyOverrides,
            dynamic::KeyOverride::restore);
    entries("alt_repeat_key", kb.dynamicCounts.altRepeatKey, kb.altRepeatKeys, plan.altRepeatKeys,
            dynamic::AltRepeatKey::restore);

    const QJsonObject settings = data.value("settings").toObject();
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        bool ok = false;
        quint16 qsid = quint16(it.key().toUInt(&ok));
        if (!ok || !qmksettings::Catalog::instance().supported(qsid) || !kb.supportedSettings.contains(qsid))
            continue;
        quint32 v = quint32(it.value().toInteger());
        if (kb.settings.value(qsid) != v || !kb.settings.contains(qsid))
            plan.settings.append({qsid, v});
    }
    return plan;
}

void applyRestore(KeyboardState &kb, const LayoutRestore &plan)
{
    for (const auto &k : plan.keys)
        kb.keymap[kb.keyIndex(k.layer, k.row, k.col)] = k.code;
    for (const auto &e : plan.encoders)
        kb.encoders[kb.encoderIndex(e.layer, e.index, e.direction)] = e.code;
    if (plan.layoutOptions)
        kb.layoutOptions = *plan.layoutOptions;
    if (plan.macros)
        kb.macros = *plan.macros;
    for (const auto &[i, e] : plan.tapDances)
        kb.tapDances[i] = e;
    for (const auto &[i, e] : plan.combos)
        kb.combos[i] = e;
    for (const auto &[i, e] : plan.keyOverrides)
        kb.keyOverrides[i] = e;
    for (const auto &[i, e] : plan.altRepeatKeys)
        kb.altRepeatKeys[i] = e;
    for (const auto &[q, v] : plan.settings)
        kb.settings[q] = v;
}
