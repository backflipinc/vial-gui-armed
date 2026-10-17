#include "hid/link.h"

#include <QThread>

namespace hid {

Link::Link(std::unique_ptr<RawHid> dev, bool dongle)
    : sleep([](int ms) { QThread::msleep(ulong(ms)); })
    , dev_(std::move(dev))
    , dongle_(dongle)
{
}

QByteArray Link::transact(QByteArray msg, int retries)
{
    if (msg.size() > kMsgLen)
        throw LinkError("message must be at most 32 bytes");
    msg.append(kMsgLen - msg.size(), '\0');

    RetryPolicy p = policy(retries);
    int timeout = p.timeoutMs;
    for (int attempt = 0; attempt < p.attempts; ++attempt) {
        if (attempt > 0) {
            if (p.retryDelayMs)
                sleep(p.retryDelayMs);
            timeout = std::min(timeout * 2, p.maxTimeoutMs);
        }
        // a reply that missed the previous timeout (wireless link) would answer this request
        while (!dev_->read(1).isEmpty()) {
        }
        if (dev_->write(msg) != kMsgLen + 1)
            continue;
        QByteArray reply = dev_->read(timeout);
        if (!reply.isEmpty())
            return reply;
    }
    throw LinkError("failed to communicate with the device");
}

void Link::write(QByteArray msg)
{
    msg.append(kMsgLen - msg.size(), '\0');
    if (dev_->write(msg) != kMsgLen + 1)
        throw LinkError("write failed");
}

QByteArray Link::read(int timeoutMs)
{
    return dev_->read(timeoutMs);
}

}
