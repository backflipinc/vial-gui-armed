#pragma once

#include "hid/rawhid.h"

#include <QByteArray>
#include <QList>

#include <deque>
#include <functional>

// A keyboard on virtual time: replies become readable `latencyMs` after the request; reads advance the clock
class FakeKeyboard : public hid::RawHid {
public:
    std::function<QByteArray(const QByteArray &request)> handler;
    int latencyMs = 1;
    int dropNext = 0;          // swallow this many requests without a reply
    QList<int> lateMs;         // per request, a latency that overrides latencyMs (consumed in order)

    qint64 now = 0;
    QList<QByteArray> requests;
    QList<int> readTimeouts;   // every read with a timeout > 1 ms

    int write(const QByteArray &report) override
    {
        requests.append(report);
        if (dropNext > 0) {
            --dropNext;
            return report.size() + 1;
        }
        int latency = lateMs.isEmpty() ? latencyMs : lateMs.takeFirst();
        if (handler)
            pending_.push_back({now + latency, handler(report)});
        return report.size() + 1;
    }

    QByteArray read(int timeoutMs) override
    {
        if (timeoutMs > 1)
            readTimeouts.append(timeoutMs);
        if (!pending_.empty() && pending_.front().at <= now + timeoutMs) {
            now = std::max(now, pending_.front().at);
            QByteArray r = pending_.front().data;
            pending_.pop_front();
            return r;
        }
        now += timeoutMs;
        return {};
    }

    int inFlight() const { return int(pending_.size()); }

private:
    struct Reply {
        qint64 at;
        QByteArray data;
    };
    std::deque<Reply> pending_;
};
