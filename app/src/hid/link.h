#pragma once

#include "hid/rawhid.h"

#include <QByteArray>
#include <QList>

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>

namespace hid {

struct LinkError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct RetryPolicy {
    int attempts;
    int timeoutMs;
    int maxTimeoutMs; // each retry doubles the timeout up to this
    int retryDelayMs;

    // USB answers at once or not at all
    static RetryPolicy usb(int retries) { return {retries, 500, 500, 500}; }
    // the radio drops a reply now and then, so retry fast; but a busy keyboard answers late, and a fixed
    // short timeout would pile up repeats of a slow request (each HUD APPLY redraws the screen again)
    static RetryPolicy dongle(int retries) { return {std::max(retries, 5), 150, 600, 0}; }
};

// A request/reply channel to one keyboard. Not thread-safe: used only from the device's worker thread.
class Link {
public:
    Link(std::unique_ptr<RawHid> dev, bool dongle);

    QByteArray transact(QByteArray msg, int retries = 20);
    // several independent reads (a lost reply makes them go again), replies in order. Over USB up to kWindow are in flight at once (the
    // keyboard answers them in order); the radio tunnel takes one at a time. A lost reply falls back to
    // one by one with retries.
    QList<QByteArray> transactMany(const QList<QByteArray> &msgs, int retries = 20);
    static constexpr int kWindow = 8;
    void write(QByteArray msg);
    QByteArray read(int timeoutMs);

    bool isDongle() const { return dongle_; }
    RetryPolicy policy(int retries) const { return dongle_ ? RetryPolicy::dongle(retries) : RetryPolicy::usb(retries); }

    // tests: no real sleeping
    std::function<void(int ms)> sleep;
    // set from another thread to abandon the work in progress (closing the device, quitting)
    const std::atomic<bool> *cancel = nullptr;

private:
    void checkCancel() const;

    std::unique_ptr<RawHid> dev_;
    bool dongle_;
};

}
