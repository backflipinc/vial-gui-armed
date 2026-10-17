#include "core/bytes.h"
#include "core/paths.h"
#include "fakekeyboard.h"
#include "protocol/armed.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace bytes;

namespace {

QByteArray named(std::initializer_list<int> head, const char *name)
{
    return of(head) + QByteArray(name) + '\0';
}

struct ArmedKb {
    QList<QByteArray> effects{named({1, 0}, "SOLID"), named({2, 1}, "TWO TONE"), named({3, 2}, "THEME")};
    QList<QByteArray> colors{named({0, 255}, "RED"), named({0, 1}, "BLACK")};
    QList<QByteArray> snaptap{named({}, "OFF"), named({}, "LAST")};
    quint16 features = armed::F_RGB_MATRIX | armed::F_COLOR2 | armed::F_SNAPTAP | armed::F_SCREEN;
    QByteArray screen;
    int listRequests = 0;

    QByteArray handle(const QByteArray &q)
    {
        QByteArray r(32, '\0');
        r[0] = char(armed::CMD_ARMED);
        r[1] = q[1];
        switch (u8(q, 1)) {
        case armed::SUB_INFO:
            r.replace(2, 4, "ARMD");
            r[6] = armed::PROTOCOL;
            r[7] = 1; r[8] = 8; r[9] = 0;
            r[10] = char(features & 0xFF);
            r[11] = char(features >> 8);
            r[12 + armed::L_EFFECTS] = char(effects.size());
            r[12 + armed::L_COLORS] = char(colors.size());
            r[12 + armed::L_COUNT] = 0; // max brightness unset -> 255
            break;
        case armed::SUB_LIST: {
            ++listRequests;
            QList<QByteArray> l;
            if (u8(q, 2) == armed::L_EFFECTS)
                l = effects;
            else if (u8(q, 2) == armed::L_COLORS)
                l = colors;
            else if (u8(q, 2) == armed::L_SNAPTAP_MODES)
                l = snaptap;
            int i = u8(q, 3);
            if (i >= l.size())
                return QByteArray(32, '\xFF');
            r.replace(4, l[i].size(), l[i]);
            break;
        }
        case armed::SUB_GET:
            r[2] = q[2];
            r[3] = char(u8(q, 2) + 100);
            break;
        case armed::SUB_SCREEN_READ: {
            quint32 off = le32(q, 2);
            r.replace(2, 4, q.mid(2, 4));
            r.replace(7, u8(q, 6), screen.mid(int(off), u8(q, 6)));
            break;
        }
        }
        return r;
    }
};

struct Rig {
    ArmedKb akb;
    FakeKeyboard *kb;
    std::unique_ptr<hid::Link> link;
    explicit Rig(bool dongle = false)
    {
        auto dev = std::make_unique<FakeKeyboard>();
        kb = dev.get();
        kb->handler = [this](const QByteArray &q) { return akb.handle(q); };
        link = std::make_unique<hid::Link>(std::move(dev), dongle);
    }
};

}

class TestArmed : public QObject {
    Q_OBJECT

    QTemporaryDir home;

private slots:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
        qputenv("APPDATA", home.path().toUtf8());
        qputenv("HOME", home.path().toUtf8()); // macOS: ~/Library/Application Support
    }

    void probeReadsListsAndValues()
    {
        Rig r;
        auto info = armed::probe(*r.link, 6, std::nullopt);
        QVERIFY(info);
        QCOMPARE(info->versionString(), QStringLiteral("1.8.0"));
        QCOMPARE(info->maxBrightness, 255);
        QCOMPARE(info->effects.size(), 3);
        QCOMPARE(info->effects[1].name, QStringLiteral("TWO TONE"));
        QVERIFY(info->effects[1].usesColor2());
        QVERIFY(info->effects[2].theme());
        QVERIFY(info->colors[1].black());
        QCOMPARE(info->snaptapModes, (QStringList{"OFF", "LAST"}));
        QVERIFY(info->accents.isEmpty());
        QCOMPARE(info->values.value(armed::V_HUE), quint8(armed::V_HUE + 100));
        QVERIFY(info->values.contains(armed::V_COLOR2));
        QVERIFY(!info->values.contains(armed::V_SLEEP));
    }

    void notArmedForVia()
    {
        Rig r;
        QVERIFY(!armed::probe(*r.link, -1, std::nullopt));
        QVERIFY(r.kb->requests.isEmpty());
    }

    void listsComeFromTheCache()
    {
        Rig a;
        QVERIFY(armed::probe(*a.link, 6, Q_UINT64_C(0x1122334455667788)));
        int first = a.akb.listRequests;
        QVERIFY(first > 0);
        const QString cache = paths::configDir() + "/cache/1122334455667788-1.8.0-423.json";
        QVERIFY2(QFile::exists(cache),
                 qPrintable(cache + " missing; there: "
                            + QDir(paths::configDir() + "/cache").entryList(QDir::Files).join(", ")));

        Rig b;
        auto info = armed::probe(*b.link, 6, Q_UINT64_C(0x1122334455667788));
        QCOMPARE(b.akb.listRequests, 0);
        QCOMPARE(info->effects.size(), 3);
        QCOMPARE(info->snaptapModes.size(), 2);
    }

    void screenReadIsPipelined()
    {
        Rig r;
        for (int i = 0; i < 1000; ++i)
            r.akb.screen.append(char(i * 7));
        int maxInFlight = 0;
        qint64 last = -1;
        QByteArray out = armed::screenRead(*r.link, 100, 600, [&](qint64 done, qint64 total) {
            QCOMPARE(total, 600);
            QVERIFY(done > last);
            last = done;
            maxInFlight = std::max(maxInFlight, r.kb->inFlight());
        });
        QCOMPARE(out, r.akb.screen.mid(100, 600));
        QCOMPARE(last, 600);
        QVERIFY(maxInFlight <= armed::SCREEN_WINDOW);
        QVERIFY(maxInFlight >= armed::SCREEN_WINDOW - 1);
    }

    void cancelledReadDrainsReplies()
    {
        Rig r;
        r.akb.screen = QByteArray(1000, 'x');
        int calls = 0;
        QVERIFY_THROWS_EXCEPTION(armed::Error, armed::screenRead(*r.link, 0, 1000, {}, [&] { return ++calls == 3; }));
        QCOMPARE(r.kb->inFlight(), 0);
    }

    // byte-exact against the Python app's hud_push_packets / hud_text_packets
    void pushPackets()
    {
        auto p = armed::hudPushPackets({{0, 1, 1234}, {1, 0, -5}, {2, 2, 3000000000LL}, {3, 0, 7}, {4, 1, -3000000000LL}}, 300);
        QCOMPARE(p.size(), 2);
        QCOMPARE(p[0].toHex(), QByteArray("ad1bff040001d20400000100fbffffff0202ffffff7f030007000000"));
        QCOMPARE(p[1].toHex(), QByteArray("ad1bff01040100000080"));
    }

    void textPackets()
    {
        auto p = armed::hudTextPackets(2, QStringLiteral("Hello, world! Привет 1234567890 abcdefghijklmnop"), 20, 40);
        QCOMPARE(p.size(), 2);
        QCOMPARE(p[0].toHex(), QByteArray("ad1c021400190048656c6c6f2c20776f726c6421203f3f3f3f3f3f2031323334"));
        QCOMPARE(p[1].toHex(), QByteArray("ad1c0214190e013536373839302061626364656667"));
        auto empty = armed::hudTextPackets(0, QString(), 5, 40);
        QCOMPARE(empty.size(), 1);
        QCOMPARE(empty[0].toHex(), QByteArray("ad1c0005000001"));
    }
};

QTEST_GUILESS_MAIN(TestArmed)
#include "tst_armed.moc"
