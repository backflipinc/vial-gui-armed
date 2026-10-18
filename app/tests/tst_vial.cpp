#include "core/bytes.h"
#include "core/lzma.h"
#include "fakekeyboard.h"
#include "protocol/vial.h"

#include <QTest>

using namespace bytes;

namespace {

struct VialKb {
    QByteArray definition = R"({"name":"test","matrix":{"rows":2,"cols":3}})";
    QByteArray compressed = lzma::compressXz(definition);
    QByteArray keymap;
    QByteArray macros;
    int macroCount = 3;
    quint32 vialProtocol = 6;
    QList<quint16> settings{1, 2, 3, 7, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 40};
    int bufferReads = 0;

    QByteArray handle(const QByteArray &q)
    {
        QByteArray r = q;
        auto put = [&r](int off, const QByteArray &d) { r.replace(off, d.size(), d); };
        switch (u8(q, 0)) {
        case vial::CMD_VIA_GET_PROTOCOL_VERSION: r[1] = 0; r[2] = 9; break;
        case vial::CMD_VIA_GET_LAYER_COUNT: r[1] = 4; break;
        case vial::CMD_VIA_KEYMAP_GET_BUFFER: put(4, keymap.mid(be16(q, 1), u8(q, 3))); break;
        case vial::CMD_VIA_MACRO_GET_COUNT: r[1] = char(macroCount); break;
        case vial::CMD_VIA_MACRO_GET_BUFFER_SIZE: r[1] = char(macros.size() >> 8); r[2] = char(macros.size()); break;
        case vial::CMD_VIA_MACRO_GET_BUFFER:
            ++bufferReads;
            put(4, macros.mid(be16(q, 1), u8(q, 3)));
            break;
        case vial::CMD_VIA_VIAL_PREFIX:
            switch (u8(q, 1)) {
            case vial::CMD_VIAL_GET_KEYBOARD_ID: {
                QByteArray d;
                putLe32(d, vialProtocol);
                putLe32(d, 0x89ABCDEF);
                putLe32(d, 0x01234567);
                put(0, d);
                break;
            }
            case vial::CMD_VIAL_GET_SIZE: {
                QByteArray d;
                putLe32(d, quint32(compressed.size()));
                put(0, d);
                break;
            }
            case vial::CMD_VIAL_GET_DEFINITION: {
                QByteArray d = compressed.mid(int(le32(q, 2)) * 32, 32);
                r = d + QByteArray(32 - d.size(), '\xEE');
                break;
            }
            case vial::CMD_VIAL_QMK_SETTINGS_QUERY: {
                quint16 cur = le16(q, 2);
                QByteArray d;
                for (quint16 s : settings)
                    if (s > cur && d.size() < 32)
                        putLe16(d, s);
                while (d.size() < 32)
                    putLe16(d, 0xFFFF);
                r = d;
                break;
            }
            }
            break;
        }
        return r;
    }
};

struct Rig {
    VialKb vkb;
    FakeKeyboard *kb;
    std::unique_ptr<hid::Link> link;
    Rig()
    {
        auto dev = std::make_unique<FakeKeyboard>();
        kb = dev.get();
        kb->handler = [this](const QByteArray &q) { return vkb.handle(q); };
        link = std::make_unique<hid::Link>(std::move(dev), false);
    }
};

}

class TestVial : public QObject {
    Q_OBJECT

private slots:
    void identity()
    {
        Rig r;
        QCOMPARE(vial::viaProtocol(*r.link), 9);
        auto id = vial::identity(*r.link);
        QCOMPARE(id.vialProtocol, 6);
        QCOMPARE(id.keyboardId, Q_UINT64_C(0x0123456789ABCDEF));
        QCOMPARE(vial::layerCount(*r.link), 4);
    }

    void viaKeyboardStaysUnsupported()
    {
        Rig r;
        r.vkb.vialProtocol = 0xFFFFFFFF;
        QVERIFY(!vial::vialProtocolSupported(vial::identity(*r.link).vialProtocol));
        QVERIFY(vial::vialProtocolSupported(-1));
    }

    void definition()
    {
        Rig r;
        QCOMPARE(vial::definition(*r.link), r.vkb.definition);
    }

    void keymapInChunks()
    {
        Rig r;
        for (int i = 0; i < 100; ++i)
            r.vkb.keymap.append(char(i));
        QCOMPARE(vial::keymapBuffer(*r.link, 100), r.vkb.keymap);
        QCOMPARE(r.kb->requests.size(), 4); // 28 bytes a packet
    }

    void macrosStopEarlyAndNormalize()
    {
        Rig r;
        r.vkb.macros = QByteArray("ab\0c\0\0", 6) + QByteArray(600, '\0');
        QByteArray out = vial::macroBuffer(*r.link, vial::macroInfo(*r.link));
        QCOMPARE(out, QByteArray("ab\0c\0\0", 6));
        QCOMPARE(r.vkb.bufferReads, hid::Link::kWindow); // one window in flight, not the whole buffer
    }

    void macrosShortBufferIsPadded()
    {
        Rig r;
        r.vkb.macros = QByteArray("xy", 2);
        r.vkb.macroCount = 2;
        QCOMPARE(vial::macroBuffer(*r.link, vial::macroInfo(*r.link)), QByteArray("xy\0\0", 4));
    }

    void settingsQueryLoops()
    {
        Rig r;
        auto s = vial::qmkSettingsSupported(*r.link);
        QCOMPARE(s.size(), r.vkb.settings.size());
        QVERIFY(s.contains(40));
        QCOMPARE(r.kb->requests.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestVial)
#include "tst_vial.moc"
