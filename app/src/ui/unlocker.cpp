#include "ui/unlocker.h"

#include "ui/theme.h"
#include "ui/widgets/keyboardview.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

void Unlocker::ensure(Controller *ctl, QWidget *parent, std::function<void(bool)> done)
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !ctl->session()) {
        done(false);
        return;
    }
    const int proto = int(kb->vialProtocol);
    ctl->session()->run(hid::Priority::Interactive, parent, [proto](hid::Link &link) {
        return vial::unlockStatus(link, proto);
    }, [ctl, parent, done](hid::Result<vial::UnlockStatus> r) {
        if (!r.ok()) {
            ctl->toast(QObject::tr("Could not ask the keyboard: %1").arg(r.error), Controller::ToastKind::Error);
            done(false);
            return;
        }
        if (r.value->unlocked) {
            done(true);
            return;
        }
        auto *dlg = new Unlocker(ctl, parent);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        for (auto &k : dlg->view_->keys())
            k.on = r.value->keys.contains({k.def.row, k.def.col});
        dlg->view_->update();
        QObject::connect(dlg, &QDialog::finished, parent, [ctl, done](int res) {
            ctl->refreshLock();
            done(res == QDialog::Accepted);
        });
        dlg->open();
        dlg->start();
    });
}

Unlocker::Unlocker(Controller *ctl, QWidget *parent)
    : QDialog(parent)
    , ctl_(ctl)
    , view_(new KeyboardView)
    , progress_(new QProgressBar)
{
    setWindowTitle(tr("Unlock the keyboard"));
    auto *l = new QVBoxLayout(this);
    l->setSpacing(ui::theme::S3);
    auto *t = new QLabel(tr("To go on, the keyboard has to be unlocked. Only do this on a computer you trust.<br>"
                            "It stays unlocked until you replug it or choose <b>Security &gt; Lock</b>.<br><br>"
                            "<b>Press and hold the highlighted keys until the bar fills up:</b>"));
    t->setWordWrap(true);
    l->addWidget(t);
    view_->setSelectable(false);
    view_->setScale(0.5);
    if (const KeyboardState *kb = ctl->keyboard())
        view_->setKeys(kb->def.keys, kb->def.encoders);
    l->addWidget(view_, 0, Qt::AlignHCenter);
    progress_->setRange(0, 1);
    progress_->setValue(0);
    l->addWidget(progress_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(buttons);
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &Unlocker::poll);
}

void Unlocker::start()
{
    ctl_->session()->run(hid::Priority::Interactive, this, [](hid::Link &link) { vial::unlockStart(link); },
                         [this](hid::Result<hid::Unit> r) {
        if (!r.ok()) {
            ctl_->toast(tr("Unlocking failed: %1").arg(r.error), Controller::ToastKind::Error);
            reject();
            return;
        }
        timer_.start();
    });
}

void Unlocker::poll()
{
    ctl_->session()->run(hid::Priority::Interactive, this, [](hid::Link &link) { return vial::unlockPoll(link); },
                         [this](hid::Result<QByteArray> r) {
        if (!r.ok())
            return;
        const int unlocked = quint8(r.value->size() > 0 ? r.value->at(0) : 0);
        const int counter = quint8(r.value->size() > 2 ? r.value->at(2) : 0);
        maxCounter_ = std::max(maxCounter_, counter);
        progress_->setMaximum(maxCounter_);
        progress_->setValue(maxCounter_ - counter);
        if (unlocked == 1) {
            timer_.stop();
            accept();
        }
    }, QStringLiteral("unlock-poll"));
}
