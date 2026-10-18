#pragma once

#include "hid/monitor.h"
#include "hid/session.h"
#include "model/keyboard.h"
#include "model/theme.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

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

    const QList<hid::DeviceInfo> &devices() const { return fakeOpen_ ? fakeDevices_ : monitor_.devices(); }
    // tests: these devices instead of the system's, opened through `open`
    using FakeOpen = std::function<std::unique_ptr<hid::RawHid>(const hid::DeviceInfo &)>;
    void setFakeDevices(const QList<hid::DeviceInfo> &devices, FakeOpen open);
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

    // where the unlock dialog opens
    void setDialogParent(QWidget *w) { dialogParent_ = w; }
    QWidget *dialogParent() const { return dialogParent_; }

    // Writes to the keyboard. The model changes at once; a failed write puts it back and says why.
    // needsUnlock: the change involves QK_BOOT (or macros), Vial wants the keyboard unlocked first
    // coalesce: writes with the same key replace each other while queued (a spin box being dragged)
    void write(bool needsUnlock, std::function<void(hid::Link &)> job, std::function<void()> revert = {},
               std::function<void()> done = {}, const QString &coalesce = {});
    void setKey(int layer, int row, int col, const QString &code);
    void setEncoder(int layer, int index, int direction, const QString &code);
    void setLayoutOptions(quint32 options);
    // qmk-armed values: the model changes at once, drags send only their last value
    int armedValue(quint8 vid) const;
    void armedSet(quint8 vid, int value);
    void armedSave(); // HSV and speed are applied without EEPROM writes until this
    void armedReload(std::function<void()> done = {});

    // lighting themes: read once, shared by the Lighting and Theme editor pages
    struct Themes {
        armed::ThemeInfo info;
        QList<theme::Theme> list;
    };
    const std::optional<Themes> &themes() const { return themes_; }
    std::optional<Themes> &themes() { return themes_; }
    void loadThemes(std::function<void(bool ok)> done);

    // Parts of the keyboard read after the keymap: done(ok) once all of them are in. A connect fetches
    // BackgroundParts by itself; entries are read the first time something requires them.
    void require(int parts, std::function<void(bool ok)> done, hid::Priority priority = hid::Priority::Interactive);

    // the keyboard's clock follows this computer's: on connect and every ten minutes
    void syncClock();
    QString clockStatus() const { return clockStatus_; }

    // a .vil file onto the keyboard; done(ok, message)
    void restoreLayout(const QByteArray &vil, std::function<void(bool, const QString &)> done);

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
    void keymapChanged();   // keys, encoders or layout options changed (here or by a revert)
    void partsLoaded(int parts); // more of the keyboard was read (Parts)
    void lockChanged();
    void armedValuesChanged();
    void clockSynced();
    void busyChanged(bool busy);
    void toastRequested(const QString &text, Controller::ToastKind kind);

private:
    void onDevices(const QList<hid::DeviceInfo> &devices);
    void open(const hid::DeviceInfo &info);
    void close();
    void setState(State s, const QString &error = {});
    void fetch(int parts, hid::Priority priority);
    void partDone(int part, bool ok);

    hid::DeviceMonitor monitor_;
    std::unique_ptr<hid::Session> session_;
    std::optional<hid::DeviceInfo> current_;
    std::optional<KeyboardState> keyboard_;
    RawKeyboard core_; // what the connect read, to read the parts from
    int fetching_ = 0; // Parts being read; a failed one is read again when required again
    struct Waiter {
        int parts;
        std::function<void(bool)> done;
    };
    QList<Waiter> waiters_;
    State state_ = State::NoDevice;
    QString error_, progress_;
    bool unlocked_ = true;
    bool busy_ = false;
    QJsonObject viaStack_;
    std::optional<QByteArray> sideloadJson_;
    quint64 loadSerial_ = 0;
    QWidget *dialogParent_ = nullptr;
    std::optional<Themes> themes_;
    QTimer clockTimer_;
    QList<hid::DeviceInfo> fakeDevices_;
    FakeOpen fakeOpen_;
    QString clockStatus_;
};
