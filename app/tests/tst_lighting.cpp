#include "core/lzma.h"
#include "render/lighting.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace lighting;

namespace {

const QList<QPair<int, qint64>> kHits = {{5, 100}, {40, 400}, {41, 650}, {70, 1400}};
const QList<qint64> kTimes = {0, 16, 250, 700, 1500, 4000, 12345, 70000};

template <class Obj, class Render>
QJsonArray frames(Obj &obj, Render render)
{
    QJsonArray out;
    auto hits = kHits;
    for (qint64 t : kTimes) {
        while (!hits.isEmpty() && hits.first().second <= t) {
            obj.hit(hits.first().first, hits.first().second);
            hits.removeFirst();
        }
        QJsonArray frame;
        for (const Rgb &c : render(obj, t))
            frame.append(QJsonArray{c[0], c[1], c[2]});
        out.append(frame);
    }
    return out;
}

int diff(const QJsonArray &got, const QJsonArray &want, const QString &what)
{
    int bad = 0;
    for (int f = 0; f < want.size(); ++f) {
        QJsonArray g = got[f].toArray(), w = want[f].toArray();
        if (g.size() != w.size()) {
            qWarning() << what << "frame" << f << "size" << g.size() << w.size();
            return 1;
        }
        for (int i = 0; i < w.size(); ++i)
            if (g[i] != w[i] && ++bad <= 3)
                qWarning() << what << "t" << kTimes[f] << "led" << i << g[i].toArray() << w[i].toArray();
    }
    return bad;
}

}

// Same colours as the Python app's effect previews, light bar and theme renderer (tools/gen_lighting_golden.py)
class TestLighting : public QObject {
    Q_OBJECT

    QJsonObject golden;
    QList<armed::Led> leds;

private slots:
    void initTestCase()
    {
        QFile f(QStringLiteral(VIAL_ARMED_TEST_DATA "/lighting-golden.json.xz"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        golden = QJsonDocument::fromJson(*lzma::decompress(f.readAll())).object();
        QFile l(QStringLiteral(VIAL_ARMED_TEST_DATA "/leds-type84.json"));
        QVERIFY(l.open(QIODevice::ReadOnly));
        for (const auto &v : QJsonDocument::fromJson(l.readAll()).array()) {
            QJsonArray a = v.toArray();
            leds.append({quint8(a[0].toInt()), quint8(a[1].toInt()), quint8(a[2].toInt()), quint8(a[3].toInt()),
                         quint8(a[4].toInt())});
        }
        QCOMPARE(leds.size(), 82);
    }

    void effects()
    {
        QJsonObject g = golden["effects"].toObject();
        int bad = 0;
        for (auto it = g.begin(); it != g.end(); ++it) {
            EffectPreview p(leds);
            p.setEffect(it.key());
            p.h = 20;
            p.s = 230;
            p.v = 200;
            p.speed = 140;
            p.color2 = {150, 200};
            bad += diff(frames(p, [](EffectPreview &e, qint64 t) { return e.frame(t); }), it.value().toArray(), it.key());
        }
        QCOMPARE(bad, 0);
    }

    void strip()
    {
        QJsonObject g = golden["strip"].toObject();
        int bad = 0;
        for (auto it = g.begin(); it != g.end(); ++it) {
            StripPreview s;
            s.mode = it.key();
            s.color = {30, 220};
            QJsonArray got;
            for (qint64 t : kTimes) {
                QJsonArray frame;
                for (const Rgb &c : s.frame(t))
                    frame.append(QJsonArray{c[0], c[1], c[2]});
                got.append(frame);
            }
            bad += diff(got, it.value().toArray(), it.key());
        }
        QCOMPARE(bad, 0);
    }

    void themes()
    {
        QJsonObject g = golden["themes"].toObject();
        int bad = 0;
        for (auto it = g.begin(); it != g.end(); ++it) {
            QJsonObject c = it.value().toObject();
            QByteArray packed = QByteArray::fromHex(c["theme"].toString().toLatin1());
            theme::Theme th = theme::Theme::unpack(packed, int(leds.size()), 16);
            QCOMPARE(th.pack(int(leds.size()), 16), packed);
            ThemeRenderer r(leds);
            r.caps = it.key().endsWith("|1");
            if (r.caps)
                r.layersOn = {1};
            bad += diff(frames(r, [&th](ThemeRenderer &rr, qint64 t) { return rr.frame(th, t, 230); }),
                        c["frames"].toArray(), it.key());
        }
        QCOMPARE(bad, 0);
    }

    void themeJsonRoundTrip()
    {
        auto keymap = [](int, int, int) { return QString(); };
        for (int i = 0; i < theme::presetNames().size(); ++i) {
            theme::Theme t = theme::buildPreset(i, leds, keymap, 4);
            auto back = theme::Theme::fromJson(t.toJson(leds), leds, 8);
            QVERIFY(std::holds_alternative<theme::Theme>(back));
            QCOMPARE(std::get<theme::Theme>(back).pack(int(leds.size()), 8), t.pack(int(leds.size()), 8));
        }
    }

    // MIX: NORMAL covers, ADD brightens, MULTIPLY tints
    void blendModes()
    {
        const QList<armed::Led> one = {{0, 0, 112, 32}};
        auto mix = [&](int blend, theme::Rgb top) {
            theme::Theme t = theme::Theme::blank("T", 1);
            theme::Layer below = theme::Layer::make(theme::SOLID, {0});
            below.a = {0, 0, 200};
            theme::Layer above = theme::Layer::make(theme::SOLID, {0});
            above.a = top;
            above.blend = blend;
            t.layers = {below, above};
            lighting::ThemeRenderer r(one);
            return r.frame(t, 0)[0];
        };
        QCOMPARE(mix(theme::NORMAL, {200, 0, 0}), (theme::Rgb{200, 0, 0}));
        QCOMPARE(mix(theme::ADD, {200, 0, 0}), (theme::Rgb{200, 0, 200}));
        QCOMPARE(mix(theme::MULTIPLY, {255, 0, 128}), (theme::Rgb{0, 0, 100}));
    }

    // the same rules as quantum/armed/armed_theme.c layer_keys()
    void keymapLayerKeys()
    {
        const QList<armed::Led> six = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}};
        // layer 0: A, MO(1), LT2(KC_B), FN_MO13, TG(3), C; layer 1: X on key 0 only; layer 3: KC_TRNS everywhere
        const QStringList base = {"KC_A", "MO(1)", "LT2(KC_B)", "FN_MO13", "TG(3)", "KC_C"};
        auto keymap = [&](int l, int, int c) -> QString {
            if (l == 0)
                return base[c];
            if (l == 1)
                return c == 0 ? "KC_X" : "KC_TRNS";
            if (l == 2)
                return c == 5 ? "KC_NO" : "KC_TRNS";
            return "KC_TRNS";
        };
        QCOMPARE(theme::layerKeys(six, keymap, 4, 1), (QSet<int>{0, 1, 3}));
        QCOMPARE(theme::layerKeys(six, keymap, 4, 2), (QSet<int>{2}));
        QCOMPARE(theme::layerKeys(six, keymap, 4, 3), (QSet<int>{3, 4}));
        QVERIFY(theme::layerKeys(six, keymap, 4, 4).isEmpty());

        theme::Layer l = theme::Layer::make(theme::SOLID, {5});
        auto keys = [&](int n) { return theme::layerKeys(six, keymap, 4, n); };
        QCOMPARE(theme::coverage(l, 6, keys), QSet<int>{5});
        l.keys = theme::K_ALL;
        QCOMPARE(theme::coverage(l, 6, keys).size(), 6);
        l.keys = 2;
        QCOMPARE(theme::coverage(l, 6, keys), QSet<int>{2});

        // byte 17 of the layer, kept through the wire format and the export
        QCOMPARE(quint8(l.pack(6)[17]), quint8(2));
        QCOMPARE(theme::Layer::unpack(l.pack(6), 6).keys, 2);
        theme::Theme t = theme::Theme::blank("T", 6);
        t.layers = {l};
        auto back = theme::Theme::fromJson(t.toJson(six), six, 8);
        QCOMPARE(std::get<theme::Theme>(back).layers[0].keys, 2);
    }
};

QTEST_GUILESS_MAIN(TestLighting)
#include "tst_lighting.moc"
