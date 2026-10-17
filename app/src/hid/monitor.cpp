#include "hid/monitor.h"

#include <QFutureWatcher>
#include <QSocketNotifier>
#include <QtConcurrent>

#ifdef VIAL_ARMED_UDEV
#include <libudev.h>
#endif

namespace hid {

DeviceMonitor::DeviceMonitor(QObject *parent)
    : QObject(parent)
{
    // a replug fires a burst of events
    debounce_.setSingleShot(true);
    debounce_.setInterval(300);
    connect(&debounce_, &QTimer::timeout, this, &DeviceMonitor::refresh);
    poll_.setInterval(2000);
    connect(&poll_, &QTimer::timeout, this, &DeviceMonitor::refresh);
    startWatching();
}

DeviceMonitor::~DeviceMonitor()
{
    cancelDiscovery();
#ifdef VIAL_ARMED_UDEV
    if (udevMonitor_)
        udev_monitor_unref(static_cast<udev_monitor *>(udevMonitor_));
    if (udev_)
        udev_unref(static_cast<udev *>(udev_));
#endif
}

void DeviceMonitor::startWatching()
{
#ifdef VIAL_ARMED_UDEV
    auto *u = udev_new();
    auto *mon = u ? udev_monitor_new_from_netlink(u, "udev") : nullptr;
    if (mon && udev_monitor_filter_add_match_subsystem_devtype(mon, "hidraw", nullptr) >= 0
        && udev_monitor_enable_receiving(mon) >= 0) {
        udev_ = u;
        udevMonitor_ = mon;
        auto *n = new QSocketNotifier(udev_monitor_get_fd(mon), QSocketNotifier::Read, this);
        connect(n, &QSocketNotifier::activated, this, [this, mon] {
            if (auto *dev = udev_monitor_receive_device(mon))
                udev_device_unref(dev);
            debounce_.start();
        });
        // a dongle whose keyboard wakes up later produces no event; recheck slowly
        poll_.setInterval(5000);
        poll_.start();
        return;
    }
    if (mon)
        udev_monitor_unref(mon);
    if (u)
        udev_unref(u);
#endif
    poll_.start();
}

void DeviceMonitor::setOptions(const DiscoveryOptions &opt)
{
    opt_ = opt;
    refresh();
}

void DeviceMonitor::setPaused(bool paused)
{
    paused_ = paused;
    ++generation_;
    if (!paused)
        refresh();
}

void DeviceMonitor::refresh()
{
    if (paused_)
        return;
    if (running_) {
        again_ = true;
        return;
    }
    running_ = true;
    quint64 gen = ++generation_;
    using Found = std::pair<QList<DeviceInfo>, bool>;
    auto *watcher = new QFutureWatcher<Found>(this);
    connect(watcher, &QFutureWatcher<Found>::finished, this, [this, watcher, gen] {
        watcher->deleteLater();
        running_ = false;
        auto [found, unprobed] = watcher->result();
        apply(found, gen);
        // wired keyboards show up at once; dongles follow when their probe answers
        if (unprobed && !paused_ && gen == generation_) {
            probeNext_ = true;
            again_ = true;
        }
        if (again_) {
            again_ = false;
            refresh();
        }
    });
    DiscoveryOptions opt = opt_;
    opt.probeDongles = probeNext_;
    probeNext_ = false;
    watcher->setFuture(QtConcurrent::run([opt] {
        bool unprobed = false;
        auto found = discover(opt, &unprobed);
        return Found{found, unprobed};
    }));
}

void DeviceMonitor::apply(const QList<DeviceInfo> &found, quint64 generation)
{
    if (generation != generation_ || paused_)
        return;
    if (found == devices_)
        return;
    devices_ = found;
    emit devicesChanged(devices_);
}

}
