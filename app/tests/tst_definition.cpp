#include "model/definition.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

// Same geometry and key roles as the Python app's kle_serial + Keyboard.reload_layout (tools/gen_layout_golden.py)
class TestDefinition : public QObject {
    Q_OBJECT

    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void parity_data()
    {
        QTest::addColumn<QString>("name");
        QDir dir(QStringLiteral(VIAL_ARMED_TEST_DATA "/layouts"));
        for (const QString &f : dir.entryList({"*.golden.json"}))
            QTest::newRow(qPrintable(f)) << f.chopped(int(strlen(".golden.json")));
    }

    void parity()
    {
        QFETCH(QString, name);
        const QString base = QStringLiteral(VIAL_ARMED_TEST_DATA "/layouts/") + name;
        QJsonObject json = QJsonDocument::fromJson(read(base + ".json")).object();
        QJsonArray golden = QJsonDocument::fromJson(read(base + ".golden.json")).array();
        QVERIFY(!golden.isEmpty());

        auto geo = kle::deserialize(json.value("layouts").toObject().value("keymap").toArray());
        QCOMPARE(geo.size(), golden.size());
        for (int i = 0; i < geo.size(); ++i) {
            const kle::Key &k = geo[i];
            QJsonObject g = golden[i].toObject();
            auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
            QVERIFY2(near(k.x, g["x"].toDouble()) && near(k.y, g["y"].toDouble()), qPrintable(QString::number(i)));
            QVERIFY(near(k.width, g["width"].toDouble()) && near(k.height, g["height"].toDouble()));
            QVERIFY(near(k.x2, g["x2"].toDouble()) && near(k.y2, g["y2"].toDouble()));
            QVERIFY(near(k.width2, g["width2"].toDouble()) && near(k.height2, g["height2"].toDouble()));
            QVERIFY(near(k.rotationX, g["rotation_x"].toDouble()) && near(k.rotationY, g["rotation_y"].toDouble()));
            QVERIFY(near(k.rotationAngle, g["rotation_angle"].toDouble()));
            QCOMPARE(k.decal, g["decal"].toBool());
            QCOMPARE(k.ghost, g["ghost"].toBool());
            QJsonArray labels = g["labels"].toArray();
            for (int l = 0; l < 12; ++l)
                QCOMPARE(k.labels[size_t(l)], labels[l].isNull() ? QString() : labels[l].toString());
        }

        auto parsed = Definition::parse(json);
        QVERIFY(std::holds_alternative<Definition>(parsed));
        const Definition &d = std::get<Definition>(parsed);
        int ki = 0, ei = 0;
        for (const auto &v : golden) {
            QJsonObject g = v.toObject();
            QString role = g["role"].toString();
            if (role == "key") {
                const LayoutKey &k = d.keys[ki++];
                QCOMPARE(k.row, g["matrix"].toArray()[0].toInt());
                QCOMPARE(k.col, g["matrix"].toArray()[1].toInt());
                QCOMPARE(k.layoutIndex, g["layout"].toArray()[0].toInt());
                QCOMPARE(k.layoutOption, g["layout"].toArray()[1].toInt());
            } else if (role == "encoder") {
                const LayoutKey &k = d.encoders[ei++];
                QCOMPARE(k.encoder, g["encoder"].toArray()[0].toInt());
                QCOMPARE(k.direction, g["encoder"].toArray()[1].toInt());
            }
        }
        QCOMPARE(ki, d.keys.size());
        QCOMPARE(ei, d.encoders.size());
    }

    void rejectsKeysOutsideTheMatrix()
    {
        auto r = Definition::parse(QByteArray(R"({"matrix":{"rows":1,"cols":1},"layouts":{"keymap":[["0,0","0,1"]]}})"));
        QVERIFY(std::holds_alternative<QString>(r));
    }

    void rejectsLateRotation()
    {
        auto r = Definition::parse(QByteArray(R"({"matrix":{"rows":1,"cols":2},"layouts":{"keymap":[["0,0",{"r":5},"0,1"]]}})"));
        QVERIFY(std::holds_alternative<QString>(r));
    }
};

QTEST_GUILESS_MAIN(TestDefinition)
#include "tst_definition.moc"
