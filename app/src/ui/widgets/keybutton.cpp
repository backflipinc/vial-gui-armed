#include "ui/widgets/keybutton.h"

#include "keycodes/keycodes.h"
#include "ui/anykeycodedialog.h"
#include "ui/keycodedisplay.h"
#include "ui/theme.h"
#include "ui/widgets/keycodepalette.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

using keycodes::Registry;

namespace {
QPointer<KeycodeTray> tray;
}

KeyButton::KeyButton(QWidget *parent)
    : KeyboardView(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    LayoutKey k;
    k.role = LayoutKey::Role::Key;
    k.row = k.col = 0;
    setKeys({k}, {});
    connect(this, &KeyboardView::clicked, this, [this](int, bool mask) {
        if (tray)
            tray->open(this, mask || basicOnly_);
    });
    connect(this, &KeyboardView::deselected, this, [this] {
        if (tray && tray->target() == this)
            tray->close();
    });
    connect(this, &KeyboardView::doubleClicked, this, &KeyButton::anyKeycode);
    connect(&KeycodeDisplay::instance(), &KeycodeDisplay::changed, this, &KeyButton::refresh);
    refresh();
}

KeyButton::~KeyButton()
{
    if (tray && tray->target() == this)
        tray->close();
}

void KeyButton::setKeycode(const QString &code)
{
    code_ = code;
    refresh();
}

void KeyButton::refresh()
{
    if (keys().isEmpty())
        return;
    const auto lg = KeycodeDisplay::instance().legend(code_);
    auto &k = keys()[0];
    k.text = lg.text;
    k.maskText = lg.maskText;
    k.masked = lg.masked;
    k.tooltip = lg.tooltip.isEmpty() ? tr("Click to choose a keycode") : lg.tooltip;
    k.color = lg.color;
    k.maskColor = lg.maskColor;
    update();
}

void KeyButton::apply(const QString &code)
{
    if (code == code_)
        return;
    if (basicOnly_ && !Registry::instance().isBasic(code))
        return;
    code_ = code;
    refresh();
    emit changed(code_);
}

void KeyButton::pick(const QString &picked)
{
    QString code = picked;
    if (activeMask()) {
        const Registry &reg = Registry::instance();
        if (!reg.isBasic(code))
            return;
        const auto *outer = reg.findOuter(code_);
        if (!outer)
            return;
        code = QString(outer->qmkId).replace(QStringLiteral("(kc)"), QStringLiteral("(%1)").arg(code));
    }
    apply(code);
}

void KeyButton::anyKeycode()
{
    QString current = code_;
    if (activeMask())
        if (const auto *inner = Registry::instance().findInner(code_))
            current = inner->qmkId;
    auto *dlg = new AnyKeycodeDialog(current, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg] { pick(dlg->value()); });
    dlg->open();
}

void KeyButton::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        apply(QStringLiteral("KC_NO"));
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        setActive(0, false);
        if (tray)
            tray->open(this, basicOnly_);
        return;
    case Qt::Key_Escape:
        deselect();
        return;
    default:
        KeyboardView::keyPressEvent(e);
    }
}

// ---- tray

KeycodeTray *KeycodeTray::instance()
{
    return tray;
}

KeycodeTray::KeycodeTray(QWidget *parent)
    : QWidget(parent)
{
    tray = this;
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    auto *head = new QHBoxLayout;
    auto *title = new QLabel(tr("CHOOSE A KEYCODE"));
    title->setObjectName("armedHint");
    auto *closeBtn = new QPushButton(tr("Close"));
    closeBtn->setToolTip(tr("Esc"));
    connect(closeBtn, &QPushButton::clicked, this, &KeycodeTray::close);
    head->addWidget(title);
    head->addStretch(1);
    head->addWidget(closeBtn);
    l->addLayout(head);
    setMinimumHeight(260);
    hide();
}

// built on first use: every keycode button is a lot to make before the window's first frame
KeycodePalette *KeycodeTray::palette()
{
    if (palette_)
        return palette_;
    palette_ = new KeycodePalette;
    static_cast<QVBoxLayout *>(layout())->addWidget(palette_, 1);
    connect(palette_, &KeycodePalette::keycodeSelected, this, [this](const QString &code) {
        if (target_)
            target_->pick(code);
    });
    connect(palette_, &KeycodePalette::anyKeycodeRequested, this, [this] {
        if (target_)
            target_->anyKeycode();
    });
    return palette_;
}

KeycodeTray::~KeycodeTray()
{
    if (tray == this)
        tray = nullptr;
}

void KeycodeTray::open(KeyButton *target, bool basicOnly)
{
    if (target_ && target_ != target)
        target_->deselect();
    target_ = target;
    palette()->setBasicOnly(basicOnly);
    show();
}

void KeycodeTray::close()
{
    KeyButton *t = target_;
    target_ = nullptr;
    if (t)
        t->deselect();
    hide();
}

void KeycodeTray::rebuild()
{
    if (palette_)
        palette_->rebuild();
}

void KeycodeTray::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}
