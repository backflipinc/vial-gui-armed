#include "keycodes/keycodes.h"
#include "model/macro.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

// Same bytes and actions as the Python app's macro actions (tools/gen_macro_golden.py)
class TestMacro : public QObject {
    Q_OBJECT

    QJsonObject golden;

    void useProtocol(int proto)
    {
        keycodes::KeyboardTraits kb;
        kb.protocol = proto;
        kb.layers = 4;
        kb.macroCount = 16;
        kb.tapDanceCount = 8;
        keycodes::Registry::instance().rebuild(kb);
    }

private slots:
    void initTestCase()
    {
        QFile f(QStringLiteral(VIAL_ARMED_TEST_DATA "/macro-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        golden = QJsonDocument::fromJson(f.readAll()).object();
    }

    void parity_data()
    {
        QTest::addColumn<int>("proto");
        QTest::newRow("v1") << 1;
        QTest::newRow("v6") << 6;
    }

    void parity()
    {
        QFETCH(int, proto);
        useProtocol(proto);
        QJsonObject g = golden.value(QString::number(proto)).toObject();
        for (const auto &v : g.value("serialize").toArray()) {
            QJsonObject c = v.toObject();
            macro::Macro m = macro::restore(c["macro"].toArray());
            QCOMPARE(macro::serialize(m, proto).toHex(), c["hex"].toString().toLatin1());
            QCOMPARE(macro::save(m), c["macro"].toArray());
        }
        for (const auto &v : g.value("deserialize").toArray()) {
            QJsonObject c = v.toObject();
            macro::Macro m = macro::deserialize(QByteArray::fromHex(c["hex"].toString().toLatin1()), proto);
            QCOMPARE(macro::save(m), c["macro"].toArray());
        }
    }

    void buffers()
    {
        useProtocol(6);
        auto all = macro::deserializeAll(QByteArray("ab\0\0c", 5), 4, 6);
        QCOMPARE(all.size(), 4);
        QCOMPARE(all[0].first().text, QStringLiteral("ab"));
        QVERIFY(all[1].isEmpty());
        QCOMPARE(all[2].first().text, QStringLiteral("c"));
        QVERIFY(all[3].isEmpty());
        QCOMPARE(macro::serializeAll(all, 4, 6), QByteArray("ab\0\0c\0\0", 7));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, macro::serializeAll(all, 5, 6));
    }

    // test_macro.py: two-byte keycodes, and ones with a zero byte (sent as 0xFF)
    void twoByteKeycodes()
    {
        useProtocol(2);
        auto act = [](const char *kind, QStringList keys) {
            return macro::restore(QJsonArray{QJsonArray{QString(kind), keys[0], keys[1]}});
        };
        const QList<std::pair<const char *, QByteArray>> cases = {
            {"tap", QByteArray("\x01\x05\xF9\x5C\x01\x01\x04", 7)},
            {"down", QByteArray("\x01\x06\xF9\x5C\x01\x02\x04", 7)},
            {"up", QByteArray("\x01\x07\xF9\x5C\x01\x03\x04", 7)},
        };
        for (const auto &[kind, bytes] : cases) {
            const macro::Macro m = act(kind, {"CMB_TOG", "KC_A"});
            QCOMPARE(macro::serialize(m, 2), bytes);
            QCOMPARE(macro::save(macro::deserialize(bytes, 2)), macro::save(m));
        }

        auto &reg = keycodes::Registry::instance();
        const QString a = reg.serialize(0xA000), b = reg.serialize(0xB100), c = reg.serialize(0xC200);
        const macro::Macro m = macro::restore(QJsonArray{QJsonArray{"tap", a, b, c}});
        QCOMPARE(macro::serialize(m, 2), QByteArray("\x01\x05\xA0\xFF\x01\x05\xB1\xFF\x01\x05\xC2\xFF", 12));
        const macro::Macro back = macro::deserialize(QByteArray("\x01\x05\xC2\xFF\x01\x05\xB1\xFF\x01\x05\xA0\xFF", 12), 2);
        QCOMPARE(macro::save(back), (QJsonArray{QJsonArray{"tap", c, b, a}}));
    }

    void delayNeedsProtocol2()
    {
        useProtocol(1);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, macro::serialize({macro::Action{macro::Kind::Delay, {}, {}, 5}}, 1));
    }
};

QTEST_GUILESS_MAIN(TestMacro)
#include "tst_macro.moc"
