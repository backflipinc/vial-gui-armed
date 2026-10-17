#include "fakekeyboard.h"
#include "hid/session.h"

#include <QTest>
#include <QThread>

#include <future>

using hid::Priority;
using hid::Session;

namespace {

Session::OpenFn fakeOpen()
{
    return [] {
        auto kb = std::make_unique<FakeKeyboard>();
        kb->handler = [](const QByteArray &req) { return req; };
        return std::unique_ptr<hid::RawHid>(std::move(kb));
    };
}

}

class TestSession : public QObject {
    Q_OBJECT

private slots:
    void resultOnGuiThread()
    {
        Session s(hid::DeviceInfo{}, fakeOpen());
        QThread *gui = QThread::currentThread();
        QThread *worker = nullptr;
        std::optional<QByteArray> got;
        s.run(Priority::Interactive, this, [&](hid::Link &link) {
            worker = QThread::currentThread();
            return link.transact(QByteArray("\x05", 1));
        }, [&](hid::Result<QByteArray> r) {
            QCOMPARE(QThread::currentThread(), gui);
            got = r.value;
        });
        QTRY_VERIFY(got.has_value());
        QVERIFY(worker != gui);
        QCOMPARE(got->left(1), QByteArray("\x05", 1));
    }

    void errorsAreReported()
    {
        Session s(hid::DeviceInfo{}, fakeOpen());
        QString error;
        s.run(Priority::Interactive, this, [](hid::Link &) { throw std::runtime_error("boom"); },
              [&](hid::Result<hid::Unit> r) { error = r.error; });
        QTRY_COMPARE(error, QStringLiteral("boom"));
    }

    void openFailure()
    {
        Session s(hid::DeviceInfo{}, [] { return std::unique_ptr<hid::RawHid>(); });
        QString error;
        s.run(Priority::Interactive, this, [](hid::Link &) {}, [&](hid::Result<hid::Unit> r) { error = r.error; });
        QTRY_VERIFY(!error.isEmpty());
    }

    void priorityAndCoalescing()
    {
        Session s(hid::DeviceInfo{}, fakeOpen());
        std::promise<void> gate;
        auto opened = gate.get_future().share();
        QStringList order;
        auto record = [&](const QString &name) {
            return [&order, name](hid::Link &) { order.append(name); };
        };
        auto none = [](hid::Result<hid::Unit>) {};
        s.run(Priority::Interactive, this, [opened](hid::Link &) { opened.wait(); }, none);
        QTRY_COMPARE(s.queued(), 0); // the blocker is running

        s.run(Priority::Background, this, record("bg"), none);
        s.run(Priority::Poll, this, record("poll1"), none, "poll");
        s.run(Priority::Interactive, this, record("ui"), none);
        s.run(Priority::Poll, this, record("poll2"), none, "poll");
        s.run(Priority::Interactive, this, record("set3"), none, "slider");
        s.run(Priority::Interactive, this, record("set4"), none, "slider");
        QCOMPARE(s.queued(), 4);
        gate.set_value();
        QTRY_COMPARE(order.size(), 4);
        QCOMPARE(order, (QStringList{"ui", "set4", "poll2", "bg"}));
    }

    void goneContextGetsNoCallback()
    {
        Session s(hid::DeviceInfo{}, fakeOpen());
        bool called = false;
        auto *ctx = new QObject;
        std::promise<void> gate;
        auto opened = gate.get_future().share();
        s.run(Priority::Interactive, ctx, [opened](hid::Link &) { opened.wait(); },
              [&](hid::Result<hid::Unit>) { called = true; });
        delete ctx;
        gate.set_value();
        bool after = false;
        s.run(Priority::Interactive, this, [](hid::Link &) {}, [&](hid::Result<hid::Unit>) { after = true; });
        QTRY_VERIFY(after);
        QVERIFY(!called);
    }

    void dropQueued()
    {
        Session s(hid::DeviceInfo{}, fakeOpen());
        std::promise<void> gate;
        auto opened = gate.get_future().share();
        auto none = [](hid::Result<hid::Unit>) {};
        s.run(Priority::Interactive, this, [opened](hid::Link &) { opened.wait(); }, none);
        QTRY_COMPARE(s.queued(), 0);
        s.run(Priority::Poll, this, [](hid::Link &) {}, none);
        s.run(Priority::Background, this, [](hid::Link &) {}, none);
        s.run(Priority::Interactive, this, [](hid::Link &) {}, none);
        s.dropQueued(Priority::Poll);
        QCOMPARE(s.queued(), 1);
        gate.set_value();
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
