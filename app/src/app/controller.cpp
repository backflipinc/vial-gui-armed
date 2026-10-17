#include "app/controller.h"

#include "keycodes/keycodes.h"
#include "model/qmksettings.h"
#include "ui/unlocker.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcController, "armed.controller")

Controller::Controller(QObject *parent)
    : QObject(parent)
{
    connect(&monitor_, &hid::DeviceMonitor::devicesChanged, this, &Controller::onDevices);
    clockTimer_.setInterval(10 * 60 * 1000);
    connect(&clockTimer_, &QTimer::timeout, this, &Controller::syncClock);
    monitor_.refresh();
}

Controller::~Controller() = default;

std::optional<hid::DeviceInfo> Controller::current() const
{
    return current_;
}

void Controller::setState(State s, const QString &error)
{
    state_ = s;
    error_ = error;
    emit stateChanged();
}

void Controller::onDevices(const QList<hid::DeviceInfo> &devices)
{
    emit devicesChanged();
    if (current_ && devices.contains(*current_))
        return;
    if (current_)
        close();
    if (!devices.isEmpty())
        open(devices.first());
}

void Controller::select(int index)
{
    const auto &list = monitor_.devices();
    if (index < 0 || index >= list.size())
        return;
    if (current_ && *current_ == list[index] && state_ != State::Failed)
        return;
    close();
    open(list[index]);
}

void Controller::reload()
{
    if (!current_)
        return;
    hid::DeviceInfo info = *current_;
    close();
    open(info);
}

void Controller::close()
{
    ++loadSerial_;
    if (current_) {
        auto opts = monitor_.options();
        opts.busyPaths.clear();
        monitor_.setOptions(opts);
    }
    bool had = keyboard_.has_value();
    keyboard_.reset();
    themes_.reset();
    clockTimer_.stop();
    clockStatus_.clear();
    session_.reset();
    current_.reset();
    unlocked_ = true;
    setState(State::NoDevice);
    if (had)
        emit keyboardChanged();
}

void Controller::open(const hid::DeviceInfo &info)
{
    current_ = info;
    if (info.kind == hid::DeviceKind::Bootloader) {
        setState(State::Ready);
        emit keyboardChanged();
        return;
    }
    auto opts = monitor_.options();
    opts.busyPaths = {info.path};
    monitor_.setOptions(opts);

    session_ = std::make_unique<hid::Session>(info);
    progress_ = info.isDongle() ? tr("READING THE KEYBOARD OVER 2.4 GHZ") : tr("READING THE KEYBOARD");
    setState(State::Connecting);

    std::optional<QByteArray> override;
    if (info.kind == hid::DeviceKind::Sideload)
        override = sideloadJson_;
    else if (info.kind == hid::DeviceKind::ViaStack)
        override = QJsonDocument(viaStack_.value("definitions").toObject().value(QString::number(info.viaId())).toObject())
                       .toJson(QJsonDocument::Compact);

    const quint64 serial = ++loadSerial_;
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    session_->run(hid::Priority::Interactive, this, [override](hid::Link &link) { return loadRaw(link, override); },
                  [this, serial, timer](hid::Result<RawKeyboard> r) {
        if (serial != loadSerial_)
            return;
        if (!r.ok()) {
            setState(State::Failed, r.error);
            return;
        }
        auto state = KeyboardState::fromRaw(*r.value);
        if (auto *err = std::get_if<QString>(&state)) {
            setState(State::Failed, *err);
            return;
        }
        keyboard_ = std::move(std::get<KeyboardState>(state));
        qCInfo(lcController) << "loaded" << keyboard_->def.name << "in" << timer->elapsed() << "ms";
        setState(State::Ready);
        emit keyboardChanged();
        refreshLock();
        if (keyboard_->armed && keyboard_->armed->has(armed::F_CLOCK)) {
            syncClock();
            clockTimer_.start();
        }
    });
}

void Controller::refreshLock()
{
    if (!session_ || !keyboard_)
        return;
    const qint64 proto = keyboard_->vialProtocol;
    const quint64 serial = loadSerial_;
    session_->run(hid::Priority::Poll, this, [proto](hid::Link &link) {
        return vial::unlockStatus(link, int(proto), 1).unlocked;
    }, [this, serial](hid::Result<bool> r) {
        if (serial != loadSerial_ || !r.ok())
            return;
        if (unlocked_ != *r.value) {
            unlocked_ = *r.value;
            emit lockChanged();
        }
    }, QStringLiteral("unlock-status"));
}

void Controller::lockKeyboard()
{
    if (!session_ || !keyboard_ || keyboard_->vialProtocol < 0)
        return;
    session_->run(hid::Priority::Interactive, this, [](hid::Link &link) { vial::lock(link); },
                  [this](hid::Result<hid::Unit>) { refreshLock(); });
}

void Controller::setViaStack(const QByteArray &json)
{
    viaStack_ = QJsonDocument::fromJson(json).object();
    auto opts = monitor_.options();
    opts.viaStackIds.clear();
    const QJsonObject defs = viaStack_.value("definitions").toObject();
    for (auto it = defs.begin(); it != defs.end(); ++it)
        opts.viaStackIds.insert(it.key().toUInt());
    monitor_.setOptions(opts);
}

bool Controller::sideload(const QByteArray &json, QString *error)
{
    QJsonObject o = QJsonDocument::fromJson(json).object();
    bool okV = false, okP = false;
    int vid = o.value("vendorId").toString().toInt(&okV, 16);
    int pid = o.value("productId").toString().toInt(&okP, 16);
    if (!okV || !okP) {
        *error = tr("The file has no vendorId / productId");
        return false;
    }
    sideloadJson_ = json;
    auto opts = monitor_.options();
    opts.sideloadVid = vid;
    opts.sideloadPid = pid;
    monitor_.setOptions(opts);
    return true;
}

void Controller::setBusy(bool busy)
{
    if (busy_ == busy)
        return;
    busy_ = busy;
    monitor_.setPaused(busy);
    emit busyChanged(busy);
}

void Controller::write(bool needsUnlock, std::function<void(hid::Link &)> job, std::function<void()> revert,
                       std::function<void()> done, const QString &coalesce)
{
    if (!session_)
        return;
    const quint64 serial = loadSerial_;
    auto send = [this, serial, job = std::move(job), revert, done, coalesce] {
        if (serial != loadSerial_ || !session_)
            return;
        session_->run(hid::Priority::Interactive, this, job, [this, serial, revert, done](hid::Result<hid::Unit> r) {
            if (serial != loadSerial_)
                return;
            if (!r.ok()) {
                if (revert) {
                    revert();
                    emit keymapChanged();
                }
                toast(tr("The keyboard did not take the change: %1").arg(r.error), ToastKind::Error);
            } else if (done) {
                done();
            }
        }, coalesce);
    };
    if (!needsUnlock) {
        send();
        return;
    }
    Unlocker::ensure(this, dialogParent_, [this, send, revert](bool ok) {
        if (ok) {
            send();
        } else if (revert) {
            revert();
            emit keymapChanged();
        }
    });
}

void Controller::setKey(int layer, int row, int col, const QString &code)
{
    if (!keyboard_)
        return;
    const int i = keyboard_->keyIndex(layer, row, col);
    const QString old = keyboard_->keymap.value(i);
    if (old == code || old.isNull())
        return;
    keyboard_->keymap[i] = code;
    emit keymapChanged();
    const quint16 v = quint16(keycodes::Registry::instance().deserialize(code));
    write(code == QLatin1String(keycodes::kResetKeycode),
          [=](hid::Link &link) { vial::setKeycode(link, layer, row, col, v); },
          [this, i, old] {
              if (keyboard_)
                  keyboard_->keymap[i] = old;
          });
}

void Controller::setEncoder(int layer, int index, int direction, const QString &code)
{
    if (!keyboard_)
        return;
    const int i = keyboard_->encoderIndex(layer, index, direction);
    const QString old = keyboard_->encoders.value(i);
    if (old == code || old.isNull())
        return;
    keyboard_->encoders[i] = code;
    emit keymapChanged();
    const quint16 v = quint16(keycodes::Registry::instance().deserialize(code));
    write(code == QLatin1String(keycodes::kResetKeycode),
          [=](hid::Link &link) { vial::setEncoder(link, layer, index, direction, v); },
          [this, i, old] {
              if (keyboard_)
                  keyboard_->encoders[i] = old;
          });
}

void Controller::setLayoutOptions(quint32 options)
{
    if (!keyboard_ || keyboard_->layoutOptions < 0 || keyboard_->layoutOptions == qint64(options))
        return;
    const qint64 old = keyboard_->layoutOptions;
    keyboard_->layoutOptions = options;
    emit keymapChanged();
    write(false, [options](hid::Link &link) { vial::setLayoutOptions(link, options); },
          [this, old] {
              if (keyboard_)
                  keyboard_->layoutOptions = old;
          });
}

void Controller::restoreLayout(const QByteArray &vil, std::function<void(bool, const QString &)> done)
{
    KeyboardState *kb = keyboard();
    if (!kb || !session_) {
        done(false, tr("No keyboard"));
        return;
    }
    LayoutRestore plan;
    try {
        plan = planRestore(*kb, vil);
    } catch (const std::exception &e) {
        done(false, tr("Not a layout file: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    if (plan.empty()) {
        done(true, tr("The keyboard already has this layout"));
        return;
    }
    // codes are resolved here, on the GUI thread, where the keycode registry lives
    const int macroMemory = kb->macroInfo.memory;
    auto code = [](const QString &n) { return quint16(keycodes::Registry::instance().deserialize(n)); };
    QList<std::function<void(hid::Link &)>> writes;
    for (const auto &k : plan.keys)
        writes.append([l = k.layer, r = k.row, c = k.col, v = code(k.code)](hid::Link &link) { vial::setKeycode(link, l, r, c, v); });
    for (const auto &e : plan.encoders)
        writes.append([l = e.layer, i = e.index, d = e.direction, v = code(e.code)](hid::Link &link) { vial::setEncoder(link, l, i, d, v); });
    if (plan.layoutOptions)
        writes.append([o = *plan.layoutOptions](hid::Link &link) { vial::setLayoutOptions(link, o); });
    if (plan.macros)
        writes.append([m = *plan.macros, macroMemory](hid::Link &link) { vial::setMacroBuffer(link, m, macroMemory); });
    for (const auto &[i, e] : plan.tapDances)
        writes.append([i, d = e.pack()](hid::Link &link) { vial::setDynamicEntry(link, vial::DYNAMIC_VIAL_TAP_DANCE_SET, i, d, 20); });
    for (const auto &[i, e] : plan.combos)
        writes.append([i, d = e.pack()](hid::Link &link) { vial::setDynamicEntry(link, vial::DYNAMIC_VIAL_COMBO_SET, i, d, 20); });
    for (const auto &[i, e] : plan.keyOverrides)
        writes.append([i, d = e.pack()](hid::Link &link) { vial::setDynamicEntry(link, vial::DYNAMIC_VIAL_KEY_OVERRIDE_SET, i, d, 1); });
    for (const auto &[i, e] : plan.altRepeatKeys)
        writes.append([i, d = e.pack()](hid::Link &link) { vial::setDynamicEntry(link, vial::DYNAMIC_VIAL_ALT_REPEAT_KEY_SET, i, d, 1); });
    for (const auto &[q, v] : plan.settings)
        writes.append([q, b = qmksettings::Catalog::instance().serialize(q, v)](hid::Link &link) { vial::qmkSettingSet(link, q, b); });

    const int n = int(writes.size());
    const KeyboardState before = *kb;
    applyRestore(*kb, plan);
    write(plan.touchesReset(), [writes](hid::Link &link) {
        for (const auto &w : writes)
            w(link);
    }, [this, before] {
        if (keyboard_)
            *keyboard_ = before;
        emit keyboardChanged();
    }, [this, n, done] {
        emit keyboardChanged();
        done(true, tr("Layout loaded: %1 changes").arg(n));
    });
}

int Controller::armedValue(quint8 vid) const
{
    return keyboard_ && keyboard_->armed ? keyboard_->armed->values.value(vid, 0) : 0;
}

void Controller::armedSet(quint8 vid, int value)
{
    if (!keyboard_ || !keyboard_->armed)
        return;
    keyboard_->armed->values[vid] = quint8(value);
    write(false, [vid, value](hid::Link &link) { armed::set(link, vid, value); }, {}, {},
          QStringLiteral("armed-v%1").arg(vid));
}

void Controller::armedSave()
{
    write(false, [](hid::Link &link) { armed::save(link); }, {}, {}, QStringLiteral("armed-save"));
}

void Controller::armedReload(std::function<void()> done)
{
    if (!session_ || !keyboard_ || !keyboard_->armed)
        return;
    const quint16 features = keyboard_->armed->features;
    const quint64 serial = loadSerial_;
    session_->run(hid::Priority::Interactive, this, [features](hid::Link &link) { return armed::readValues(link, features); },
                  [this, serial, done](hid::Result<QMap<int, quint8>> r) {
        if (serial != loadSerial_ || !keyboard_ || !keyboard_->armed)
            return;
        if (r.ok()) {
            keyboard_->armed->values = *r.value;
            emit armedValuesChanged();
        }
        if (done)
            done();
    }, QStringLiteral("armed-reload"));
}

void Controller::loadThemes(std::function<void(bool)> done)
{
    if (themes_) {
        done(true);
        return;
    }
    if (!session_ || !keyboard_ || !keyboard_->armed || !keyboard_->armed->has(armed::F_THEMES)) {
        done(false);
        return;
    }
    const quint64 serial = loadSerial_;
    session_->run(hid::Priority::Interactive, this, [](hid::Link &link) {
        std::pair<std::optional<armed::ThemeInfo>, QList<QByteArray>> out;
        out.first = armed::themeInfo(link);
        if (out.first)
            for (int s = 0; s < out.first->slotCount; ++s)
                out.second.append(armed::themeRead(link, s, out.first->size));
        return out;
    }, [this, serial, done](hid::Result<std::pair<std::optional<armed::ThemeInfo>, QList<QByteArray>>> r) {
        if (serial != loadSerial_)
            return;
        if (!r.ok() || !r.value->first) {
            done(false);
            return;
        }
        Themes t{*r.value->first, {}};
        for (const QByteArray &blob : r.value->second)
            t.list.append(theme::Theme::unpack(blob, t.info.leds, t.info.layers));
        themes_ = t;
        done(true);
    });
}

void Controller::syncClock()
{
    if (!session_ || busy_)
        return;
    // the keyboard counts whole seconds: send the time right after a second ticks over
    const int wait = 1000 - QTime::currentTime().msec();
    QTimer::singleShot(wait, this, [this, serial = loadSerial_] {
        if (serial != loadSerial_ || !session_)
            return;
        session_->run(hid::Priority::Interactive, this, [](hid::Link &link) {
            auto before = armed::clockGet(link);
            const QDateTime now = QDateTime::currentDateTime();
            armed::clockSet(link, now);
            return std::pair{before, now};
        }, [this, serial](hid::Result<std::pair<std::optional<QDateTime>, QDateTime>> r) {
            if (serial != loadSerial_)
                return;
            if (!r.ok()) {
                clockStatus_ = tr("NOT SYNCED");
            } else {
                const auto &[was, now] = *r.value;
                QDateTime nowLocal(now.date(), QTime(now.time().hour(), now.time().minute(), now.time().second()));
                clockStatus_ = tr("SYNCED %1").arg(now.toString("HH:mm:ss"));
                if (was)
                    clockStatus_ += tr("  ·  WAS %1%2 S OFF")
                                        .arg(was->secsTo(nowLocal) <= 0 ? "+" : "-")
                                        .arg(std::abs(was->secsTo(nowLocal)));
            }
            emit clockSynced();
        }, QStringLiteral("clock"));
    });
}
