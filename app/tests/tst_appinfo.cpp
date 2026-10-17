#include "core/appinfo.h"

#include <QRegularExpression>
#include <QTest>

class TestAppInfo : public QObject {
    Q_OBJECT

private slots:
    void version()
    {
        static const QRegularExpression re(R"(^\d+\.\d+\.\d+(\+(\d+\.)?g[0-9a-f]{7,})?$)");
        QVERIFY2(re.match(appinfo::version()).hasMatch(), qPrintable(appinfo::version()));
    }

    void modes()
    {
        using appinfo::Mode;
        QCOMPARE(appinfo::modeFromArgs({"vial-armed"}), Mode::Window);
        QCOMPARE(appinfo::modeFromArgs({"vial-armed", "--agent"}), Mode::Agent);
        QCOMPARE(appinfo::modeFromArgs({"vial-armed", "--linux-recorder"}), Mode::LinuxRecorder);
        QCOMPARE(appinfo::modeFromArgs({"vial-armed", "--agent", "x"}), Mode::Window);
    }
};

QTEST_GUILESS_MAIN(TestAppInfo)
#include "tst_appinfo.moc"
