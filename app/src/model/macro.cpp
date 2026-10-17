#include "model/macro.h"

#include "keycodes/keycodes.h"

#include <stdexcept>

namespace macro {
namespace {

constexpr quint8 SS_QMK_PREFIX = 1;
constexpr quint8 SS_TAP_CODE = 1;
constexpr quint8 SS_DOWN_CODE = 2;
constexpr quint8 SS_UP_CODE = 3;
constexpr quint8 SS_DELAY_CODE = 4;
constexpr quint8 VIAL_MACRO_EXT_TAP = 5;
constexpr quint8 VIAL_MACRO_EXT_DOWN = 6;
constexpr quint8 VIAL_MACRO_EXT_UP = 7;
constexpr int VIAL_PROTOCOL_ADVANCED_MACROS = 2;

Kind kindOf(quint8 code)
{
    switch (code) {
    case SS_TAP_CODE: return Kind::Tap;
    case SS_DOWN_CODE: return Kind::Down;
    default: return Kind::Up;
    }
}

// consecutive same-kind codes and text bytes merge into one action, like the Python app
void appendKey(Macro &out, Kind kind, int code)
{
    QString name = keycodes::Registry::instance().serialize(code);
    if (!out.isEmpty() && out.last().kind == kind)
        out.last().keys.append(name);
    else
        out.append(Action{kind, {}, {name}, 0});
}

void appendChar(Macro &out, quint8 byte)
{
    if (!out.isEmpty() && out.last().kind == Kind::Text)
        out.last().text += QChar(byte);
    else
        out.append(Action{Kind::Text, QString(QChar(byte)), {}, 0});
}

}

QString Action::tag() const
{
    switch (kind) {
    case Kind::Text: return QStringLiteral("text");
    case Kind::Tap: return QStringLiteral("tap");
    case Kind::Down: return QStringLiteral("down");
    case Kind::Up: return QStringLiteral("up");
    case Kind::Delay: return QStringLiteral("delay");
    }
    return {};
}

QByteArray serialize(const Macro &m, qint64 vialProtocol)
{
    QByteArray out;
    for (const Action &a : m) {
        switch (a.kind) {
        case Kind::Text:
            out += a.text.toUtf8();
            break;
        case Kind::Delay:
            if (vialProtocol < VIAL_PROTOCOL_ADVANCED_MACROS)
                throw std::runtime_error("ActionDelay can only be used with vial_protocol>=2");
            out += char(SS_QMK_PREFIX);
            out += char(SS_DELAY_CODE);
            out += char(a.delay % 255 + 1);
            out += char(a.delay / 255 + 1);
            break;
        default:
            for (const QString &k : a.keys) {
                int kc = keycodes::Registry::instance().deserialize(k);
                if (vialProtocol >= VIAL_PROTOCOL_ADVANCED_MACROS)
                    out += char(SS_QMK_PREFIX);
                quint8 base = a.kind == Kind::Tap ? SS_TAP_CODE : a.kind == Kind::Down ? SS_DOWN_CODE : SS_UP_CODE;
                if (kc < 256) {
                    out += char(base);
                    out += char(kc);
                } else {
                    out += char(base + 4); // VIAL_MACRO_EXT_*
                    // see decode_keycode() in qmk
                    if (kc % 256 == 0)
                        kc = 0xFF00 | (kc >> 8);
                    out += char(kc & 0xFF);
                    out += char((kc >> 8) & 0xFF);
                }
            }
        }
    }
    return out;
}

Macro deserialize(const QByteArray &data, qint64 vialProtocol)
{
    Macro out;
    const int n = data.size();
    auto at = [&data](int i) { return quint8(data[i]); };
    if (vialProtocol < VIAL_PROTOCOL_ADVANCED_MACROS) {
        for (int i = 0; i < n;) {
            if (at(i) == SS_TAP_CODE || at(i) == SS_DOWN_CODE || at(i) == SS_UP_CODE) {
                if (n - i < 2)
                    break;
                appendKey(out, kindOf(at(i)), at(i + 1));
                i += 2;
            } else {
                appendChar(out, at(i));
                ++i;
            }
        }
        return out;
    }
    for (int i = 0; i < n;) {
        if (at(i) != SS_QMK_PREFIX) {
            appendChar(out, at(i));
            ++i;
            continue;
        }
        if (n - i < 2)
            break;
        quint8 act = at(i + 1);
        if (act == SS_TAP_CODE || act == SS_DOWN_CODE || act == SS_UP_CODE) {
            if (n - i < 3)
                break;
            appendKey(out, kindOf(act), at(i + 2));
            i += 3;
        } else if (act == VIAL_MACRO_EXT_TAP || act == VIAL_MACRO_EXT_DOWN || act == VIAL_MACRO_EXT_UP) {
            if (n - i < 4)
                break;
            int kc = at(i + 2) | at(i + 3) << 8;
            if (kc > 0xFF00)
                kc = (kc & 0xFF) << 8;
            appendKey(out, kindOf(act - 4), kc);
            i += 4;
        } else if (act == SS_DELAY_CODE) {
            if (n - i < 4)
                break;
            out.append(Action{Kind::Delay, {}, {}, (at(i + 2) - 1) + (at(i + 3) - 1) * 255});
            i += 4;
        } else {
            // clearly malformed: skip it and hope for the best
            i += 2;
        }
    }
    return out;
}

QList<Macro> deserializeAll(const QByteArray &buffer, int count, qint64 vialProtocol)
{
    QList<QByteArray> parts = buffer.split('\0');
    QList<Macro> out;
    for (int i = 0; i < count; ++i)
        out.append(i < parts.size() ? deserialize(parts[i], vialProtocol) : Macro{});
    return out;
}

QByteArray serializeAll(const QList<Macro> &macros, int count, qint64 vialProtocol)
{
    if (macros.size() != count)
        throw std::runtime_error(QStringLiteral("expected array with %1 macros, got %2 macros")
                                     .arg(count).arg(macros.size()).toStdString());
    QByteArray out;
    for (const Macro &m : macros)
        out += serialize(m, vialProtocol) + '\0';
    return out;
}

QJsonArray save(const Macro &m)
{
    QJsonArray out;
    for (const Action &a : m) {
        QJsonArray act{a.tag()};
        if (a.kind == Kind::Text)
            act.append(a.text);
        else if (a.kind == Kind::Delay)
            act.append(a.delay);
        else
            for (const QString &k : a.keys)
                act.append(k);
        out.append(act);
    }
    return out;
}

Macro restore(const QJsonArray &actions)
{
    Macro out;
    for (const auto &v : actions) {
        QJsonArray act = v.toArray();
        QString tag = act.first().toString();
        Action a;
        if (tag == QLatin1String("text")) {
            a.kind = Kind::Text;
            a.text = act.at(1).toString();
        } else if (tag == QLatin1String("delay")) {
            a.kind = Kind::Delay;
            a.delay = act.at(1).toInt();
        } else if (tag == QLatin1String("tap") || tag == QLatin1String("down") || tag == QLatin1String("up")) {
            a.kind = tag == QLatin1String("tap") ? Kind::Tap : tag == QLatin1String("down") ? Kind::Down : Kind::Up;
            for (int i = 1; i < act.size(); ++i)
                a.keys.append(act[i].isString() ? act[i].toString()
                                                : keycodes::Registry::instance().serialize(act[i].toInt()));
        } else {
            continue; // unknown tags are dropped, as the Python app does
        }
        out.append(a);
    }
    return out;
}

}
