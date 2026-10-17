#pragma once

#include "hid/rawhid.h"

#include <QByteArray>

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
    void write(QByteArray msg);
    QByteArray read(int timeoutMs);

    bool isDongle() const { return dongle_; }
    RetryPolicy policy(int retries) const { return dongle_ ? RetryPolicy::dongle(retries) : RetryPolicy::usb(retries); }

    // tests: no real sleeping
    std::function<void(int ms)> sleep;

private:
    std::unique_ptr<RawHid> dev_;
    bool dongle_;
};

}
