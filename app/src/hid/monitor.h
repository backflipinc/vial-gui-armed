#pragma once

#include "hid/devices.h"

#include <QObject>
#include <QTimer>

namespace hid {

// Keeps the device list current: udev events on Linux, a slow poll elsewhere. Discovery runs off the GUI
// thread; a result is dropped if a newer discovery was started meanwhile.
class DeviceMonitor : public QObject {
    Q_OBJECT

public:
    explicit DeviceMonitor(QObject *parent = nullptr);
    ~DeviceMonitor() override;

    const QList<DeviceInfo> &devices() const { return devices_; }

    void setOptions(const DiscoveryOptions &opt);
    DiscoveryOptions options() const { return opt_; }
    // while paused (flashing), no discovery touches the devices
    void setPaused(bool paused);

public slots:
    void refresh();

signals:
    void devicesChanged(const QList<hid::DeviceInfo> &devices);

private:
    void startWatching();
    void apply(const QList<DeviceInfo> &found, quint64 generation);

    DiscoveryOptions opt_;
    QList<DeviceInfo> devices_;
    QTimer debounce_;
    QTimer poll_;
    quint64 generation_ = 0;
    bool paused_ = false;
    bool running_ = false;
    bool again_ = false;
    void *udev_ = nullptr;
    void *udevMonitor_ = nullptr;
};

}
