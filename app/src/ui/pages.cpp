#include "ui/pages.h"

#include "pages/entrypages.h"
#include "pages/keymappage.h"
#include "pages/layoutpage.h"
#include "pages/macropage.h"
#include "pages/vialpages.h"
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
        {"Keymap", hasKeyboard, {{"Keymap", [](Controller *c) { return new KeymapPage(c); }}}},
        {"Layout", [](const Controller &c) {
             return c.keyboard() && c.keyboard()->def.layoutLabels.isArray()
                 && !c.keyboard()->def.layoutLabels.toArray().isEmpty();
         }, {{"Layout", [](Controller *c) { return new LayoutPage(c); }}}},
        {"Macros", [](const Controller &c) { return c.keyboard() && c.keyboard()->macroInfo.count > 0; },
         {{"Macros", [](Controller *c) { return new MacroPage(c); }}}},
        {"Lighting", [](const Controller &c) { return armedHas(c, F_RGB_MATRIX); },
         {pending("Effects"), pending("Theme editor")}},
        {"Lighting", [](const Controller &c) {
             const KeyboardState *kb = c.keyboard();
             return kb && ((kb->lighting == "vialrgb" && !armedHas(c, F_RGB_MATRIX)) || kb->lighting.startsWith("qmk_"));
         }, {{"Lighting", [](Controller *c) { return new RgbPage(c); }}}},
        {"Screen", [](const Controller &c) { return armedHas(c, F_SCREEN) || armedHas(c, F_HUD) || armedHas(c, F_DISPLAY); },
         {pending("Settings"), pending("HUD editor"), pending("Animation")}},
        {"Tap Dance", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.tapDance > 0; },
         {{"Tap Dance", [](Controller *c) { return new TapDancePage(c); }}}},
        {"Combos", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.combo > 0; },
         {{"Combos", [](Controller *c) { return new ComboPage(c); }}}},
        {"Key Overrides", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.keyOverride > 0; },
         {{"Key Overrides", [](Controller *c) { return new KeyOverridePage(c); }}}},
        {"Alt Repeat Key", [](const Controller &c) { return c.keyboard() && c.keyboard()->dynamicCounts.altRepeatKey > 0; },
         {{"Alt Repeat Key", [](Controller *c) { return new AltRepeatKeyPage(c); }}}},
        {"QMK Settings", [](const Controller &c) { return c.keyboard() && !c.keyboard()->settings.isEmpty(); },
         {{"QMK Settings", [](Controller *c) { return new QmkSettingsPage(c); }}}},
        {"Matrix Tester", [](const Controller &c) {
             const KeyboardState *kb = c.keyboard();
             return kb && kb->vialProtocol >= 3 && (kb->def.cols / 8 + 1) * kb->def.rows <= 28;
         }, {{"Matrix Tester", [](Controller *c) { return new MatrixPage(c); }}}},
        {"Firmware", [](const Controller &c) {
             auto cur = c.current();
             return (cur && cur->kind == hid::DeviceKind::Bootloader) || (c.keyboard() && c.keyboard()->def.vibl);
         }, {{"Firmware", [](Controller *c) { return new FirmwarePage(c); }}}},
        {"Device", [](const Controller &c) { return c.keyboard() && c.keyboard()->armed.has_value(); },
         {pending("Device")}},
    };
}
