#include "ui/anykeycodedialog.h"

#include "keycodes/keycodes.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

AnyKeycodeDialog::AnyKeycodeDialog(const QString &initial, QWidget *parent)
    : QDialog(parent)
    , entry_(new QLineEdit)
    , computed_(new QLabel)
    , buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel))
{
    setWindowTitle(tr("Enter an arbitrary keycode"));
    setMinimumWidth(420);
    auto *l = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("A keycode name or an expression: LCTL(KC_A), LT(2, KC_SPC), MT(MOD_LSFT, KC_ESC), 0x7C00"));
    hint->setObjectName("armedHint");
    hint->setWordWrap(true);
    l->addWidget(entry_);
    l->addWidget(computed_);
    l->addWidget(hint);
    l->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(entry_, &QLineEdit::textChanged, this, &AnyKeycodeDialog::onChange);
    entry_->setText(initial);
    entry_->selectAll();
    onChange();
}

void AnyKeycodeDialog::onChange()
{
    const QString text = entry_->text().trimmed();
    value_.clear();
    if (text.isEmpty()) {
        computed_->setText(tr("Enter an expression"));
    } else if (auto v = keycodes::Registry::instance().deserializeStrict(text); v && *v >= 0 && *v <= 0xFFFF) {
        value_ = keycodes::Registry::instance().serialize(*v);
        computed_->setText(tr("Computed value: 0x%1  →  %2")
                               .arg(QString::number(*v, 16).rightJustified(4, QLatin1Char('0')).toUpper(), value_));
    } else {
        computed_->setText(tr("Invalid input"));
    }
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!value_.isEmpty());
}
