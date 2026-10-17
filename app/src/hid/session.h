#pragma once

#include "hid/devices.h"
#include "hid/link.h"

#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QString>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

namespace hid {

struct Unit {};

template <class T>
struct Result {
    std::optional<T> value;
    QString error;
    bool ok() const { return value.has_value(); }
};

enum class Priority { Background = 0, Poll = 1, Interactive = 2 };

// The open keyboard. All traffic runs on one worker thread in priority order, so the GUI never waits on
// the device; jobs with the same coalescing key replace each other while queued (a dragged slider sends
// only its last value).
class Session : public QObject {
    Q_OBJECT

public:
    using OpenFn = std::function<std::unique_ptr<RawHid>()>;

    explicit Session(const DeviceInfo &info, QObject *parent = nullptr);
    Session(const DeviceInfo &info, OpenFn open, QObject *parent = nullptr);
    ~Session() override;

    const DeviceInfo &info() const { return info_; }

    // work(Link&) runs on the worker thread; done(Result) on the GUI thread, unless ctx is gone by then
    template <class Work, class Done>
    void run(Priority prio, QObject *ctx, Work work, Done done, const QString &coalesceKey = {})
    {
        using R = std::invoke_result_t<Work, Link &>;
        using V = std::conditional_t<std::is_void_v<R>, Unit, R>;
        QPointer<QObject> guard(ctx);
        enqueue(prio, coalesceKey, [work = std::move(work), done = std::move(done), guard](Link *link) mutable {
            auto r = std::make_shared<Result<V>>();
            if (!link) {
                r->error = QStringLiteral("unable to open the device");
            } else {
                try {
                    if constexpr (std::is_void_v<R>) {
                        work(*link);
                        r->value = Unit{};
                    } else {
                        r->value = work(*link);
                    }
                } catch (const std::exception &e) {
                    r->error = QString::fromUtf8(e.what());
                }
            }
            QMetaObject::invokeMethod(qApp, [guard, done = std::move(done), r]() mutable {
                if (guard)
                    done(std::move(*r));
            }, Qt::QueuedConnection);
        });
    }

    // drops queued (not running) jobs at or below a priority, e.g. polls when a page closes
    void dropQueued(Priority upTo);
    int queued() const;

private:
    struct Job {
        Priority prio;
        QString key;
        std::function<void(Link *)> fn;
    };

    void enqueue(Priority prio, const QString &key, std::function<void(Link *)> fn);
    void loop();

    DeviceInfo info_;
    OpenFn open_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queues_[3];
    bool stop_ = false;
    std::thread thread_;
};

}
