#include "model/dynamic.h"

#include "core/bytes.h"
#include "keycodes/keycodes.h"

#include <QJsonArray>
#include <QJsonObject>

namespace dynamic {
namespace {

using bytes::le16;
using bytes::putLe16;
using bytes::u8;

QString name(int code)
{
    return keycodes::Registry::instance().serialize(code);
}

quint16 code(const QString &name)
{
    return quint16(keycodes::Registry::instance().deserialize(name));
}

// a layout file keycode: a name, or a number from old files
QString keyFrom(const QJsonValue &v)
{
    return v.isString() ? v.toString() : name(v.toInt());
}

}

TapDance TapDance::unpack(const QByteArray &d)
{
    TapDance t;
    for (int i = 0; i < 4; ++i)
        t.keys[size_t(i)] = name(le16(d, i * 2));
    t.tappingTerm = le16(d, 8);
    return t;
}

QByteArray TapDance::pack() const
{
    QByteArray d;
    for (const QString &k : keys)
        putLe16(d, code(k));
    putLe16(d, quint16(tappingTerm));
    return d;
}

QJsonValue TapDance::save() const
{
    return QJsonArray{keys[0], keys[1], keys[2], keys[3], tappingTerm};
}

TapDance TapDance::restore(const QJsonValue &v)
{
    QJsonArray a = v.toArray();
    TapDance t;
    for (int i = 0; i < 4; ++i)
        t.keys[size_t(i)] = keyFrom(a.at(i));
    t.tappingTerm = a.at(4).toInt();
    return t;
}

Combo Combo::unpack(const QByteArray &d)
{
    Combo c;
    for (int i = 0; i < 4; ++i)
        c.keys[size_t(i)] = name(le16(d, i * 2));
    c.output = name(le16(d, 8));
    return c;
}

QByteArray Combo::pack() const
{
    QByteArray d;
    for (const QString &k : keys)
        putLe16(d, code(k));
    putLe16(d, code(output));
    return d;
}

QJsonValue Combo::save() const
{
    return QJsonArray{keys[0], keys[1], keys[2], keys[3], output};
}

Combo Combo::restore(const QJsonValue &v)
{
    QJsonArray a = v.toArray();
    Combo c;
    for (int i = 0; i < 4; ++i)
        c.keys[size_t(i)] = keyFrom(a.at(i));
    c.output = keyFrom(a.at(4));
    return c;
}

KeyOverride KeyOverride::unpack(const QByteArray &d)
{
    return {name(le16(d, 0)), name(le16(d, 2)), le16(d, 4), u8(d, 6), u8(d, 7), u8(d, 8), u8(d, 9)};
}

QByteArray KeyOverride::pack() const
{
    QByteArray d;
    putLe16(d, code(trigger));
    putLe16(d, code(replacement));
    putLe16(d, layers);
    d += char(triggerMods);
    d += char(negativeModMask);
    d += char(suppressedMods);
    // bit 6 has no meaning and is dropped, like the Python app's KeyOverrideOptions
    d += char(options & 0xBF);
    return d;
}

QJsonValue KeyOverride::save() const
{
    return QJsonObject{{"trigger", trigger},
                       {"replacement", replacement},
                       {"layers", layers},
                       {"trigger_mods", triggerMods},
                       {"negative_mod_mask", negativeModMask},
                       {"suppressed_mods", suppressedMods},
                       {"options", options & 0xBF}};
}

KeyOverride KeyOverride::restore(const QJsonValue &v)
{
    QJsonObject o = v.toObject();
    return {keyFrom(o["trigger"]), keyFrom(o["replacement"]), quint16(o["layers"].toInt()),
            quint8(o["trigger_mods"].toInt()), quint8(o["negative_mod_mask"].toInt()),
            quint8(o["suppressed_mods"].toInt()), quint8(o["options"].toInt() & 0xBF)};
}

AltRepeatKey AltRepeatKey::unpack(const QByteArray &d)
{
    return {name(le16(d, 0)), name(le16(d, 2)), u8(d, 4), u8(d, 5)};
}

QByteArray AltRepeatKey::pack() const
{
    QByteArray d;
    putLe16(d, code(keycode));
    putLe16(d, code(altKeycode));
    d += char(allowedMods);
    d += char(options & 0x0F);
    return d;
}

QJsonValue AltRepeatKey::save() const
{
    return QJsonObject{{"keycode", keycode},
                       {"alt_keycode", altKeycode},
                       {"allowed_mods", allowedMods},
                       {"options", options & 0x0F}};
}

AltRepeatKey AltRepeatKey::restore(const QJsonValue &v)
{
    QJsonObject o = v.toObject();
    return {keyFrom(o["keycode"]), keyFrom(o["alt_keycode"]), quint8(o["allowed_mods"].toInt()),
            quint8(o["options"].toInt() & 0x0F)};
}

}
