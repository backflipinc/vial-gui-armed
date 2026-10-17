#pragma once

#include "core/bytes.h"
#include "core/lzma.h"
#include "fakekeyboard.h"
#include "protocol/vial.h"

#include <QList>
#include <QMutex>

#include <array>
#include <memory>

// A whole Vial keyboard behind a FakeKeyboard, after the Python tests' VirtualKeyboard: keymap, encoders,
// macros, combos and tap dances that the app can read and change. Not a qmk-armed keyboard.
class VirtualKeyboard {
public:
    using Entry = std::array<quint16, 5>;

    VirtualKeyboard(const QByteArray &json, int rows, int cols, int layers = 4, int encoders = 0)
        : rows(rows)
        , cols(cols)
        , layers(layers)
        , encoderCount(encoders)
        , compressed_(lzma::compressXz(json))
    {
        keymap = QList<quint16>(layers * rows * cols, 0);
        this->encoders = QList<quint16>(layers * encoders * 2, 0);
        macros = QByteArray(512, '\0');
    }

    int rows, cols, layers, encoderCount;
    QList<quint16> keymap;   // [layer][row][col]
    QList<quint16> encoders; // [layer][index][direction]
    QByteArray macros;
    int macroCount = 8;
    QList<Entry> combos, tapDances;
    quint32 layoutOptions = 0;
    int vialProtocol = 6;

    quint16 &key(int layer, int row, int col) { return keymap[(layer * rows + row) * cols + col]; }
    quint16 &encoder(int layer, int index, int dir) { return encoders[(layer * encoderCount + index) * 2 + dir]; }

    // requests that change the keyboard, in order
    QList<QByteArray> writes() const
    {
        QMutexLocker l(&mutex_);
        return writes_;
    }

    // a fresh device for a session; every one talks to this keyboard
    std::unique_ptr<hid::RawHid> open()
    {
        auto dev = std::make_unique<FakeKeyboard>();
        dev->handler = [this](const QByteArray &q) { return handle(q); };
        return dev;
    }

    QByteArray handle(const QByteArray &q)
    {
        using namespace bytes;
        QMutexLocker l(&mutex_);
        QByteArray r = q;
        r.resize(32);
        auto put = [&r](int off, const QByteArray &d) { r.replace(off, d.size(), d); };
        auto keymapBytes = [this] {
            QByteArray b;
            for (quint16 k : keymap)
                putBe16(b, k);
            return b;
        };
        auto entry = [](const Entry &e) {
            QByteArray d(1, '\0');
            for (quint16 v : e)
                putLe16(d, v);
            return d;
        };
        switch (u8(q, 0)) {
        case vial::CMD_VIA_GET_PROTOCOL_VERSION: r[1] = 0; r[2] = 9; return r;
        case vial::CMD_VIA_GET_LAYER_COUNT: r[1] = char(layers); return r;
        case vial::CMD_VIA_KEYMAP_GET_BUFFER: put(4, keymapBytes().mid(be16(q, 1), u8(q, 3))); return r;
        case vial::CMD_VIA_SET_KEYCODE:
            writes_.append(q.left(6));
            key(u8(q, 1), u8(q, 2), u8(q, 3)) = be16(q, 4);
            return r;
        case vial::CMD_VIA_MACRO_GET_COUNT: r[1] = char(macroCount); return r;
        case vial::CMD_VIA_MACRO_GET_BUFFER_SIZE: r[1] = char(macros.size() >> 8); r[2] = char(macros.size()); return r;
        case vial::CMD_VIA_MACRO_GET_BUFFER: put(4, macros.mid(be16(q, 1), u8(q, 3))); return r;
        case vial::CMD_VIA_GET_KEYBOARD_VALUE: {
            QByteArray d;
            putBe32(d, layoutOptions);
            put(2, d);
            return r;
        }
        case vial::CMD_VIA_SET_KEYBOARD_VALUE:
            writes_.append(q.left(6));
            layoutOptions = be32(q, 2);
            return r;
        case vial::CMD_VIA_VIAL_PREFIX:
            break;
        default:
            r[0] = char(0xFF); // id_unhandled
            return r;
        }
        switch (u8(q, 1)) {
        case vial::CMD_VIAL_GET_KEYBOARD_ID: {
            QByteArray d;
            putLe32(d, quint32(vialProtocol));
            putLe32(d, 0xDEADBEEF);
            putLe32(d, 0xF00DFACE);
            put(0, d);
            return r;
        }
        case vial::CMD_VIAL_GET_SIZE: {
            QByteArray d;
            putLe32(d, quint32(compressed_.size()));
            put(0, d);
            return r;
        }
        case vial::CMD_VIAL_GET_DEFINITION: {
            QByteArray d = compressed_.mid(int(le32(q, 2)) * 32, 32);
            return d + QByteArray(32 - d.size(), '\0');
        }
        case vial::CMD_VIAL_GET_ENCODER: {
            QByteArray d;
            putBe16(d, encoder(u8(q, 2), u8(q, 3), 0));
            putBe16(d, encoder(u8(q, 2), u8(q, 3), 1));
            put(0, d);
            return r;
        }
        case vial::CMD_VIAL_SET_ENCODER:
            writes_.append(q.left(7));
            encoder(u8(q, 2), u8(q, 3), u8(q, 4)) = be16(q, 5);
            return r;
        case vial::CMD_VIAL_GET_UNLOCK_STATUS: r[0] = 0; r[1] = 0; return r;
        case vial::CMD_VIAL_QMK_SETTINGS_QUERY: return QByteArray(32, '\xFF');
        case vial::CMD_VIAL_DYNAMIC_ENTRY_OP:
            switch (u8(q, 2)) {
            case vial::DYNAMIC_VIAL_GET_NUMBER_OF_ENTRIES:
                r.fill('\0');
                r[0] = char(tapDances.size());
                r[1] = char(combos.size());
                r[31] = 0x03; // Caps Word, Layer Lock
                return r;
            case vial::DYNAMIC_VIAL_TAP_DANCE_GET: return entry(tapDances.value(u8(q, 3))) + QByteArray(21, '\0');
            case vial::DYNAMIC_VIAL_COMBO_GET: return entry(combos.value(u8(q, 3))) + QByteArray(21, '\0');
            case vial::DYNAMIC_VIAL_TAP_DANCE_SET:
            case vial::DYNAMIC_VIAL_COMBO_SET: {
                writes_.append(q.left(14));
                Entry e;
                for (int i = 0; i < 5; ++i)
                    e[i] = le16(q, 4 + 2 * i);
                auto &list = u8(q, 2) == vial::DYNAMIC_VIAL_COMBO_SET ? combos : tapDances;
                if (u8(q, 3) < list.size())
                    list[u8(q, 3)] = e;
                r.fill('\0');
                return r;
            }
            }
            break;
        }
        r[0] = char(0xFF);
        return r;
    }

private:
    QByteArray compressed_;
    QList<QByteArray> writes_;
    mutable QMutex mutex_;
};
