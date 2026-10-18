#include "app/firmwareflasher.h"

#include "hid/devices.h"
#include "ui/unlocker.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent>

namespace {

// Vial's UID bytes: the keyboard id, little-endian
QByteArray uidBytes(quint64 id)
{
    QByteArray b;
    for (int i = 0; i < 8; ++i)
        b.append(char((id >> (8 * i)) & 0xFF));
    return b;
}

}

FirmwareFlasher *FirmwareFlasher::instance(Controller *ctl)
{
    static QPointer<FirmwareFlasher> f;
    if (!f)
        f = new FirmwareFlasher(ctl);
    return f;
}

FirmwareFlasher::FirmwareFlasher(Controller *ctl)
    : QObject(ctl)
    , ctl_(ctl)
{
    connect(ctl, &Controller::keyboardChanged, this, &FirmwareFlasher::onKeyboard);
}

void FirmwareFlasher::log(const QString &line)
{
    const QString l = QStringLiteral("[%1] %2").arg(QTime::currentTime().toString("HH:mm:ss"), line);
    log_.append(l);
    emit logged(l);
}

void FirmwareFlasher::start(const QByteArray &firmware, bool restore)
{
    if (running_)
        return;
    log_.clear();
    progress_ = 0;
    emit progressed(0);
    auto parsed = vibl::parse(firmware);
    if (auto *err = std::get_if<QString>(&parsed)) {
        log(*err);
        return;
    }
    pkg_ = std::get<vibl::Package>(parsed);
    log(tr("* Firmware build date: %1 (UTC)")
            .arg(QDateTime::fromSecsSinceEpoch(qint64(pkg_.timestamp), QTimeZone::UTC).toString("yyyy-MM-dd HH:mm:ss")));
    running_ = true;
    emit runningChanged(true);

    const auto cur = ctl_->current();
    if (cur && cur->kind == hid::DeviceKind::Bootloader) {
        layout_.reset();
        uid_ = pkg_.uid;
        ctl_->setBusy(true);
        flashAt(cur->path);
        return;
    }
    const KeyboardState *kb = ctl_->keyboard();
    if (!kb || !kb->keyboardId) {
        finish(false, tr("Error: no Vial keyboard to flash"));
        return;
    }
    uid_ = uidBytes(*kb->keyboardId);
    if (uid_ != pkg_.uid) {
        finish(false, tr("Error: Firmware UID does not match keyboard UID. Check that you have the correct file"));
        return;
    }
    layout_.reset();
    if (!restore) {
        enterBootloader();
        return;
    }
    log(tr("Backing up current layout..."));
    ctl_->loadEntries(AllEntries, [this](bool ok) {
        const KeyboardState *kb = ctl_->keyboard();
        if (!ok || !kb) {
            finish(false, tr("Error: could not read the current layout"));
            return;
        }
        layout_ = kb->saveLayout();
        enterBootloader();
    });
}

void FirmwareFlasher::enterBootloader()
{
    Unlocker::ensure(ctl_, ctl_->dialogParent(), [this](bool ok) {
        if (!ok) {
            finish(false, tr("The keyboard was not unlocked"));
            return;
        }
        log(tr("Restarting in bootloader mode..."));
        ctl_->session()->run(hid::Priority::Interactive, this, [](hid::Link &link) { vial::jumpToBootloader(link); },
                             [this](hid::Result<hid::Unit>) {
            ctl_->setBusy(true);
            waitForBootloader(30);
        });
    });
}

void FirmwareFlasher::waitForBootloader(int triesLeft)
{
    if (triesLeft <= 0) {
        finish(false, tr("Error: the bootloader did not show up"));
        return;
    }
    log(tr("Looking for devices..."));
    auto *w = new QFutureWatcher<QByteArray>(this);
    connect(w, &QFutureWatcher<QByteArray>::finished, this, [this, w, triesLeft] {
        w->deleteLater();
        const QByteArray path = w->result();
        if (path.isEmpty())
            QTimer::singleShot(1000, this, [this, triesLeft] { waitForBootloader(triesLeft - 1); });
        else
            flashAt(path);
    });
    w->setFuture(QtConcurrent::run([uid = uid_] {
        for (const auto &d : hid::discover({})) {
            if (d.kind != hid::DeviceKind::Bootloader)
                continue;
            if (auto dev = hid::HidapiDevice::open(d.path)) {
                try {
                    if (vibl::uid(*dev) == uid)
                        return d.path;
                } catch (const std::exception &) {
                }
            }
        }
        return QByteArray();
    }));
}

void FirmwareFlasher::flashAt(const QByteArray &path)
{
    log(tr("Found Vial Bootloader device at %1").arg(QString::fromUtf8(path)));
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
        w->deleteLater();
        const QString err = w->result();
        if (!err.isEmpty())
            finish(false, err);
        else
            finish(true, tr("Done!"));
    });
    const bool insecure = layout_.has_value();
    w->setFuture(QtConcurrent::run([this, path, pkg = pkg_, insecure]() -> QString {
        auto dev = hid::HidapiDevice::open(path);
        if (!dev)
            return tr("Error: could not open the bootloader");
        try {
            vibl::flash(*dev, pkg, insecure,
                        [this](const QString &l) { QMetaObject::invokeMethod(this, [this, l] { log(l); }); },
                        [this](double p) {
                            QMetaObject::invokeMethod(this, [this, p] {
                                progress_ = p;
                                emit progressed(p);
                            });
                        });
        } catch (const std::exception &e) {
            return QString::fromUtf8(e.what());
        }
        return {};
    }));
}

void FirmwareFlasher::finish(bool ok, const QString &msg)
{
    log(msg);
    if (ok) {
        progress_ = 1;
        emit progressed(1);
    }
    if (!ok || !layout_) {
        running_ = false;
        emit runningChanged(false);
        layout_.reset();
    } else {
        log(tr("Waiting for the keyboard to restore the layout..."));
    }
    ctl_->setBusy(false);
    ctl_->refreshDevices();
}

void FirmwareFlasher::onKeyboard()
{
    if (!running_ || !layout_)
        return;
    const KeyboardState *kb = ctl_->keyboard();
    if (!kb || !kb->keyboardId || uidBytes(*kb->keyboardId) != uid_)
        return;
    log(tr("Restoring saved layout..."));
    const QByteArray layout = *layout_;
    layout_.reset();
    ctl_->restoreLayout(layout, [this](bool ok, const QString &msg) {
        log(msg);
        ctl_->lockKeyboard();
        running_ = false;
        emit runningChanged(false);
        ctl_->toast(ok ? tr("Firmware updated, layout restored") : tr("Firmware updated; the layout was not restored"),
                    ok ? Controller::ToastKind::Ok : Controller::ToastKind::Error);
    });
}
