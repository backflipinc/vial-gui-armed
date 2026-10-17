#include "pages/entrypages.h"

#include "keycodes/keycodes.h"
#include "ui/keycodedisplay.h"
#include "ui/theme.h"
#include "ui/widgets/keybutton.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

const QString kReset = QLatin1String(keycodes::kResetKeycode);

QString legend(const QString &code)
{
    return KeycodeDisplay::instance().label(code).replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified();
}

bool none(const QString &code)
{
    return code == QLatin1String("KC_NO");
}

// a list entry of KeyboardState changes at once; the keyboard follows, or the entry goes back
template <class E>
void writeEntry(Controller *ctl, QList<E> KeyboardState::*list, int i, const E &e, quint8 op, int retries,
                bool unlock, const QString &key, std::function<void()> after)
{
    KeyboardState *kb = ctl->keyboard();
    if (!kb || (kb->*list)[i] == e)
        return;
    const E old = (kb->*list)[i];
    (kb->*list)[i] = e;
    after();
    const QByteArray data = e.pack();
    ctl->write(unlock, [=](hid::Link &link) { vial::setDynamicEntry(link, op, i, data, retries); },
               [ctl, list, i, old, after] {
                   if (KeyboardState *k = ctl->keyboard()) {
                       (k->*list)[i] = old;
                       after();
                   }
               },
               {}, key + QString::number(i));
}

KeyButton *keyButton(const QString &code, std::function<void(const QString &)> onChange)
{
    auto *b = new KeyButton;
    b->setKeycode(code);
    QObject::connect(b, &KeyButton::changed, b, onChange);
    return b;
}

// eight modifier checkboxes, bit 0 LCtrl ... bit 7 RGui
QWidget *modsEditor(quint8 value, std::function<void(quint8)> onChange)
{
    static const char *names[] = {"LCtrl", "LShift", "LAlt", "LGui", "RCtrl", "RShift", "RAlt", "RGui"};
    auto *w = new QWidget;
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    auto boxes = std::make_shared<QList<QCheckBox *>>();
    for (int i = 0; i < 8; ++i) {
        auto *c = new QCheckBox(QString::fromLatin1(names[i]));
        c->setChecked(value & (1 << i));
        g->addWidget(c, i / 4, i % 4);
        boxes->append(c);
        QObject::connect(c, &QCheckBox::toggled, w, [boxes, onChange] {
            quint8 v = 0;
            for (int k = 0; k < boxes->size(); ++k)
                v |= quint8((*boxes)[k]->isChecked() ? 1 << k : 0);
            onChange(v);
        });
    }
    return w;
}

QWidget *centered(QLayout *form, const QString &note = {})
{
    auto *w = new QWidget;
    auto *outer = new QVBoxLayout(w);
    auto *inner = new QWidget;
    inner->setLayout(form);
    inner->setMaximumWidth(720);
    outer->addWidget(inner, 0, Qt::AlignTop | Qt::AlignLeft);
    if (!note.isEmpty()) {
        auto *n = new QLabel(note);
        n->setObjectName("armedHint");
        n->setTextFormat(Qt::RichText);
        outer->addWidget(n);
    }
    outer->addStretch(1);
    return w;
}

}

EntryPage::EntryPage(Controller *ctl, const QString &hint)
    : Page(ctl)
    , list_(new QListWidget)
    , area_(new QScrollArea)
    , hint_(new QLabel(hint))
{
    list_->setFixedWidth(250);
    list_->setTextElideMode(Qt::ElideRight);
    list_->setUniformItemSizes(true);
    area_->setWidgetResizable(true);
    area_->setFrameShape(QFrame::NoFrame);
    hint_->setObjectName("armedHint");
    hint_->setWordWrap(true);
    auto *right = new QVBoxLayout;
    right->addWidget(hint_);
    right->addWidget(area_, 1);
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->setSpacing(ui::theme::S4);
    l->addWidget(list_);
    l->addLayout(right, 1);
    connect(list_, &QListWidget::currentRowChanged, this, &EntryPage::show);
    connect(&KeycodeDisplay::instance(), &KeycodeDisplay::changed, this, [this] {
        for (int i = 0; i < list_->count(); ++i)
            refreshSummary(i);
    });
}

void EntryPage::keyboardChanged()
{
    QSignalBlocker block(list_);
    list_->clear();
    for (int i = 0; i < count(); ++i)
        list_->addItem(QString());
    for (int i = 0; i < count(); ++i)
        refreshSummary(i);
    list_->setCurrentRow(0);
    show(0);
}

void EntryPage::refreshSummary(int index)
{
    if (QListWidgetItem *it = list_->item(index)) {
        const QString s = summary(index);
        it->setText(QStringLiteral("%1   %2").arg(index, 2, 10, QLatin1Char('0')).arg(s.isEmpty() ? QStringLiteral("—") : s));
        it->setForeground(s.isEmpty() ? ui::theme::DIM : ui::theme::TXT);
    }
}

void EntryPage::show(int index)
{
    if (auto *tray = KeycodeTray::instance())
        tray->close();
    if (index < 0 || index >= count()) {
        area_->setWidget(new QWidget);
        return;
    }
    area_->setWidget(editor(index));
}

// ---- tap dance

TapDancePage::TapDancePage(Controller *ctl)
    : EntryPage(ctl, tr("A tap dance key does different things when tapped, held, double-tapped or tapped then held. "
                        "Put TD(n) on a key in the Keymap to use entry n."))
{
}

int TapDancePage::count() const
{
    return ctl->keyboard() ? int(ctl->keyboard()->tapDances.size()) : 0;
}

QString TapDancePage::summary(int i) const
{
    const auto &t = ctl->keyboard()->tapDances[i];
    QStringList parts;
    static const char *what[] = {"tap", "hold", "2×tap", "tap+hold"};
    for (int k = 0; k < 4; ++k)
        if (!none(t.keys[size_t(k)]))
            parts << QStringLiteral("%1 %2").arg(QLatin1String(what[k]), legend(t.keys[size_t(k)]));
    return parts.join(QStringLiteral(" · "));
}

QWidget *TapDancePage::editor(int i)
{
    const auto t = ctl->keyboard()->tapDances[i];
    auto *form = new QFormLayout;
    form->setSpacing(ui::theme::S3);
    auto after = [this, i] { refreshSummary(i); };
    static const char *labels[] = {"On tap", "On hold", "On double tap", "On tap + hold"};
    for (int k = 0; k < 4; ++k) {
        form->addRow(tr(labels[k]), keyButton(t.keys[size_t(k)], [this, i, k, after](const QString &code) {
            auto e = ctl->keyboard()->tapDances[i];
            e.keys[size_t(k)] = code;
            writeEntry(ctl, &KeyboardState::tapDances, i, e, vial::DYNAMIC_VIAL_TAP_DANCE_SET, 20, code == kReset,
                       "td-", after);
        }));
    }
    auto *term = new QSpinBox;
    term->setRange(0, 10000);
    term->setSuffix(tr(" ms"));
    term->setValue(t.tappingTerm);
    term->setToolTip(tr("How long a press counts as a tap"));
    connect(term, &QSpinBox::valueChanged, this, [this, i, after](int v) {
        auto e = ctl->keyboard()->tapDances[i];
        e.tappingTerm = v;
        writeEntry(ctl, &KeyboardState::tapDances, i, e, vial::DYNAMIC_VIAL_TAP_DANCE_SET, 20, false, "td-", after);
    });
    form->addRow(tr("Tapping term"), term);
    return centered(form, tr("Use <code>TD(%1)</code> to set up this action in the keymap.").arg(i));
}

// ---- combos

ComboPage::ComboPage(Controller *ctl)
    : EntryPage(ctl, tr("Press the keys of a combo together to send its output key instead."))
{
}

int ComboPage::count() const
{
    return ctl->keyboard() ? int(ctl->keyboard()->combos.size()) : 0;
}

QString ComboPage::summary(int i) const
{
    const auto &c = ctl->keyboard()->combos[i];
    QStringList keys;
    for (const auto &k : c.keys)
        if (!none(k))
            keys << legend(k);
    if (keys.isEmpty() && none(c.output))
        return {};
    return keys.join(QStringLiteral(" + ")) + QStringLiteral("  →  ") + legend(c.output);
}

QWidget *ComboPage::editor(int i)
{
    const auto c = ctl->keyboard()->combos[i];
    auto *form = new QFormLayout;
    form->setSpacing(ui::theme::S3);
    auto after = [this, i] { refreshSummary(i); };
    auto set = [this, i, after](std::function<void(dynamic::Combo &)> f) {
        auto e = ctl->keyboard()->combos[i];
        f(e);
        writeEntry(ctl, &KeyboardState::combos, i, e, vial::DYNAMIC_VIAL_COMBO_SET, 20, e.output == kReset, "combo-", after);
    };
    for (int k = 0; k < 4; ++k)
        form->addRow(tr("Key %1").arg(k + 1), keyButton(c.keys[size_t(k)], [set, k](const QString &code) {
            set([&](dynamic::Combo &e) { e.keys[size_t(k)] = code; });
        }));
    form->addRow(tr("Output key"), keyButton(c.output, [set](const QString &code) {
        set([&](dynamic::Combo &e) { e.output = code; });
    }));
    return centered(form);
}

// ---- key overrides

KeyOverridePage::KeyOverridePage(Controller *ctl)
    : EntryPage(ctl, tr("A key override sends another key when a key is pressed with certain modifiers, "
                        "e.g. Shift + Backspace → Delete."))
{
}

int KeyOverridePage::count() const
{
    return ctl->keyboard() ? int(ctl->keyboard()->keyOverrides.size()) : 0;
}

QString KeyOverridePage::summary(int i) const
{
    const auto &k = ctl->keyboard()->keyOverrides[i];
    if (none(k.trigger) && none(k.replacement))
        return {};
    static const char *mods[] = {"LCtrl", "LShift", "LAlt", "LGui", "RCtrl", "RShift", "RAlt", "RGui"};
    QStringList m;
    for (int b = 0; b < 8; ++b)
        if (k.triggerMods & (1 << b))
            m << QLatin1String(mods[b]);
    m << legend(k.trigger);
    QString s = m.join(QStringLiteral("+")) + QStringLiteral("  →  ") + legend(k.replacement);
    if (!(k.options & dynamic::KeyOverride::Enabled))
        s += tr("  (off)");
    return s;
}

QWidget *KeyOverridePage::editor(int i)
{
    using KO = dynamic::KeyOverride;
    const KO ko = ctl->keyboard()->keyOverrides[i];
    auto *form = new QFormLayout;
    form->setSpacing(ui::theme::S3);
    auto after = [this, i] { refreshSummary(i); };
    auto set = [this, i, after](std::function<void(KO &)> f) {
        auto e = ctl->keyboard()->keyOverrides[i];
        f(e);
        writeEntry(ctl, &KeyboardState::keyOverrides, i, e, vial::DYNAMIC_VIAL_KEY_OVERRIDE_SET, 1,
                   e.replacement == kReset, "ko-", after);
    };
    auto option = [set, &ko](int bit, const QString &text) {
        auto *c = new QCheckBox(text);
        c->setChecked(ko.options & bit);
        QObject::connect(c, &QCheckBox::toggled, c, [set, bit](bool on) {
            set([&](KO &e) { e.options = quint8(on ? (e.options | bit) : (e.options & ~bit)); });
        });
        return c;
    };
    form->addRow(tr("Enabled"), option(KO::Enabled, QString()));

    auto *layers = new QWidget;
    auto *lg = new QGridLayout(layers);
    lg->setContentsMargins(0, 0, 0, 0);
    auto boxes = std::make_shared<QList<QCheckBox *>>();
    const int nLayers = std::max(1, std::min(16, ctl->keyboard()->layers));
    auto apply = [boxes, set] {
        quint16 v = 0;
        for (int b = 0; b < boxes->size(); ++b)
            v |= quint16((*boxes)[b]->isChecked() ? 1 << b : 0);
        set([&](KO &e) { e.layers = quint16((e.layers & ~((1u << boxes->size()) - 1)) | v); });
    };
    for (int b = 0; b < nLayers; ++b) {
        auto *c = new QCheckBox(QString::number(b));
        c->setChecked(ko.layers & (1 << b));
        lg->addWidget(c, b / 8, b % 8);
        boxes->append(c);
        connect(c, &QCheckBox::toggled, this, apply);
    }
    auto *all = new QPushButton(tr("All"));
    auto *noLayers = new QPushButton(tr("None"));
    for (auto [btn, on] : {std::pair{all, true}, std::pair{noLayers, false}}) {
        connect(btn, &QPushButton::clicked, this, [boxes, on, apply] {
            for (auto *c : *boxes) {
                QSignalBlocker block(c);
                c->setChecked(on);
            }
            apply();
        });
    }
    auto *allNone = new QHBoxLayout;
    allNone->addWidget(all);
    allNone->addWidget(noLayers);
    allNone->addStretch(1);
    lg->addLayout(allNone, 2, 0, 1, 8);
    form->addRow(tr("On layers"), layers);

    form->addRow(tr("Trigger"), keyButton(ko.trigger, [set](const QString &code) { set([&](KO &e) { e.trigger = code; }); }));
    form->addRow(tr("Trigger mods"), modsEditor(ko.triggerMods, [set](quint8 v) { set([&](KO &e) { e.triggerMods = v; }); }));
    form->addRow(tr("Negative mods"), modsEditor(ko.negativeModMask, [set](quint8 v) { set([&](KO &e) { e.negativeModMask = v; }); }));
    form->addRow(tr("Suppressed mods"), modsEditor(ko.suppressedMods, [set](quint8 v) { set([&](KO &e) { e.suppressedMods = v; }); }));
    form->addRow(tr("Replacement"), keyButton(ko.replacement, [set](const QString &code) { set([&](KO &e) { e.replacement = code; }); }));

    auto *opts = new QVBoxLayout;
    opts->addWidget(option(KO::ActivationTriggerDown, tr("Activate when the trigger key is pressed down")));
    opts->addWidget(option(KO::ActivationRequiredModDown, tr("Activate when a necessary modifier is pressed down")));
    opts->addWidget(option(KO::ActivationNegativeModUp, tr("Activate when a negative modifier is released")));
    opts->addWidget(option(KO::OneMod, tr("Activate on one modifier")));
    opts->addWidget(option(KO::NoReregisterTrigger, tr("Don't register the trigger key again after the override is deactivated")));
    opts->addWidget(option(KO::NoUnregisterOnOtherKeyDown, tr("Don't deactivate when another key is pressed down")));
    auto *ow = new QWidget;
    ow->setLayout(opts);
    opts->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Options"), ow);
    return centered(form);
}

// ---- alt repeat key

AltRepeatKeyPage::AltRepeatKeyPage(Controller *ctl)
    : EntryPage(ctl, tr("Alt Repeat Key sends an alternative of the last key, e.g. after Left it sends Right."))
{
}

int AltRepeatKeyPage::count() const
{
    return ctl->keyboard() ? int(ctl->keyboard()->altRepeatKeys.size()) : 0;
}

QString AltRepeatKeyPage::summary(int i) const
{
    const auto &a = ctl->keyboard()->altRepeatKeys[i];
    if (none(a.keycode) && none(a.altKeycode))
        return {};
    QString s = legend(a.keycode) + QStringLiteral("  →  ") + legend(a.altKeycode);
    if (!(a.options & dynamic::AltRepeatKey::Enabled))
        s += tr("  (off)");
    return s;
}

QWidget *AltRepeatKeyPage::editor(int i)
{
    using ARK = dynamic::AltRepeatKey;
    const ARK a = ctl->keyboard()->altRepeatKeys[i];
    auto *form = new QFormLayout;
    form->setSpacing(ui::theme::S3);
    auto after = [this, i] { refreshSummary(i); };
    auto set = [this, i, after](std::function<void(ARK &)> f) {
        auto e = ctl->keyboard()->altRepeatKeys[i];
        f(e);
        writeEntry(ctl, &KeyboardState::altRepeatKeys, i, e, vial::DYNAMIC_VIAL_ALT_REPEAT_KEY_SET, 1,
                   e.keycode == kReset || e.altKeycode == kReset, "ark-", after);
    };
    auto option = [set, &a](int bit, const QString &text) {
        auto *c = new QCheckBox(text);
        c->setChecked(a.options & bit);
        QObject::connect(c, &QCheckBox::toggled, c, [set, bit](bool on) {
            set([&](ARK &e) { e.options = quint8(on ? (e.options | bit) : (e.options & ~bit)); });
        });
        return c;
    };
    form->addRow(tr("Enabled"), option(ARK::Enabled, QString()));
    form->addRow(tr("Last key"), keyButton(a.keycode, [set](const QString &code) { set([&](ARK &e) { e.keycode = code; }); }));
    form->addRow(tr("Alt key"), keyButton(a.altKeycode, [set](const QString &code) { set([&](ARK &e) { e.altKeycode = code; }); }));
    form->addRow(tr("Allowed mods"), modsEditor(a.allowedMods, [set](quint8 v) { set([&](ARK &e) { e.allowedMods = v; }); }));
    auto *opts = new QVBoxLayout;
    opts->setContentsMargins(0, 0, 0, 0);
    opts->addWidget(option(ARK::DefaultToThisAltKey, tr("Default to this alt key")));
    opts->addWidget(option(ARK::Bidirectional, tr("Bidirectional")));
    opts->addWidget(option(ARK::IgnoreModHandedness, tr("Ignore mod handedness")));
    auto *ow = new QWidget;
    ow->setLayout(opts);
    form->addRow(tr("Options"), ow);
    return centered(form);
}
