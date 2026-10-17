#include "ui/pages.h"

#include "protocol/armed.h"

#include <QLabel>
#include <QVBoxLayout>

PendingPage::PendingPage(Controller *ctl, const QString &title)
    : Page(ctl)
{
    auto *l = new QVBoxLayout(this);
    auto *t = new QLabel(tr("%1 is coming in Vial Armed 2.0.\nUntil then, use Vial Armed 1.x for it.").arg(title));
    t->setObjectName("armedHint");
    t->setAlignment(Qt::AlignCenter);
    l->addWidget(t, 1);
}

namespace {

PageSpec pending(const QString &title)
{
    return {title, [title](Controller *c) { return new PendingPage(c, title); }};
}

bool hasKeyboard(const Controller &c)
{
    return c.keyboard() != nullptr;
}

bool armedHas(const Controller &c, quint16 f)
{
    return c.keyboard() && c.keyboard()->armed && c.keyboard()->armed->has(f);
}

}

QList<NavEntry> navEntries()
{
    using namespace armed;
    return {
        {"Keymap", hasKeyboard, {pending("Keymap")}},
        {"Layout", [](const Controller &c) {
             return c.keyboard() && c.keyboard()->def.layoutLabels.isArray()
                 && !c.keyboard()->def.layoutLabels.toArray().isEmpty();
         }, {pending("Layout")}},
        {"Macros", [](const Controller &c) { return c.keyboard() && c.keyboard()->macroInfo.count > 0; },
         {pending("Macros")}},
        {"Lighting", [](const Controller &c) { return armedHas(c, F_RGB_MATRIX); },
         {pending("Effects"), pending("Theme editor")}},
        {"Lighting", [](const Controller &c) {
             const KeyboardState *kb = c.keyboard();
             return kb && !armedHas(c, F_RGB_MATRIX)
                 && (kb->lighting == "vialrgb" || kb->lighting.startsWith("qmk_"));
         }, {pending("Lighting")}},
        {"Screen", [](const Controller &c) { return armedHas(c, F_SCREEN) || armedHas(c, F_HUD) || armedHas(c, F_DISPLAY); },
         {pending("Settings"), pending("HUD editor"), pending("Animation")}},
        {"Tap Dance", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.tapDance > 0; },
         {pending("Tap Dance")}},
        {"Combos", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.combo > 0; },
         {pending("Combos")}},
        {"Key Overrides", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.keyOverride > 0; },
         {pending("Key Overrides")}},
        {"Alt Repeat Key", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.altRepeatKey > 0; },
         {pending("Alt Repeat Key")}},
        {"QMK Settings", [](const Controller &c) { return c.keyboard() && !c.keyboard()->settings.isEmpty(); },
         {pending("QMK Settings")}},
        {"Matrix Tester", [](const Controller &c) {
             return c.keyboard() && (c.keyboard()->vialProtocol >= 3 || c.keyboard()->vialProtocol < 0);
         }, {pending("Matrix Tester")}},
        {"Firmware", [](const Controller &c) {
             auto cur = c.current();
             return (cur && cur->kind == hid::DeviceKind::Bootloader) || (c.keyboard() && c.keyboard()->def.vibl);
         }, {pending("Firmware")}},
        {"Device", [](const Controller &c) { return c.keyboard() && c.keyboard()->armed.has_value(); },
         {pending("Device")}},
    };
}
