#include "model/hudpresets.h"
#include "render/hudrender.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace hud;

namespace {

QByteArray read(const QString &name)
{
    QFile f(QStringLiteral(VIAL_ARMED_TEST_DATA "/hud/") + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// the same values as the Stub in tools/gen_hud_golden.py
class Stub : public Sources {
public:
    std::optional<Value> value(int source) const override
    {
        switch (source) {
        case S_TIME: return Value{0, 0, C_ACCENT, true, "13:37:42"};
        case S_DATE: return Value{0, 0, C_ACCENT, true, "2026-10-17"};
        case S_LINK: return Value{0, 0, C_ACCENT, true, "USB  2.4G"};
        case S_LAYER: return Value{0, 0, C_ACCENT, true, "LAYER 1"};
        case S_CHARGING: return Value{1, 0, C_OK, true, {}};
        case S_BATTERY: return Value{73, 0, C_OK, true, {}};
        }
        if (source == S_HOST + 7)
            return std::nullopt;
        if (source == S_HOST + 8)
            return Value{0, 0, C_LINE, false, {}};
        if (source >= S_HOST_TXT)
            return Value{0, 0, C_TEXT, true, {}};
        int n = (source * 37) % 101;
        bool third = source % 3 == 0;
        return Value{third ? n * 10 + 3 : n, third ? 1 : 0, C_TEXT + source % 4, true, {}};
    }

    std::optional<QString> hostText(int slot) const override
    {
        static const QStringList t = {"Line one\nline two is a much longer line that has to wrap across the box width\n\nlast",
                                      "", QString(200, QLatin1Char('x')), "tabs\tand ~ symbols {}[]"};
        return t.value(slot);
    }

    QList<Value> history(int source) const override
    {
        QList<Value> out;
        for (int i = 0; i < 80; ++i)
            out.append(Value{(i * 13 + source * 7) % 101, 0, C_TEXT, true, {}});
        return out;
    }
};

}

// Pixel-exact with the Python app's renderer (tools/gen_hud_golden.py), which matches the keyboard
class TestHud : public QObject {
    Q_OBJECT

    QList<std::shared_ptr<QffFont>> fonts;
    QJsonArray golden;

private slots:
    void initTestCase()
    {
        for (int i = 0; i < 3; ++i) {
            fonts.append(QffFont::load(read(QStringLiteral("font%1.qff").arg(i))));
            QVERIFY(fonts.last());
        }
        golden = QJsonDocument::fromJson(read("golden.json")).array();
        QCOMPARE(golden.size(), 4);
    }

    void cases_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < golden.size(); ++i)
            QTest::newRow(qPrintable(golden[i].toObject()["name"].toString())) << i;
    }

    void cases()
    {
        QFETCH(int, index);
        QJsonObject c = golden[index].toObject();
        auto parsed = Layout::fromJson(QJsonDocument(c["layout"].toObject()).toJson());
        QVERIFY(std::holds_alternative<Layout>(parsed));
        const Layout &layout = std::get<Layout>(parsed);

        auto packed = layout.pack({});
        QVERIFY(std::holds_alternative<QByteArray>(packed));
        QCOMPARE(std::get<QByteArray>(packed).toHex(), c["packed"].toString().toLatin1());
        auto back = Layout::unpack(std::get<QByteArray>(packed), {});
        QVERIFY(back);
        QCOMPARE(*back, layout);

        Stub stub;
        Renderer r(320, 172, fonts, DEFAULT_PALETTE, stub);
        r.names = {{S_HOST + 8, "HOST 9"}};
        QImage got = r.render(layout);
        QImage want;
        QVERIFY(want.loadFromData(read(c["png"].toString())));
        want = want.convertToFormat(QImage::Format_RGB888);
        QCOMPARE(got.size(), want.size());
        int bad = 0;
        for (int y = 0; y < got.height(); ++y)
            for (int x = 0; x < got.width(); ++x)
                if (got.pixel(x, y) != want.pixel(x, y) && ++bad <= 5)
                    qWarning() << "pixel" << x << y << Qt::hex << got.pixel(x, y) << want.pixel(x, y);
        if (bad)
            got.save(QStringLiteral("hud-mismatch-%1.png").arg(index));
        QCOMPARE(bad, 0);
    }

    void packLimits()
    {
        Layout l;
        for (int i = 0; i < 41; ++i)
            l.widgets.append(Widget::make(W_BAR, 0, 0));
        QVERIFY(std::holds_alternative<QString>(l.pack({})));
        Layout big;
        Widget w = Widget::make(W_TEXT, 0, 0);
        for (int i = 0; i < 20; ++i) {
            w.label = QString(30, QChar('a' + i));
            big.widgets.append(w);
        }
        QVERIFY(std::holds_alternative<QString>(big.pack({})));
    }

    void presets()
    {
        QFile f(QStringLiteral(VIAL_ARMED_TEST_DATA "/hud/presets.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject want = QJsonDocument::fromJson(f.readAll()).object();
        int checked = 0;
        for (const auto &p : hud::presets()) {
            if (!p.make)
                continue;
            const Layout l = p.make();
            QVERIFY2(std::holds_alternative<QByteArray>(l.pack({})), qPrintable(p.name));
            QCOMPARE(QJsonDocument::fromJson(l.toJson()).object(), want.value(p.name).toObject());
            ++checked;
        }
        QCOMPARE(checked, want.size());
    }

    void renderSpeed()
    {
        auto parsed = Layout::fromJson(QJsonDocument(golden[0].toObject()["layout"].toObject()).toJson());
        Stub stub;
        Renderer r(320, 172, fonts, DEFAULT_PALETTE, stub);
        QBENCHMARK {
            r.render(std::get<Layout>(parsed));
        }
    }
};

QTEST_GUILESS_MAIN(TestHud)
#include "tst_hud.moc"
