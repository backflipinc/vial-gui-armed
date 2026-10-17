#include "hid/session.h"

#include <QThread>

namespace hid {

Session::Session(const DeviceInfo &info, QObject *parent)
    : Session(info, [path = info.path]() -> std::unique_ptr<RawHid> {
          for (int i = 0; i < 10; ++i) {
              if (auto dev = HidapiDevice::open(path))
                  return dev;
              QThread::msleep(1000);
          }
          return nullptr;
      }, parent)
{
}

Session::Session(const DeviceInfo &info, OpenFn open, QObject *parent)
    : QObject(parent)
    , info_(info)
    , open_(std::move(open))
{
    thread_ = std::thread([this] { loop(); });
}

Session::~Session()
{
    cancel_ = true;
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        for (auto &q : queues_)
            q.clear();
    }
    cv_.notify_all();
    thread_.join();
}

void Session::enqueue(Priority prio, const QString &key, std::function<void(Link *)> fn)
{
    {
        std::lock_guard lock(mutex_);
        auto &q = queues_[int(prio)];
        if (!key.isEmpty()) {
            for (Job &j : q) {
                if (j.key == key) {
                    j.fn = std::move(fn);
                    return;
                }
            }
        }
        q.push_back({prio, key, std::move(fn)});
    }
    cv_.notify_one();
}

void Session::dropQueued(Priority upTo)
{
    std::lock_guard lock(mutex_);
    for (int p = 0; p <= int(upTo); ++p)
        queues_[p].clear();
}

int Session::queued() const
{
    std::lock_guard lock(mutex_);
    return int(queues_[0].size() + queues_[1].size() + queues_[2].size());
}

void Session::loop()
{
    std::unique_ptr<Link> link;
    bool opened = false;
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return stop_ || !queues_[0].empty() || !queues_[1].empty() || !queues_[2].empty();
            });
            if (stop_)
                return;
            for (int p = 2; p >= 0; --p) {
                if (!queues_[p].empty()) {
                    job = std::move(queues_[p].front());
                    queues_[p].pop_front();
                    break;
                }
            }
        }
        if (!opened) {
            opened = true;
            if (auto dev = open_()) {
                link = std::make_unique<Link>(std::move(dev), info_.isDongle());
                link->cancel = &cancel_;
            }
        }
        job.fn(link.get());
    }
}

}
