#include "core/lzma.h"
#include "keycodes/keycodes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using keycodes::Registry;

// Byte-for-byte parity with the Python app's Keycode (tools/gen_keycode_golden.py)
class TestKeycodes : public QObject {
    Q_OBJECT

    QJsonObject golden;

    Registry &forProtocol(int proto)
    {
        keycodes::KeyboardTraits kb;
        kb.protocol = proto;
        kb.layers = 4;
        kb.macroCount = 16;
        kb.tapDanceCount = 8;
        kb.midi = "advanced";
        kb.features = {"caps_word", "persistent_default_layer"};
        Registry::instance().rebuild(kb);
        return Registry::instance();
    }

private slots:
    void initTestCase()
    {
        QFile f(QStringLiteral(VIAL_ARMED_TEST_DATA "/keycodes-golden.json.xz"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto json = lzma::decompress(f.readAll());
        QVERIFY(json);
        golden = QJsonDocument::fromJson(*json).object();
        QVERIFY(golden.contains("6"));
    }

    void parity_data()
    {
        QTest::addColumn<int>("proto");
        QTest::newRow("v5") << 5;
        QTest::newRow("v6") << 6;
    }

    void parity()
    {
        QFETCH(int, proto);
        Registry &r = forProtocol(proto);
        QJsonObject g = golden.value(QString::number(proto)).toObject();

        QJsonArray ser = g.value("serialize").toArray();
        QCOMPARE(ser.size(), 0x10000);
        int mismatches = 0;
        for (int code = 0; code < 0x10000; ++code) {
            QString want = ser[code].toString();
            QString got = r.serialize(code);
            if (got != want && ++mismatches <= 10)
                qWarning() << "serialize" << Qt::hex << code << "got" << got << "want" << want;
        }
        QCOMPARE(mismatches, 0);

        auto check = [&](const char *key, auto fn) {
            QJsonObject m = g.value(key).toObject();
            int bad = 0;
            for (auto it = m.begin(); it != m.end(); ++it) {
                QJsonValue got = fn(it.key());
                if (got != it.value() && ++bad <= 10)
                    qWarning() << key << it.key() << "got" << got << "want" << it.value();
            }
            QCOMPARE(bad, 0);
        };
        check("deserialize", [&](const QString &e) { return QJsonValue(r.deserialize(e)); });
        check("label", [&](const QString &e) { return QJsonValue(r.label(e)); });
        check("tooltip", [&](const QString &e) {
            QString t = r.tooltip(e);
            return t.isEmpty() ? QJsonValue() : QJsonValue(t);
        });
        check("normalize", [&](const QString &e) { return QJsonValue(r.normalize(e)); });
        check("is_mask", [&](const QString &e) { return QJsonValue(r.isMask(e)); });

        QStringList hidden;
        for (const auto &v : g.value("hidden").toArray())
            hidden.append(v.toString());
        QStringList got;
        for (keycodes::Group grp : {keycodes::Group::Quantum})
            for (const auto *k : r.group(grp))
                if (k->hidden)
                    got.append(k->qmkId);
        for (const auto *k : r.layers())
            if (k->hidden)
                got.append(k->qmkId);
        got.sort();
        QCOMPARE(got, hidden);
    }

    void recorderAlias()
    {
        Registry &r = forProtocol(6);
        QVERIFY(r.findByRecorderAlias("left ctrl"));
        QCOMPARE(r.findByRecorderAlias("left ctrl")->qmkId, QStringLiteral("KC_LCTRL"));
        QVERIFY(!r.findByRecorderAlias("nope"));
    }
};

QTEST_GUILESS_MAIN(TestKeycodes)
#include "tst_keycodes.moc"
