#pragma once

#include "hid/monitor.h"
#include "hid/session.h"
#include "model/keyboard.h"

#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>

// The open keyboard, on the GUI thread: which device, its session, what was read from it
class Controller : public QObject {
    Q_OBJECT

public:
    enum class State { NoDevice, Connecting, Ready, Failed };
    enum class ToastKind { Info, Ok, Error };

    explicit Controller(QObject *parent = nullptr);
    ~Controller() override;

    const QList<hid::DeviceInfo> &devices() const { return monitor_.devices(); }
    std::optional<hid::DeviceInfo> current() const;
    State state() const { return state_; }
    QString error() const { return error_; }
    QString progressText() const { return progress_; }

    hid::Session *session() const { return session_.get(); }
    KeyboardState *keyboard() { return keyboard_ ? &*keyboard_ : nullptr; }
    const KeyboardState *keyboard() const { return keyboard_ ? &*keyboard_ : nullptr; }
    bool unlocked() const { return unlocked_; }

    // VIA keyboards: definitions from the VIA project, or one sideloaded file
    void setViaStack(const QByteArray &json);
    bool sideload(const QByteArray &json, QString *error);

    // exclusive work (flashing): the device list freezes and pages leave the keyboard alone
    void setBusy(bool busy);
    bool busy() const { return busy_; }

    void toast(const QString &text, ToastKind kind = ToastKind::Info) { emit toastRequested(text, kind); }

public slots:
    void select(int index);
    void reload();
    void refreshDevices() { monitor_.refresh(); }
    void refreshLock();
    void lockKeyboard();

signals:
    void devicesChanged();
    void stateChanged();
    void keyboardChanged(); // a keyboard finished loading, or went away
    void lockChanged();
    void busyChanged(bool busy);
    void toastRequested(const QString &text, Controller::ToastKind kind);

private:
    void onDevices(const QList<hid::DeviceInfo> &devices);
    void open(const hid::DeviceInfo &info);
    void close();
    void setState(State s, const QString &error = {});

    hid::DeviceMonitor monitor_;
    std::unique_ptr<hid::Session> session_;
    std::optional<hid::DeviceInfo> current_;
    std::optional<KeyboardState> keyboard_;
    State state_ = State::NoDevice;
    QString error_, progress_;
    bool unlocked_ = true;
    bool busy_ = false;
    QJsonObject viaStack_;
    std::optional<QByteArray> sideloadJson_;
    quint64 loadSerial_ = 0;
};
