#include "hid/link.h"

#include <QThread>

namespace hid {

Link::Link(std::unique_ptr<RawHid> dev, bool dongle)
    : sleep([](int ms) { QThread::msleep(ulong(ms)); })
    , dev_(std::move(dev))
    , dongle_(dongle)
{
}

void Link::checkCancel() const
{
    if (cancel && cancel->load())
        throw LinkError("cancelled");
}

QByteArray Link::transact(QByteArray msg, int retries)
{
    if (msg.size() > kMsgLen)
        throw LinkError("message must be at most 32 bytes");
    msg.append(kMsgLen - msg.size(), '\0');

    RetryPolicy p = policy(retries);
    int timeout = p.timeoutMs;
    for (int attempt = 0; attempt < p.attempts; ++attempt) {
        checkCancel();
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

QList<QByteArray> Link::transactMany(const QList<QByteArray> &msgs, int retries)
{
    QList<QByteArray> out;
    out.reserve(msgs.size());
    if (dongle_ || msgs.size() < 2) {
        for (const QByteArray &m : msgs)
            out.append(transact(m, retries));
        return out;
    }
    const int timeout = policy(retries).timeoutMs;
    while (!dev_->read(1).isEmpty()) {
    }
    qsizetype sent = 0;
    while (out.size() < msgs.size()) {
        checkCancel();
        bool lost = false;
        while (sent < msgs.size() && sent - out.size() < kWindow) {
            QByteArray m = msgs[sent];
            if (m.size() > kMsgLen)
                throw LinkError("message must be at most 32 bytes");
            m.append(kMsgLen - m.size(), '\0');
            if (dev_->write(m) != kMsgLen + 1) {
                lost = true;
                break;
            }
            ++sent;
        }
        QByteArray reply = lost ? QByteArray() : dev_->read(timeout);
        if (reply.isEmpty()) {
            // the replies still in flight would answer the wrong requests: wait them out, then go one by one
            for (qsizetype n = sent - out.size(); n > 0 && !dev_->read(timeout).isEmpty(); --n) {
            }
            for (qsizetype i = out.size(); i < msgs.size(); ++i)
                out.append(transact(msgs[i], retries));
            return out;
        }
        out.append(reply);
    }
    return out;
}

void Link::write(QByteArray msg)
{
    checkCancel();
    msg.append(kMsgLen - msg.size(), '\0');
    if (dev_->write(msg) != kMsgLen + 1)
        throw LinkError("write failed");
}

QByteArray Link::read(int timeoutMs)
{
    checkCancel();
    return dev_->read(timeoutMs);
}

}
