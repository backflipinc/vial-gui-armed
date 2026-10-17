#include "keycodes/keycodes.h"

#include "keycodes/anykeycode.h"

#include <QJsonObject>

namespace keycodes {
namespace {

QStringList splitBar(const char *s)
{
    if (!s || !*s)
        return {};
    return QString::fromUtf8(s).split(QLatin1Char('|'));
}

QString str(const char *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

}

Registry &Registry::instance()
{
    static Registry r;
    return r;
}

Registry::Registry()
{
    for (const auto &g : tables::groups()) {
        QList<const Keycode *> list;
        for (int i = 0; i < g.count; ++i) {
            // the tab subsets reuse the keycodes of their full groups
            const Keycode *existing = byQmkId_.value(QString::fromUtf8(g.defs[i].qmkId));
            bool subset = g.group == Group::BasicNumpad || g.group == Group::BasicNav || g.group == Group::IsoKr;
            list.append(subset && existing ? existing : make(g.defs[i]));
        }
        groups_[g.group] = list;
    }
    for (int x = 0; x < 256; ++x) {
        QString id = QStringLiteral("TD(%1)").arg(x);
        hidden_.append(make(Keycode{id, id, {}, false, {}, {id}, {}, false}));
    }
    rebuild({});
}

const Keycode *Registry::make(const tables::KeycodeDef &d)
{
    Keycode k;
    k.qmkId = str(d.qmkId);
    k.label = str(d.label);
    k.tooltip = str(d.tooltip);
    k.masked = d.masked;
    k.printable = str(d.printable);
    k.alias = QStringList{k.qmkId} + splitBar(d.aliases);
    k.requiresFeature = str(d.requiresFeature);
    const Keycode *p = make(k);
    for (const QString &a : splitBar(d.recorderAliases))
        recorderAlias_.insert(a, p);
    return p;
}

const Keycode *Registry::make(const Keycode &k)
{
    store_.push_back(std::make_unique<Keycode>(k));
    const Keycode *p = store_.back().get();
    byQmkId_.insert(p->qmkId, p);
    if (p->masked)
        maskedPrefixes_.insert(QString(p->qmkId).replace(QLatin1String("(kc)"), QString()));
    return p;
}

void Registry::rebuild(const KeyboardTraits &kb)
{
    protocol_ = kb.protocol;
    kc_.clear();
    maskedCodes_.clear();
    for (const auto &rc : tables::rawCodes(protocol_ == 6 ? 6 : 5)) {
        QString name = QString::fromUtf8(rc.name);
        kc_.insert(name, rc.code);
        if (name.endsWith(QLatin1String("(kc)")))
            maskedCodes_.insert(rc.code);
    }

    auto forMask = [&](const QString &prefix, const QString &description, const QString &feature = {}) {
        for (int layer = 0; layer < kb.layers; ++layer) {
            QString lbl = QStringLiteral("%1(%2)").arg(prefix).arg(layer);
            layers_.append(make(Keycode{lbl, lbl, description, false, {}, {lbl}, feature, false}));
        }
    };
    layers_.clear();
    layers_.append(make(Keycode{"QK_LAYER_LOCK", "Layer\nLock", "Locks the current layer", false, {},
                                {"QK_LAYER_LOCK", "QK_LLCK"}, "layer_lock", false}));
    if (kb.layers >= 4) {
        layers_.append(make(Keycode{"FN_MO13", "Fn1\n(Fn3)", {}, false, {}, {"FN_MO13"}, {}, false}));
        layers_.append(make(Keycode{"FN_MO23", "Fn2\n(Fn3)", {}, false, {}, {"FN_MO23"}, {}, false}));
    }
    forMask("MO", "Momentarily turn on layer when pressed (requires KC_TRNS on destination layer)");
    forMask("DF", "Set the base (default) layer");
    forMask("PDF", "Persistently set the base (default) layer", "persistent_default_layer");
    forMask("TG", "Toggle layer on or off");
    forMask("TT", "Normally acts like MO unless it's tapped multiple times, which toggles layer on");
    forMask("OSL", "Momentarily activates layer until a key is pressed");
    forMask("TO", "Turns on layer and turns off all other layers, except the default layer");
    for (int x = 0; x < std::min(kb.layers, 16); ++x) {
        QString id = QStringLiteral("LT%1(kc)").arg(x);
        layers_.append(make(Keycode{id, QStringLiteral("LT %1\n(kc)").arg(x),
                                    QStringLiteral("kc on tap, switch to layer %1 while held").arg(x), true, {}, {id}, {},
                                    false}));
    }

    macros_.clear();
    for (int x = 0; x < kb.macroCount; ++x) {
        QString id = QStringLiteral("M%1").arg(x);
        macros_.append(make(Keycode{id, id, {}, false, {}, {id}, {}, false}));
    }
    macros_ += groups_.value(Group::MacroBase);

    tapDances_.clear();
    for (int x = 0; x < kb.tapDanceCount; ++x) {
        QString id = QStringLiteral("TD(%1)").arg(x);
        tapDances_.append(make(Keycode{id, id, "Tap dance keycode", false, {}, {id}, {}, false}));
    }

    user_.clear();
    if (!kb.customKeycodes.isEmpty()) {
        for (int x = 0; x < kb.customKeycodes.size(); ++x) {
            QJsonObject c = kb.customKeycodes[x].toObject();
            QString def = QStringLiteral("USER%1").arg(x, 2, 10, QLatin1Char('0'));
            user_.append(make(Keycode{def, c.value("shortName").toString(def), c.value("title").toString(def), false,
                                      {}, {def, c.value("name").toString(def)}, {}, false}));
        }
    } else {
        for (int x = 0; x < 16; ++x) {
            QString id = QStringLiteral("USER%1").arg(x, 2, 10, QLatin1Char('0'));
            user_.append(make(Keycode{id, QStringLiteral("User %1").arg(x), QStringLiteral("User keycode %1").arg(x),
                                      false, {}, {id}, {}, false}));
        }
    }

    midi_.clear();
    if (kb.midi == QLatin1String("basic") || kb.midi == QLatin1String("advanced"))
        midi_ += groups_.value(Group::MidiBasic);
    if (kb.midi == QLatin1String("advanced"))
        midi_ += groups_.value(Group::MidiAdvanced);

    recreate();
    for (const Keycode *k : all_)
        const_cast<Keycode *>(k)->hidden = !k->requiresFeature.isEmpty() && !kb.features.contains(k->requiresFeature);
}

void Registry::recreate()
{
    all_.clear();
    for (Group g : {Group::Special, Group::Basic, Group::Shifted, Group::Iso})
        all_ += groups_.value(g);
    all_ += layers_;
    for (Group g : {Group::Boot, Group::Modifiers, Group::Quantum, Group::Backlight, Group::Media})
        all_ += groups_.value(g);
    all_ += tapDances_;
    all_ += macros_;
    all_ += user_;
    all_ += hidden_;
    all_ += midi_;

    map_.clear();
    raw_.clear();
    for (const Keycode *k : all_) {
        map_.insert(QString(k->qmkId).replace(QLatin1String("(kc)"), QString()), k);
        if (auto code = resolve(k->qmkId))
            raw_.insert(*code, k);
    }

    anyNames_.clear();
    QList<const Keycode *> named;
    for (Group g : {Group::Special, Group::Basic, Group::Shifted, Group::Iso, Group::Backlight, Group::Media})
        named += groups_.value(g);
    named += user_;
    for (const Keycode *k : named) {
        if (auto code = resolve(k->qmkId))
            for (const QString &a : k->alias)
                anyNames_.insert(a, *code);
    }
    for (const char *m : {"MOD_LCTL", "MOD_LSFT", "MOD_LALT", "MOD_LGUI", "MOD_RCTL", "MOD_RSFT", "MOD_RALT",
                          "MOD_RGUI", "MOD_MEH", "MOD_HYPR"})
        if (auto code = resolve(QLatin1String(m)))
            anyNames_.insert(QLatin1String(m), *code);
}

std::optional<int> Registry::resolve(const QString &constant) const
{
    auto it = kc_.find(constant);
    if (it == kc_.end())
        return std::nullopt;
    return it.value();
}

const Keycode *Registry::find(const QString &qmkId) const
{
    return map_.value(qmkId == QLatin1String("kc") ? QStringLiteral("KC_NO") : qmkId);
}

bool Registry::isMask(const QString &qmkId) const
{
    int p = qmkId.indexOf(QLatin1Char('('));
    return p >= 0 && maskedPrefixes_.contains(qmkId.left(p));
}

const Keycode *Registry::findOuter(const QString &qmkId) const
{
    if (isMask(qmkId))
        return find(qmkId.left(qmkId.indexOf(QLatin1Char('('))));
    return find(qmkId);
}

const Keycode *Registry::findInner(const QString &qmkId) const
{
    if (isMask(qmkId)) {
        int p = qmkId.indexOf(QLatin1Char('('));
        return find(qmkId.mid(p + 1, qmkId.size() - p - 2));
    }
    return find(qmkId);
}

const Keycode *Registry::findByRecorderAlias(const QString &alias) const
{
    return recorderAlias_.value(alias);
}

const Keycode *Registry::findByQmkId(const QString &qmkId) const
{
    return byQmkId_.value(qmkId);
}

bool Registry::isBasic(const QString &qmkId) const
{
    return deserialize(qmkId) < 0x00FF;
}

QString Registry::label(const QString &qmkId) const
{
    const Keycode *k = findOuter(qmkId);
    return k ? k->label : qmkId;
}

QString Registry::tooltip(const QString &qmkId) const
{
    const Keycode *k = findOuter(qmkId);
    if (!k)
        return {};
    return k->tooltip.isEmpty() ? k->qmkId : QStringLiteral("%1: %2").arg(k->qmkId, k->tooltip);
}

QString Registry::serialize(int code) const
{
    if (!maskedCodes_.contains(code & 0xFF00)) {
        if (const Keycode *k = raw_.value(code))
            return k->qmkId;
    } else {
        const Keycode *outer = raw_.value(code & 0xFF00);
        const Keycode *inner = raw_.value(code & 0x00FF);
        if (outer && inner)
            return QString(outer->qmkId).replace(QLatin1String("kc"), inner->qmkId);
    }
    return QStringLiteral("0x%1").arg(code, 0, 16);
}

std::optional<int> Registry::deserializeStrict(const QString &value) const
{
    if (const Keycode *k = byQmkId_.value(value))
        return resolve(k->qmkId);
    auto v = evaluate(value, anyNames_, [this](const QString &c) { return resolve(c); });
    if (!v)
        return std::nullopt;
    return int(*v);
}

int Registry::deserialize(const QString &value) const
{
    return deserializeStrict(value).value_or(0);
}

QString Registry::normalize(const QString &qmkId) const
{
    return serialize(deserialize(qmkId));
}

QList<const Keycode *> Registry::group(Group g) const
{
    return groups_.value(g);
}

}
