#include "ui/keycodedisplay.h"

#include "keycodes/keycodes.h"
#include "keycodes/tables.h"
#include "ui/theme.h"

KeycodeDisplay &KeycodeDisplay::instance()
{
    static KeycodeDisplay d;
    return d;
}

void KeycodeDisplay::setKeymap(const QString &title)
{
    override_.clear();
    title_ = QStringLiteral("QWERTY");
    for (const auto &km : keycodes::tables::keymaps()) {
        if (QString::fromUtf8(km.title) != title)
            continue;
        title_ = title;
        for (int i = 0; i < km.count; ++i)
            override_.insert(QString::fromUtf8(km.labels[i].qmkId), QString::fromUtf8(km.labels[i].label));
    }
    emit changed();
}

bool KeycodeDisplay::overridden(const QString &code) const
{
    const auto *k = keycodes::Registry::instance().findOuter(code);
    return k && override_.contains(k->qmkId);
}

QString KeycodeDisplay::label(const QString &code) const
{
    const auto &reg = keycodes::Registry::instance();
    if (overridden(code))
        return override_.value(reg.findOuter(code)->qmkId);
    return reg.label(code);
}

KeycodeDisplay::Legend KeycodeDisplay::legend(const QString &code) const
{
    const auto &reg = keycodes::Registry::instance();
    Legend l;
    l.text = label(code);
    l.tooltip = reg.tooltip(code);
    l.masked = reg.isMask(code);
    const auto *inner = reg.findInner(code);
    if (inner)
        l.maskText = label(inner->qmkId);
    if (l.masked)
        l.text = l.text.section(QLatin1Char('\n'), 0, 0);
    if (overridden(code))
        l.color = ui::theme::ACC;
    if (inner && l.masked && overridden(inner->qmkId))
        l.maskColor = ui::theme::ACC;
    return l;
}
