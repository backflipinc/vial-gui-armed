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
        auto st = KeyboardState::fromRaw(loadRaw(link, std::nullopt, AllEntries));
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
    void connectSkipsEntries()
    {
        Rig rig(LAYOUT_2x2, 2, 2, 2);
        rig.vk.tapDances = {{0x04, 0x05, 0x06, 0x07, 200}, {}};
        rig.vk.combos = {{0x04, 0x05, 0, 0, 0x06}};
        auto st = KeyboardState::fromRaw(loadRaw(rig.link, std::nullopt));
        QVERIFY(std::holds_alternative<KeyboardState>(st));
        KeyboardState kb = std::get<KeyboardState>(st);
        auto entryReads = [&rig] {
            int n = 0;
            for (const QByteArray &q : rig.vk.requests())
                if (quint8(q[0]) == 0xFE && quint8(q[1]) == vial::CMD_VIAL_DYNAMIC_ENTRY_OP
                    && quint8(q[2]) != vial::DYNAMIC_VIAL_GET_NUMBER_OF_ENTRIES)
                    ++n;
            return n;
        };
        QCOMPARE(entryReads(), 0);
        QCOMPARE(kb.entriesLoaded, 0);
        QCOMPARE(kb.dynamicCounts.tapDance, 2);

        kb.applyEntries(loadEntries(rig.link, kb.dynamicCounts, ComboEntries));
        QCOMPARE(entryReads(), 1);
        QCOMPARE(kb.entriesLoaded, int(ComboEntries));
        QCOMPARE(kb.combos.size(), 1);
        QVERIFY(kb.tapDances.isEmpty());

        // an edited list is not replaced by a later read
        kb.combos[0].output = QStringLiteral("KC_X");
        kb.applyEntries(loadEntries(rig.link, kb.dynamicCounts, AllEntries));
        QCOMPARE(kb.entriesLoaded, int(AllEntries));
        QCOMPARE(kb.combos[0].output, QStringLiteral("KC_X"));
        QCOMPARE(kb.tapDances.size(), 2);
    }

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
