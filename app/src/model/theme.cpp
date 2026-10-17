#include "model/theme.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <optional>

namespace theme {
namespace {

struct Defaults {
    std::optional<Rgb> a, b;
    std::optional<int> speed, size, param, flags;
};

const QHash<int, Defaults> &typeDefaults()
{
    static const QHash<int, Defaults> d = {
        {SOLID, {Rgb{255, 170, 0}, {}, {}, {}, {}, {}}},
        {GRADIENT, {Rgb{255, 170, 0}, Rgb{255, 0, 60}, {}, {}, {}, F_MIRROR}},
        {RAINBOW, {{}, {}, 24, {}, 255, {}}},
        {PULSE, {Rgb{255, 170, 0}, Rgb{0, 0, 0}, 40, 0, {}, {}}},
        {WAVE, {Rgb{255, 255, 255}, {}, 60, 60, {}, {}}},
        {SPARKLE, {Rgb{255, 255, 255}, Rgb{255, 170, 0}, 60, 128, {}, {}}},
        {REACTIVE, {Rgb{255, 170, 0}, {}, {}, 64, 0, {}}},
        {RIPPLE, {Rgb{0, 200, 255}, {}, 100, 64, {}, {}}},
        {PAINT, {}},
    };
    return d;
}

QByteArray ascii(const QString &s)
{
    QByteArray out;
    for (char32_t c : s.toUcs4())
        out.append(c < 128 ? char(c) : '?');
    return out;
}

}

const QList<LayerType> &types()
{
    static const QList<LayerType> t = {
        {SOLID, "SOLID", "One colour", {"a"}},
        {GRADIENT, "GRADIENT", "Colour A fades into colour B across the keys", {"a", "b", "dir", "speed", "size", "mirror"}},
        {RAINBOW, "RAINBOW", "Every hue, spread across the keys", {"dir", "speed", "size", "param"}},
        {PULSE, "PULSE", "Breathes between colour A and colour B", {"a", "b", "dir", "speed", "size"}},
        {WAVE, "WAVE", "A band of colour sweeps over the layers below", {"a", "dir", "speed", "size", "mirror"}},
        {SPARKLE, "SPARKLE", "Random keys flash, in colours between A and B", {"a", "b", "speed", "size"}},
        {REACTIVE, "REACTIVE", "Keys light up when pressed and fade out", {"a", "size", "param"}},
        {RIPPLE, "RIPPLE", "Rings spread from every key press", {"a", "speed", "size"}},
        {PAINT, "PAINT", "Your own colour on every key: pick a colour and click keys", {"paint"}},
    };
    return t;
}

const LayerType *typeById(int id)
{
    for (const auto &t : types())
        if (t.id == id)
            return &t;
    return nullptr;
}

Layer Layer::make(int type, const QSet<int> &leds)
{
    Layer l;
    l.type = type;
    l.mask = leds;
    const Defaults d = typeDefaults().value(type);
    if (d.a)
        l.a = *d.a;
    if (d.b)
        l.b = *d.b;
    if (d.speed)
        l.speed = *d.speed;
    if (d.size)
        l.size = *d.size;
    if (d.param)
        l.param = *d.param;
    if (d.flags)
        l.flags = *d.flags;
    return l;
}

QByteArray Layer::pack(int nLeds) const
{
    QByteArray out;
    for (int v : {type, blend, opacity, speed, dir, size, flags, cond})
        out += char(v);
    for (quint8 c : a)
        out += char(c);
    for (quint8 c : b)
        out += char(c);
    out += char(param);
    out += char(cx);
    out += char(cy);
    out.append(3, '\0');
    QByteArray m((nLeds + 7) / 8, '\0');
    for (int i : mask)
        if (i >= 0 && i < nLeds)
            m[i / 8] = char(quint8(m[i / 8]) | (1 << (i % 8)));
    return out + m;
}

Layer Layer::unpack(const QByteArray &d, int nLeds)
{
    auto u = [&d](int i) { return i < d.size() ? int(quint8(d[i])) : 0; };
    Layer l;
    l.type = u(0);
    l.blend = u(1);
    l.opacity = u(2);
    l.speed = u(3);
    l.dir = u(4);
    l.size = u(5);
    l.flags = u(6);
    l.cond = u(7);
    l.a = {quint8(u(8)), quint8(u(9)), quint8(u(10))};
    l.b = {quint8(u(11)), quint8(u(12)), quint8(u(13))};
    l.param = u(14);
    l.cx = u(15);
    l.cy = u(16);
    for (int i = 0; i < nLeds; ++i)
        if (u(LAYER_HEAD + i / 8) & (1 << (i % 8)))
            l.mask.insert(i);
    return l;
}

Theme Theme::blank(const QString &name, int nLeds)
{
    Theme t;
    t.name = name;
    t.paint = QList<Rgb>(nLeds, Rgb{0, 0, 0});
    return t;
}

QByteArray Theme::pack(int nLeds, int maxLayers) const
{
    QByteArray n = ascii(name).left(NAME_LEN - 1);
    n.append(NAME_LEN - n.size(), '\0');
    const int layerSize = LAYER_HEAD + (nLeds + 7) / 8;
    QByteArray out;
    out += char(MAGIC);
    out += char(FORMAT);
    out += char(std::min(int(layers.size()), maxLayers));
    out += '\0';
    out += n;
    for (int i = 0; i < maxLayers; ++i)
        out += i < layers.size() ? layers[i].pack(nLeds) : QByteArray(layerSize, '\0');
    for (int i = 0; i < nLeds; ++i) {
        Rgb c = i < paint.size() ? paint[i] : Rgb{0, 0, 0};
        out += char(c[0]);
        out += char(c[1]);
        out += char(c[2]);
    }
    return out;
}

Theme Theme::unpack(const QByteArray &d, int nLeds, int maxLayers)
{
    Theme t = blank("THEME", nLeds);
    if (d.size() < HEADER || quint8(d[0]) != MAGIC || quint8(d[1]) != FORMAT)
        return t;
    QByteArray n = d.mid(4, NAME_LEN);
    n.truncate(n.indexOf('\0') < 0 ? n.size() : n.indexOf('\0'));
    t.name.clear();
    for (char c : n)
        t.name += quint8(c) < 128 ? QChar(c) : QChar(0xFFFD);
    const int layerSize = LAYER_HEAD + (nLeds + 7) / 8;
    for (int i = 0; i < std::min(int(quint8(d[2])), maxLayers); ++i)
        t.layers.append(Layer::unpack(d.mid(HEADER + i * layerSize, layerSize), nLeds));
    const int off = HEADER + maxLayers * layerSize;
    for (int i = 0; i < nLeds; ++i) {
        auto u = [&d](int k) { return k < d.size() ? quint8(d[k]) : quint8(0); };
        t.paint[i] = {u(off + i * 3), u(off + i * 3 + 1), u(off + i * 3 + 2)};
    }
    return t;
}

QString hexColor(const Rgb &c)
{
    return QStringLiteral("#%1%2%3")
        .arg(c[0], 2, 16, QLatin1Char('0'))
        .arg(c[1], 2, 16, QLatin1Char('0'))
        .arg(c[2], 2, 16, QLatin1Char('0'));
}

Rgb parseColor(const QString &str, const Rgb &fallback)
{
    QString s = str;
    while (s.startsWith(QLatin1Char('#')))
        s.remove(0, 1);
    if (s.size() < 6)
        return fallback;
    Rgb out;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        out[size_t(i)] = quint8(s.mid(i * 2, 2).toInt(&ok, 16));
        if (!ok)
            return fallback;
    }
    return out;
}

QString keyName(const QList<armed::Led> &leds, int i)
{
    const armed::Led &led = leds[i];
    return led.row != 0xFF ? QStringLiteral("%1,%2").arg(led.row).arg(led.col) : QStringLiteral("led%1").arg(i);
}

QByteArray Theme::toJson(const QList<armed::Led> &leds) const
{
    QJsonArray ls;
    for (const Layer &l : layers) {
        const LayerType *t = typeById(l.type);
        QStringList keys;
        for (int i : l.mask)
            if (i < leds.size())
                keys.append(keyName(leds, i));
        keys.sort();
        ls.append(QJsonObject{
            {"type", t ? QJsonValue(t->name.toLower()) : QJsonValue(l.type)},
            {"blend", l.blend < 3 ? QJsonValue(kBlends[l.blend].toLower()) : QJsonValue(0)},
            {"opacity", l.opacity}, {"speed", l.speed}, {"direction", l.dir}, {"size", l.size},
            {"param", l.param}, {"center", QJsonArray{l.cx, l.cy}}, {"cond", l.cond},
            {"radial", l.flag(F_RADIAL)}, {"mirror", l.flag(F_MIRROR)}, {"reverse", l.flag(F_REVERSE)},
            {"hidden", l.hidden()}, {"a", hexColor(l.a)}, {"b", hexColor(l.b)},
            {"keys", QJsonArray::fromStringList(keys)},
        });
    }
    QJsonObject p;
    for (int i = 0; i < paint.size() && i < leds.size(); ++i)
        if (paint[i] != Rgb{0, 0, 0})
            p.insert(keyName(leds, i), hexColor(paint[i]));
    return QJsonDocument(QJsonObject{{"vial_armed_theme", FORMAT}, {"name", name}, {"layers", ls}, {"paint", p}})
        .toJson(QJsonDocument::Indented);
}

std::variant<Theme, QString> Theme::fromJson(const QByteArray &json, const QList<armed::Led> &leds, int maxLayers)
{
    QJsonObject d = QJsonDocument::fromJson(json).object();
    if (!d.contains("vial_armed_theme"))
        return QStringLiteral("not a Vial Armed theme");
    QHash<QString, int> names;
    for (int i = 0; i < leds.size(); ++i)
        names.insert(keyName(leds, i), i);
    Theme t = blank(d.value("name").toVariant().toString().left(NAME_LEN - 1), int(leds.size()));
    if (!d.contains("name"))
        t.name = "THEME";
    for (const auto &v : d.value("layers").toArray()) {
        if (t.layers.size() >= maxLayers)
            break;
        QJsonObject o = v.toObject();
        int type = SOLID;
        for (const auto &lt : types())
            if (lt.name.toLower() == o.value("type").toString())
                type = lt.id;
        Layer l = Layer::make(type);
        QString blend = o.value("blend").toString("normal").toUpper();
        l.blend = kBlends.contains(blend) ? int(kBlends.indexOf(blend)) : NORMAL;
        const QList<QPair<const char *, int *>> ints = {{"opacity", &l.opacity}, {"speed", &l.speed},
                                                        {"direction", &l.dir}, {"size", &l.size},
                                                        {"param", &l.param}, {"cond", &l.cond}};
        for (const auto &[key, field] : ints)
            if (o.contains(QLatin1String(key)))
                *field = o.value(QLatin1String(key)).toInt() & 0xFF;
        if (o.contains("center")) {
            QJsonArray c = o.value("center").toArray();
            l.cx = c.at(0).toInt() & 0xFF;
            l.cy = c.at(1).toInt() & 0xFF;
        }
        l.flags = 0;
        l.setFlag(F_RADIAL, o.value("radial").toBool());
        l.setFlag(F_MIRROR, o.value("mirror").toBool());
        l.setFlag(F_REVERSE, o.value("reverse").toBool());
        l.setFlag(F_HIDDEN, o.value("hidden").toBool());
        l.a = parseColor(o.value("a").toString(), l.a);
        l.b = parseColor(o.value("b").toString(), l.b);
        l.mask.clear();
        for (const auto &k : o.value("keys").toArray())
            if (names.contains(k.toString()))
                l.mask.insert(names.value(k.toString()));
        t.layers.append(l);
    }
    const QJsonObject paint = d.value("paint").toObject();
    for (auto it = paint.begin(); it != paint.end(); ++it)
        if (names.contains(it.key()))
            t.paint[names.value(it.key())] = parseColor(it.value().toString(), Rgb{0, 0, 0});
    return t;
}

QMap<QString, QSet<int>> keyGroups(const QList<armed::Led> &leds, const KeymapFn &keymap)
{
    static const QSet<QString> letters = [] {
        QSet<QString> s;
        for (char c = 'A'; c <= 'Z'; ++c)
            s.insert(QStringLiteral("KC_") + QChar(c));
        return s;
    }();
    static const QSet<QString> numbers = [] {
        QSet<QString> s{"KC_GRAVE", "KC_GRV", "KC_MINUS", "KC_MINS", "KC_EQUAL", "KC_EQL"};
        for (char c : QByteArray("1234567890"))
            s.insert(QStringLiteral("KC_") + QChar(c));
        return s;
    }();
    static const QSet<QString> fkeys = [] {
        QSet<QString> s{"KC_ESCAPE", "KC_ESC"};
        for (int i = 1; i < 25; ++i)
            s.insert(QStringLiteral("KC_F%1").arg(i));
        return s;
    }();
    static const QSet<QString> arrows{"KC_UP", "KC_DOWN", "KC_LEFT", "KC_RIGHT", "KC_RGHT"};
    static const QSet<QString> wasd{"KC_W", "KC_A", "KC_S", "KC_D"};

    auto byCode = [&](const QSet<QString> &codes) {
        QSet<int> out;
        for (int i = 0; i < leds.size(); ++i)
            if (codes.contains(keymap(0, leds[i].row, leds[i].col)))
                out.insert(i);
        return out;
    };
    QMap<QString, QSet<int>> g;
    QSet<int> all, mods, fn;
    for (int i = 0; i < leds.size(); ++i) {
        all.insert(i);
        if (leds[i].modifier())
            mods.insert(i);
        QString onFn = keymap(1, leds[i].row, leds[i].col);
        if ((!onFn.isNull() && onFn != QLatin1String("KC_TRNS") && onFn != QLatin1String("KC_NO"))
            || keymap(0, leds[i].row, leds[i].col) == QLatin1String("MO(1)"))
            fn.insert(i);
    }
    g["ALL"] = all;
    g["LETTERS"] = byCode(letters);
    g["NUMBERS"] = byCode(numbers);
    g["F-ROW"] = byCode(fkeys);
    g["MODS"] = mods;
    g["ARROWS"] = byCode(arrows);
    g["WASD"] = byCode(wasd);
    g["FN LAYER"] = fn;
    return g;
}

namespace {

struct PresetLayer {
    int type;
    QString keys;
    std::function<void(Layer &)> set;
};

const QList<QPair<QString, QList<PresetLayer>>> &presets()
{
    static const QList<QPair<QString, QList<PresetLayer>>> p = {
        {"AMBER DRIFT", {{GRADIENT, "ALL", [](Layer &l) { l.a = {255, 170, 0}; l.b = {255, 40, 0}; l.speed = 16; l.flags = F_MIRROR; }}}},
        {"DEEP SEA", {{GRADIENT, "ALL", [](Layer &l) { l.a = {0, 200, 255}; l.b = {0, 20, 160}; l.dir = 64; l.speed = 12; l.flags = F_MIRROR; }},
                      {SPARKLE, "ALL", [](Layer &l) { l.a = {255, 255, 255}; l.b = {120, 255, 255}; l.speed = 60; }}}},
        {"SHOCKWAVE", {{SOLID, "ALL", [](Layer &l) { l.a = {20, 0, 60}; }},
                       {RIPPLE, "ALL", [](Layer &l) { l.a = {255, 170, 0}; l.speed = 100; l.size = 96; }}}},
        {"SPECTRUM", {{RAINBOW, "ALL", [](Layer &l) { l.speed = 24; }}}},
        {"GAMER WASD", {{SOLID, "ALL", [](Layer &l) { l.a = {30, 0, 0}; }},
                        {SOLID, "ARROWS", [](Layer &l) { l.a = {255, 60, 0}; }},
                        {PULSE, "WASD", [](Layer &l) { l.a = {255, 0, 0}; l.b = {255, 170, 0}; l.speed = 50; }},
                        {REACTIVE, "ALL", [](Layer &l) { l.a = {255, 255, 255}; l.size = 40; }}}},
        {"TYPEWRITER", {{SOLID, "ALL", [](Layer &l) { l.a = {255, 140, 40}; }},
                        {SOLID, "MODS", [](Layer &l) { l.a = {120, 40, 0}; }},
                        {REACTIVE, "ALL", [](Layer &l) { l.a = {255, 255, 255}; l.size = 80; l.param = 24; }}}},
        {"AURORA", {{GRADIENT, "ALL", [](Layer &l) { l.a = {0, 255, 120}; l.b = {120, 0, 255}; l.dir = 32; l.speed = 10; l.size = 96; l.flags = F_MIRROR; }},
                    {WAVE, "ALL", [](Layer &l) { l.a = {255, 255, 255}; l.dir = 0; l.speed = 20; l.size = 40; l.opacity = 110; l.blend = ADD; }}}},
        {"RADAR", {{SOLID, "ALL", [](Layer &l) { l.a = {0, 20, 0}; }},
                   {WAVE, "ALL", [](Layer &l) { l.a = {0, 255, 60}; l.speed = 40; l.size = 50; l.flags = F_RADIAL; }}}},
        {"CAPS ALERT", {{GRADIENT, "ALL", [](Layer &l) { l.a = {0, 120, 255}; l.b = {150, 0, 255}; l.speed = 14; l.flags = F_MIRROR; }},
                        {PULSE, "ALL", [](Layer &l) { l.a = {255, 0, 0}; l.b = {60, 0, 0}; l.speed = 90; l.cond = C_CAPS; }}}},
        {"FN MAP", {{GRADIENT, "ALL", [](Layer &l) { l.a = {0, 120, 255}; l.b = {0, 255, 160}; l.speed = 12; l.flags = F_MIRROR; }},
                    {SOLID, "ALL", [](Layer &l) { l.a = {0, 0, 0}; l.opacity = 220; l.cond = C_LAYER | 1; }},
                    {PULSE, "FN LAYER", [](Layer &l) { l.a = {255, 170, 0}; l.b = {255, 255, 255}; l.speed = 60; l.cond = C_LAYER | 1; }}}},
        {"BLANK", {}},
    };
    return p;
}

}

QStringList presetNames()
{
    QStringList out;
    for (const auto &p : presets())
        out.append(p.first);
    return out;
}

Theme buildPreset(int index, const QList<armed::Led> &leds, const KeymapFn &keymap)
{
    const auto &[name, layers] = presets()[index];
    auto groups = keyGroups(leds, keymap);
    Theme t = Theme::blank(name, int(leds.size()));
    for (const PresetLayer &pl : layers) {
        Layer l = Layer::make(pl.type, groups.value(pl.keys, groups.value("ALL")));
        pl.set(l);
        t.layers.append(l);
    }
    return t;
}

}
