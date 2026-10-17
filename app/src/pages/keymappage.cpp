#include "pages/keymappage.h"

#include "core/appinfo.h"
#include "keycodes/keycodes.h"
#include "model/layoutoptions.h"
#include "ui/anykeycodedialog.h"
#include "ui/keycodedisplay.h"
#include "ui/theme.h"
#include "ui/widgets/keyboardview.h"
#include "ui/widgets/keycodepalette.h"

#include <QButtonGroup>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>

using keycodes::Registry;

KeymapPage::KeymapPage(Controller *ctl)
    : Page(ctl)
    , view_(new KeyboardView)
    , palette_(new KeycodePalette)
    , layerRow_(new QHBoxLayout)
    , layers_(new QButtonGroup(this))
    , selection_(new QLabel)
{
    setFocusPolicy(Qt::StrongFocus);
    auto *top = new QHBoxLayout;
    top->setSpacing(ui::theme::S2);
    auto *layerLabel = new QLabel(tr("LAYER"));
    layerLabel->setObjectName("armedHint");
    top->addWidget(layerLabel);
    top->addLayout(layerRow_);
    top->addSpacing(ui::theme::S5);
    selection_->setObjectName("armedHint");
    top->addWidget(selection_, 1);
    auto *minus = new QPushButton(QStringLiteral("−"));
    auto *plus = new QPushButton(QStringLiteral("+"));
    for (auto *b : {minus, plus}) {
        b->setFixedSize(30, 28);
        b->setFocusPolicy(Qt::NoFocus);
    }
    minus->setToolTip(tr("Smaller keyboard (Ctrl+-)"));
    plus->setToolTip(tr("Bigger keyboard (Ctrl+=)"));
    connect(minus, &QPushButton::clicked, this, [this] { zoom(-0.1); });
    connect(plus, &QPushButton::clicked, this, [this] { zoom(0.1); });
    top->addWidget(minus);
    top->addWidget(plus);
    for (auto [seq, d] : {std::pair{QKeySequence::ZoomOut, -0.1}, std::pair{QKeySequence::ZoomIn, 0.1}}) {
        auto *sc = new QShortcut(seq, this);
        connect(sc, &QShortcut::activated, this, [this, d] { zoom(d); });
    }

    fitted_ = new FittedKeyboard(view_);
    auto *viewHost = new QWidget;
    auto *vh = new QVBoxLayout(viewHost);
    vh->setContentsMargins(0, 0, 0, 0);
    vh->addWidget(fitted_);
    vh->addStretch(1);

    auto *upper = new QWidget;
    auto *ul = new QVBoxLayout(upper);
    ul->setContentsMargins(0, 0, 0, 0);
    ul->addLayout(top);
    ul->addWidget(viewHost, 1);

    auto *split = new QSplitter(Qt::Vertical);
    split_ = split;
    split->addWidget(upper);
    split->addWidget(palette_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setChildrenCollapsible(false);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->addWidget(split);

    QSettings s(appinfo::kSettingsOrg, appinfo::kSettingsApp);
    userScale_ = std::clamp(s.value("keymap_scale", 1.0).toDouble(), 0.4, 3.0);

    connect(view_, &KeyboardView::clicked, this, &KeymapPage::onKeyClicked);
    connect(view_, &KeyboardView::deselected, this, [this] {
        palette_->setBasicOnly(false);
        refresh();
    });
    connect(view_, &KeyboardView::doubleClicked, this, &KeymapPage::anyKeycode);
    connect(palette_, &KeycodePalette::keycodeSelected, this, &KeymapPage::assign);
    connect(palette_, &KeycodePalette::anyKeycodeRequested, this, &KeymapPage::anyKeycode);
    connect(layers_, &QButtonGroup::idClicked, this, &KeymapPage::setLayer);
    connect(ctl, &Controller::keymapChanged, this, &KeymapPage::refresh);
    connect(&KeycodeDisplay::instance(), &KeycodeDisplay::changed, this, &KeymapPage::refresh);
}

void KeymapPage::keyboardChanged()
{
    const KeyboardState *kb = ctl->keyboard();
    for (auto *b : layers_->buttons()) {
        layers_->removeButton(b);
        b->deleteLater();
    }
    if (!kb)
        return;
    for (int i = 0; i < kb->layers; ++i) {
        auto *b = new QPushButton(QString::number(i));
        b->setCheckable(true);
        b->setFixedSize(32, 28);
        b->setToolTip(tr("Layer %1 (PgUp / PgDn switch layers)").arg(i));
        layers_->addButton(b, i);
        layerRow_->addWidget(b);
    }
    view_->setKeys(kb->def.keys, kb->def.encoders);
    const auto choices = layoutoptions::parse(kb->def.layoutLabels);
    view_->setLayoutChoice([this, choices](int index) {
        const KeyboardState *k = ctl->keyboard();
        if (!k || k->layoutOptions < 0)
            return 0;
        return layoutoptions::unpack(quint32(k->layoutOptions), choices).value(index);
    });
    shownOptions_ = kb->layoutOptions;
    palette_->rebuild();
    setLayer(0);
    fit();
    // the keyboard gets its height, the keycodes the rest
    const int upper = view_->sizeHint().height() + 60;
    split_->setSizes({upper, std::max(200, height() - upper)});
}

void KeymapPage::setLayer(int layer)
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    layer_ = std::clamp(layer, 0, std::max(0, kb->layers - 1));
    if (auto *b = layers_->button(layer_))
        b->setChecked(true);
    refresh();
}

QString KeymapPage::codeOf(int keyIndex) const
{
    const KeyboardState *kb = ctl->keyboard();
    const auto &k = view_->keys()[keyIndex];
    if (k.encoder)
        return kb->encoder(layer_, k.def.encoder, k.def.direction);
    return kb->key(layer_, k.def.row, k.def.col);
}

void KeymapPage::refresh()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    if (kb->layoutOptions != shownOptions_) {
        shownOptions_ = kb->layoutOptions;
        view_->relayout();
    }
    auto &keys = view_->keys();
    for (int i = 0; i < keys.size(); ++i) {
        const auto lg = KeycodeDisplay::instance().legend(codeOf(i));
        auto &k = keys[i];
        k.text = lg.text;
        k.maskText = lg.maskText;
        k.masked = lg.masked;
        k.tooltip = lg.tooltip;
        k.color = lg.color;
        k.maskColor = lg.maskColor;
    }
    const int a = view_->activeIndex();
    if (a >= 0) {
        const auto &k = keys[a];
        const QString where = k.encoder ? tr("ENCODER %1 %2").arg(k.def.encoder).arg(k.def.direction ? "CW" : "CCW")
                                        : tr("KEY %1,%2").arg(k.def.row).arg(k.def.col);
        selection_->setText(tr("%1 · %2 — pick a keycode below, double-click for any · Del clears · Esc")
                                .arg(where, codeOf(a)));
    } else {
        selection_->setText(tr("Click a key, then a keycode below"));
    }
    view_->update();
}

void KeymapPage::onKeyClicked(int, bool mask)
{
    palette_->setBasicOnly(mask);
    refresh();
}

void KeymapPage::assign(const QString &picked)
{
    const int a = view_->activeIndex();
    if (a < 0 || !ctl->keyboard())
        return;
    const Registry &reg = Registry::instance();
    const auto &k = view_->keys()[a];
    QString code = picked;
    if (view_->activeMask()) {
        // the inner part of a masked keycode takes basic keycodes only
        if (!reg.isBasic(code))
            return;
        const auto *outer = reg.findOuter(codeOf(a));
        if (!outer)
            return;
        code = QString(outer->qmkId).replace(QStringLiteral("(kc)"), QStringLiteral("(%1)").arg(code));
    }
    if (k.encoder)
        ctl->setEncoder(layer_, k.def.encoder, k.def.direction, code);
    else if (k.def.row >= 0 && k.def.col >= 0)
        ctl->setKey(layer_, k.def.row, k.def.col, code);
    view_->selectNext();
    palette_->setBasicOnly(false);
    refresh();
}

void KeymapPage::anyKeycode()
{
    const int a = view_->activeIndex();
    if (a < 0)
        return;
    QString current = codeOf(a);
    if (view_->activeMask()) {
        if (const auto *inner = Registry::instance().findInner(current))
            current = inner->qmkId;
    }
    auto *dlg = new AnyKeycodeDialog(current, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg] { assign(dlg->value()); });
    dlg->open();
}

void KeymapPage::zoom(double delta)
{
    userScale_ = std::clamp(userScale_ + delta, 0.4, 3.0);
    QSettings(appinfo::kSettingsOrg, appinfo::kSettingsApp).setValue("keymap_scale", userScale_);
    fit();
}

void KeymapPage::fit()
{
    // the chosen zoom, unless the keyboard would not fit across
    fitted_->setPreferredScale(userScale_);
}

void KeymapPage::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        view_->deselect();
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (view_->activeIndex() >= 0)
            assign(QStringLiteral("KC_NO"));
        return;
    case Qt::Key_PageUp:
        setLayer(layer_ - 1);
        return;
    case Qt::Key_PageDown:
        setLayer(layer_ + 1);
        return;
    default:
        Page::keyPressEvent(e);
    }
}
