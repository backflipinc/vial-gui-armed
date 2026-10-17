#include "hid/link.h"
#include "keycodes/keycodes.h"
#include "model/keyboard.h"
#include "virtualkeyboard.h"

#include <QTest>

// test_keyboard.py: loading, changing keys and encoders, a layout file there and back
namespace {

const QByteArray LAYOUT_2x2 = R"({"name":"test","vendorId":"0x0000","productId":"0x1111","lighting":"none","matrix":{"rows":2,"cols":2},"layouts":{"keymap":[["0,0","0,1"],["1,0","1,1"]]}})";
const QByteArray LAYOUT_ENCODER = R"({"name":"test","vendorId":"0x0000","productId":"0x1111","lighting":"none","matrix":{"rows":1,"cols":1},"layouts":{"keymap":[["0,0\n\n\n\n\n\n\n\n\ne","0,1\n\n\n\n\n\n\n\n\ne"],["0,0"]]}})";

struct Rig {
    VirtualKeyboard vk;
    hid::Link link;
    KeyboardState kb;

    Rig(const QByteArray &json, int rows, int cols, int layers, int encoders = 0)
        : vk(json, rows, cols, layers, encoders)
        , link(vk.open(), false)
    {
    }
    void load()
    {
        auto st = KeyboardState::fromRaw(loadRaw(link, std::nullopt));
        QVERIFY2(std::holds_alternative<KeyboardState>(st), qPrintable(std::get_if<QString>(&st) ? *std::get_if<QString>(&st) : ""));
        kb = std::get<KeyboardState>(st);
    }
};

QString s(int code)
{
    return keycodes::Registry::instance().serialize(code);
}

void fill2x2(VirtualKeyboard &vk)
{
    int v = 1;
    for (int l = 0; l < 2; ++l)
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                vk.key(l, r, c) = quint16(v++);
}

}

class TestKeyboard : public QObject {
    Q_OBJECT

private slots:
    void keyboardLayout()
    {
        Rig rig(LAYOUT_2x2, 2, 2, 2);
        fill2x2(rig.vk);
        rig.load();
        QCOMPARE(rig.kb.layers, 2);
        int v = 1;
        for (int l = 0; l < 2; ++l)
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c)
                    QCOMPARE(rig.kb.key(l, r, c), s(v++));
    }

    void setKey()
    {
        Rig rig(LAYOUT_2x2, 2, 2, 2);
        fill2x2(rig.vk);
        rig.load();
        vial::setKeycode(rig.link, 1, 1, 0, 9);
        QCOMPARE(rig.vk.writes(), QList<QByteArray>{QByteArray::fromHex("050101000009")});
        QCOMPARE(rig.vk.key(1, 1, 0), 9);
    }

    void layoutSaveRestore()
    {
        Rig rig(LAYOUT_2x2, 2, 2, 2);
        fill2x2(rig.vk);
        rig.load();
        rig.kb.keymap[rig.kb.keyIndex(1, 1, 0)] = s(10);
        const QByteArray vil = rig.kb.saveLayout();

        Rig fresh(LAYOUT_2x2, 2, 2, 2);
        fill2x2(fresh.vk);
        fresh.load();
        const LayoutRestore plan = planRestore(fresh.kb, vil);
        QCOMPARE(plan.keys.size(), 1);
        QCOMPARE(plan.keys[0].layer, 1);
        QCOMPARE(plan.keys[0].row, 1);
        QCOMPARE(plan.keys[0].col, 0);
        QCOMPARE(plan.keys[0].code, s(10));
        QVERIFY(plan.encoders.isEmpty());
        applyRestore(fresh.kb, plan);
        QCOMPARE(fresh.kb.key(1, 1, 0), s(10));
        // nothing left to change
        QVERIFY(planRestore(fresh.kb, vil).empty());
    }

    void encoderSimple()
    {
        Rig rig(LAYOUT_ENCODER, 1, 1, 4, 1);
        for (int l = 0; l < 4; ++l) {
            rig.vk.key(l, 0, 0) = quint16(l + 1);
            rig.vk.encoder(l, 0, 0) = quint16(10 + 2 * l);
            rig.vk.encoder(l, 0, 1) = quint16(11 + 2 * l);
        }
        rig.load();
        for (int l = 0; l < 4; ++l) {
            QCOMPARE(rig.kb.encoder(l, 0, 0), s(10 + 2 * l));
            QCOMPARE(rig.kb.encoder(l, 0, 1), s(11 + 2 * l));
        }
    }

    void encoderChange()
    {
        Rig rig(LAYOUT_ENCODER, 1, 1, 4, 1);
        rig.load();
        vial::setEncoder(rig.link, 1, 0, 1, 0x20);
        QCOMPARE(rig.vk.writes(), QList<QByteArray>{QByteArray::fromHex("FE040100010020")});
        QCOMPARE(rig.vk.encoder(1, 0, 1), 0x20);
    }
};

QTEST_GUILESS_MAIN(TestKeyboard)
#include "tst_keyboard.moc"
