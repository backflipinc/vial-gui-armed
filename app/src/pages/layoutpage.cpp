#include "pages/layoutpage.h"

#include "ui/theme.h"
#include "ui/widgets/keyboardview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

LayoutPage::LayoutPage(Controller *ctl)
    : Page(ctl)
    , view_(new KeyboardView)
    , grid_(new QGridLayout)
{
    view_->setSelectable(false);
    auto *fitted = new FittedKeyboard(view_);
    fitted->setPreferredScale(0.7);
    auto *l = new QVBoxLayout(this);
    l->addStretch(1);
    l->addWidget(fitted);
    l->addSpacing(ui::theme::S5);
    auto *form = new QWidget;
    form->setLayout(grid_);
    grid_->setHorizontalSpacing(ui::theme::S5);
    grid_->setVerticalSpacing(ui::theme::S2);
    l->addWidget(form, 0, Qt::AlignHCenter);
    l->addStretch(2);
    connect(ctl, &Controller::keymapChanged, this, &LayoutPage::sync);
}

void LayoutPage::keyboardChanged()
{
    qDeleteAll(editors_);
    editors_.clear();
    const KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    choices_ = layoutoptions::parse(kb->def.layoutLabels);
    for (int i = 0; i < choices_.size(); ++i) {
        auto *label = new QLabel(choices_[i].label);
        grid_->addWidget(label, i, 0);
        editors_.append(label);
        QWidget *editor;
        if (choices_[i].options.isEmpty()) {
            auto *cb = new QCheckBox;
            connect(cb, &QCheckBox::toggled, this, &LayoutPage::changed);
            editor = cb;
        } else {
            auto *combo = new QComboBox;
            combo->addItems(choices_[i].options);
            connect(combo, &QComboBox::currentIndexChanged, this, &LayoutPage::changed);
            editor = combo;
        }
        label->setBuddy(editor);
        grid_->addWidget(editor, i, 1);
        editors_.append(editor);
    }
    view_->setKeys(kb->def.keys, kb->def.encoders);
    view_->setLayoutChoice([this](int index) {
        const KeyboardState *k = ctl->keyboard();
        return k ? layoutoptions::unpack(quint32(std::max<qint64>(0, k->layoutOptions)), choices_).value(index) : 0;
    });
    sync();
}

void LayoutPage::sync()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    updating_ = true;
    const auto values = layoutoptions::unpack(quint32(std::max<qint64>(0, kb->layoutOptions)), choices_);
    for (int i = 0; i < choices_.size(); ++i) {
        QWidget *e = editors_.value(i * 2 + 1);
        if (auto *cb = qobject_cast<QCheckBox *>(e))
            cb->setChecked(values.value(i));
        else if (auto *combo = qobject_cast<QComboBox *>(e))
            combo->setCurrentIndex(values.value(i));
    }
    updating_ = false;
    view_->relayout();
}

void LayoutPage::changed()
{
    if (updating_)
        return;
    QList<int> values;
    for (int i = 0; i < choices_.size(); ++i) {
        QWidget *e = editors_.value(i * 2 + 1);
        if (auto *cb = qobject_cast<QCheckBox *>(e))
            values.append(cb->isChecked() ? 1 : 0);
        else if (auto *combo = qobject_cast<QComboBox *>(e))
            values.append(combo->currentIndex());
    }
    ctl->setLayoutOptions(layoutoptions::pack(values, choices_));
}
