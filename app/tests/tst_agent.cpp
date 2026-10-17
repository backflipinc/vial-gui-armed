#include "agent/hudagent.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

// Same text cleanup and number parsing as the Python agent (data from its hud_agent module)
class TestAgent : public QObject {
    Q_OBJECT

private slots:
    void parity()
    {
        QFile f(QStringLiteral(VIAL_ARMED_TEST_DATA "/agent-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject g = QJsonDocument::fromJson(f.readAll()).object();
        for (const auto &v : g["ascii"].toArray())
            QCOMPARE(agent::toAscii(v.toArray()[0].toString()), v.toArray()[1].toString());
        for (const auto &v : g["num"].toArray()) {
            const auto got = agent::firstNumber(v.toArray()[0].toString());
            if (v.toArray()[1].isNull())
                QVERIFY(!got);
            else
                QCOMPARE(*got, v.toArray()[1].toDouble());
        }
    }

    void sampler()
    {
        agent::Sampler s;
        s.sample({"cpu", "ram", "disk"});
        QTest::qWait(150);
        const auto got = s.sample({"cpu", "ram", "disk", "load"});
#ifdef Q_OS_LINUX
        QVERIFY(got.value("cpu").has_value());
        QVERIFY(got.value("ram").has_value());
        QVERIFY(*got.value("ram") > 0 && *got.value("ram") <= 100);
#endif
        QVERIFY(got.value("disk").has_value());
    }

    void defaults()
    {
        const auto c = agent::Config::defaults();
        QCOMPARE(c.values.size(), agent::HOST_VALUES);
        QCOMPARE(c.texts.size(), agent::HOST_TEXTS);
        QCOMPARE(agent::slotName(c, 0), QStringLiteral("CPU LOAD"));
        QCOMPARE(agent::slotName(c, 12), QStringLiteral("HOST 13"));
    }
};

QTEST_GUILESS_MAIN(TestAgent)
#include "tst_agent.moc"
