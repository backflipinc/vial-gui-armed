#include "fakekeyboard.h"
#include "hid/link.h"

#include <QTest>

using hid::Link;

namespace {

struct Rig {
    FakeKeyboard *kb;
    std::unique_ptr<Link> link;
    QList<int> sleeps;

    explicit Rig(bool dongle)
    {
        auto dev = std::make_unique<FakeKeyboard>();
        kb = dev.get();
        kb->handler = [](const QByteArray &req) { return req.left(4) + QByteArray(28, 'r'); };
        link = std::make_unique<Link>(std::move(dev), dongle);
        link->sleep = [this](int ms) { sleeps.append(ms); };
    }
};

}

class TestLink : public QObject {
    Q_OBJECT

private slots:
    void padsTo32()
    {
        Rig r(false);
        QByteArray reply = r.link->transact(QByteArray("\x01", 1));
        QCOMPARE(r.kb->requests.size(), 1);
        QCOMPARE(r.kb->requests[0].size(), 32);
        QCOMPARE(reply.left(1), QByteArray("\x01", 1));
    }

    void rejectsLongMessages()
    {
        Rig r(false);
        QVERIFY_THROWS_EXCEPTION(hid::LinkError, r.link->transact(QByteArray(33, 'x')));
    }

    void usbRetriesWithDelay()
    {
        Rig r(false);
        r.kb->dropNext = 2;
        r.link->transact(QByteArray("\x02", 1), 20);
        QCOMPARE(r.kb->requests.size(), 3);
        QCOMPARE(r.sleeps, (QList<int>{500, 500}));
        QCOMPARE(r.kb->readTimeouts, (QList<int>{500, 500, 500}));
    }

    void usbGivesUp()
    {
        Rig r(false);
        r.kb->dropNext = 100;
        QVERIFY_THROWS_EXCEPTION(hid::LinkError, r.link->transact(QByteArray("\x02", 1), 3));
        QCOMPARE(r.kb->requests.size(), 3);
    }

    // a slow reply is waited for longer each time instead of being asked again every 150 ms
    void dongleBacksOff()
    {
        Rig r(true);
        r.kb->dropNext = 4;
        r.link->transact(QByteArray("\x02", 1), 20);
        QCOMPARE(r.kb->readTimeouts, (QList<int>{150, 300, 600, 600, 600}));
        QVERIFY(r.sleeps.isEmpty());
    }

    void dongleAttemptsAtLeastFive()
    {
        Rig r(true);
        r.kb->dropNext = 100;
        QVERIFY_THROWS_EXCEPTION(hid::LinkError, r.link->transact(QByteArray("\x02", 1), 1));
        QCOMPARE(r.kb->requests.size(), 5);
    }

    // a retried request leaves a second reply in flight; it must not answer the next request
    void dongleFlushesLateReplies()
    {
        Rig r(true);
        r.kb->lateMs = {200};
        QCOMPARE(r.link->transact(QByteArray("\x02", 1)).left(1), QByteArray("\x02", 1));
        QCOMPARE(r.kb->inFlight(), 1);
        r.kb->now += 1000;
        QCOMPARE(r.link->transact(QByteArray("\x03", 1)).left(1), QByteArray("\x03", 1));
        QCOMPARE(r.kb->inFlight(), 0);
    }
};

QTEST_GUILESS_MAIN(TestLink)
#include "tst_link.moc"
