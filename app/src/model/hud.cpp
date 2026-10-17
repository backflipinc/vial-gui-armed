#include "model/hud.h"

#include "core/bytes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hud {
namespace {

qint16 clamp16(int v)
{
    return qint16(std::clamp(v, -32768, 32767));
}

QByteArray ascii(const QString &s)
{
    QByteArray out;
    for (char32_t c : s.toUcs4())
        out.append(c < 128 ? char(c) : '?');
    return out;
}

const char *const kFields[] = {"type", "source", "font", "fg", "bg", "flags", "x", "y", "w", "h", "lo", "hi", "warn",
                               "param"};

int *field(Widget &w, int i)
{
    int *f[] = {&w.type, &w.source, &w.font, &w.fg, &w.bg, &w.flags, &w.x, &w.y, &w.w, &w.h, &w.lo, &w.hi, &w.warn,
                &w.param};
    return f[i];
}

}

QString typeName(int type)
{
    static const QHash<int, QString> n = {{W_TEXT, "TEXT"}, {W_BAR, "BAR"}, {W_GRAPH, "GRAPH"},
                                          {W_INDICATOR, "INDICATOR"}, {W_BATTERY, "BATTERY"}, {W_FRAME, "FRAME"},
                                          {W_TEXTBOX, "TEXT BOX"}};
    return n.value(type);
}

QString frameName(int frame)
{
    static const QStringList n = {"BRACKETS", "BOX", "H LINE", "V LINE", "PANEL"};
    return n.value(frame);
}

QString roleName(int role)
{
    static const QHash<int, QString> n = {{C_BG, "BG"}, {C_PANEL, "PANEL"}, {C_LINE, "LINE"}, {C_DIM, "DIM"},
                                          {C_TEXT, "TEXT"}, {C_ACCENT, "ACCENT"}, {C_OK, "OK"}, {C_WARN, "WARN"},
                                          {C_AUTO, "AUTO"}};
    return n.value(role);
}

QString alignName(int align)
{
    static const QStringList n = {"LEFT", "CENTER", "RIGHT"};
    return n.value(align);
}

Widget Widget::make(int type, int x, int y)
{
    Widget w;
    w.type = type;
    w.x = x;
    w.y = y;
    switch (type) {
    case W_TEXT: w.font = 0; w.fg = C_TEXT; w.w = 80; w.h = 0; w.label = QStringLiteral("{}"); break;
    case W_BAR: w.fg = C_ACCENT; w.w = 100; w.h = 12; break;
    case W_GRAPH: w.fg = C_ACCENT; w.w = 120; w.h = 40; w.param = 10; break;
    case W_INDICATOR: w.fg = C_ACCENT; w.w = 99; w.h = 20; break;
    case W_BATTERY: w.source = S_BATTERY; w.fg = C_ACCENT; w.w = 25; w.h = 11; break;
    case W_FRAME: w.fg = C_LINE; w.w = 100; w.h = 40; w.param = FRAME_BRACKETS; break;
    case W_TEXTBOX: w.fg = C_TEXT; w.w = 200; w.h = 60; w.flags = F_TAIL; break;
    }
    return w;
}

std::variant<QByteArray, QString> Layout::pack(const Sizes &s) const
{
    if (widgets.size() > s.maxWidgets)
        return QStringLiteral("too many widgets: %1 of %2").arg(widgets.size()).arg(s.maxWidgets);
    QByteArray strings, body;
    QHash<QByteArray, int> offsets;
    for (const Widget &w : widgets) {
        quint16 label = NO_LABEL;
        if (w.label) {
            QByteArray raw = ascii(*w.label);
            if (!offsets.contains(raw)) {
                offsets.insert(raw, int(strings.size()));
                strings += raw + '\0';
            }
            label = quint16(offsets.value(raw));
        }
        body += bytes::of({w.type, w.source, w.font, w.fg, w.bg, w.flags});
        for (int v : {w.x, w.y, w.w, w.h, w.lo, w.hi, w.warn})
            bytes::putLe16(body, quint16(clamp16(v)));
        body += bytes::of({w.param & 0xFF, 0});
        bytes::putLe16(body, label);
    }
    if (strings.size() > s.strings)
        return QStringLiteral("labels too long: %1 of %2 bytes").arg(strings.size()).arg(s.strings);
    body.append(WIDGET_SIZE * (s.maxWidgets - widgets.size()), '\0');
    strings.append(s.strings - strings.size(), '\0');
    return bytes::of({MAGIC, FORMAT, int(widgets.size()), 0}) + body + strings;
}

std::optional<Layout> Layout::unpack(const QByteArray &blob, const Sizes &s)
{
    if (blob.size() < s.layoutSize() || quint8(blob[0]) != MAGIC || quint8(blob[1]) != FORMAT
        || quint8(blob[2]) > s.maxWidgets)
        return std::nullopt;
    const QByteArray strings = blob.mid(HEADER + WIDGET_SIZE * s.maxWidgets, s.strings);
    Layout out;
    for (int i = 0; i < quint8(blob[2]); ++i) {
        int o = HEADER + i * WIDGET_SIZE;
        Widget w;
        w.type = bytes::u8(blob, o);
        w.source = bytes::u8(blob, o + 1);
        w.font = bytes::u8(blob, o + 2);
        w.fg = bytes::u8(blob, o + 3);
        w.bg = bytes::u8(blob, o + 4);
        w.flags = bytes::u8(blob, o + 5);
        int *v[] = {&w.x, &w.y, &w.w, &w.h, &w.lo, &w.hi, &w.warn};
        for (int k = 0; k < 7; ++k)
            *v[k] = qint16(bytes::le16(blob, o + 6 + k * 2));
        w.param = bytes::u8(blob, o + 20);
        quint16 label = bytes::le16(blob, o + 22);
        if (label < strings.size()) {
            QByteArray raw = strings.mid(label);
            raw.truncate(raw.indexOf('\0') < 0 ? raw.size() : raw.indexOf('\0'));
            QString text;
            for (char c : raw)
                text += quint8(c) < 128 ? QChar(c) : QChar(0xFFFD);
            w.label = text;
        }
        out.widgets.append(w);
    }
    return out;
}

QByteArray Layout::toJson() const
{
    QJsonArray list;
    for (Widget w : widgets) {
        QJsonObject o;
        for (int i = 0; i < 14; ++i)
            o.insert(QLatin1String(kFields[i]), *field(w, i));
        o.insert("label", w.label ? QJsonValue(*w.label) : QJsonValue());
        list.append(o);
    }
    return QJsonDocument(QJsonObject{{"vial-armed-hud", FORMAT}, {"widgets", list}}).toJson(QJsonDocument::Indented);
}

std::variant<Layout, QString> Layout::fromJson(const QByteArray &json)
{
    QJsonObject d = QJsonDocument::fromJson(json).object();
    if (d.value("vial-armed-hud").toInt(-1) != FORMAT)
        return QStringLiteral("not a Vial Armed HUD layout");
    Layout out;
    for (const auto &v : d.value("widgets").toArray()) {
        QJsonObject o = v.toObject();
        Widget w;
        for (int i = 0; i < 14; ++i)
            if (o.contains(QLatin1String(kFields[i])))
                *field(w, i) = o.value(QLatin1String(kFields[i])).toInt();
        if (o.value("label").isString())
            w.label = o.value("label").toString();
        out.widgets.append(w);
    }
    return out;
}

}
