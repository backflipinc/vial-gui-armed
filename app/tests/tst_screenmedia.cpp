#include "core/lzma.h"
#include "model/screenmedia.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace screenmedia;

// Same upload stream as the Python app's screen_media.encode (tools/gen_screen_golden.py)
class TestScreenMedia : public QObject {
    Q_OBJECT

    QJsonObject golden;

private slots:
    void initTestCase()
    {
        QFile f(QStringLiteral(VIAL_ARMED_TEST_DATA "/screen-golden.json.xz"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        golden = QJsonDocument::fromJson(*lzma::decompress(f.readAll())).object();
    }

    void encoder()
    {
        QList<Frame> frames;
        for (const auto &v : golden["frames"].toArray()) {
            QJsonObject o = v.toObject();
            frames.append({QByteArray::fromHex(o["hex"].toString().toLatin1()), o["delay"].toDouble()});
        }
        QList<Frame> stretched = stretchDelays(frames, 100);
        QJsonArray delays = golden["stretched"].toArray();
        QCOMPARE(stretched.size(), delays.size());
        for (int i = 0; i < delays.size(); ++i)
            QCOMPARE(stretched[i].delayMs, delays[i].toDouble());
        QCOMPARE(header(stretched).toHex(), golden["header"].toString().toLatin1());
        int calls = 0;
        QByteArray stream = encode(stretched, [&](int, int total) {
            ++calls;
            QCOMPARE(total, int(stretched.size()));
        });
        QCOMPARE(calls, int(stretched.size()));
        QCOMPARE(stream.toHex(), golden["stream"].toString().toLatin1());
        QCOMPARE(encode({{QByteArray(70000 * 2, '\x09'), 20}}).toHex(), golden["big_run"].toString().toLatin1());
    }

    void backupRoundTrip()
    {
        QList<Frame> frames{{QByteArray(8 * 4 * 2, 'a'), 40}, {QByteArray(8 * 4 * 2, 'b'), 100}};
        QByteArray blob = header(frames) + frames[0].pixels + frames[1].pixels;
        auto back = fromBackup(blob, 8, 4, 10);
        QCOMPARE(back.size(), 2);
        QCOMPARE(back[1].pixels, frames[1].pixels);
        QCOMPARE(back[1].delayMs, 100.0);
        QCOMPARE(fromBackup(blob, 8, 4, 1).size(), 1);
        QCOMPARE(storedSize(frames, 8, 4), qint64(256 + 2 * 64));
    }

    void fitKeepsTheCanvas()
    {
        QImage tall(10, 100, QImage::Format_RGB32);
        tall.fill(Qt::white);
        QImage c = fit(tall, 320, 172, FIT_CONTAIN);
        QCOMPARE(c.size(), QSize(320, 172));
        QCOMPARE(QColor(c.pixel(0, 0)), QColor(Qt::black));
        QCOMPARE(QColor(c.pixel(160, 86)), QColor(Qt::white));
        QCOMPARE(QColor(fit(tall, 320, 172, FIT_COVER).pixel(0, 0)), QColor(Qt::white));
    }

    // a full stock-size animation: 152 frames of 320x172
    void encodeSpeed()
    {
        QList<Frame> frames;
        for (int i = 0; i < 152; ++i) {
            QByteArray px(320 * 172 * 2, '\0');
            for (int k = 0; k < px.size(); k += 2)
                px[k] = char((k / 2 + i * 3) % 251);
            frames.append({px, 40});
        }
        QElapsedTimer t;
        t.start();
        QByteArray s = encode(frames);
        qInfo() << "encoded 152 frames in" << t.elapsed() << "ms," << s.size() << "bytes";
        QVERIFY(!s.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestScreenMedia)
#include "tst_screenmedia.moc"
