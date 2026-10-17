#include "protocol/vial.h"

#include "core/bytes.h"
#include "core/lzma.h"

namespace vial {

using namespace bytes;

namespace {

QByteArray vialCmd(std::initializer_list<int> v)
{
    QByteArray m = of({CMD_VIA_VIAL_PREFIX});
    m += of(v);
    return m;
}

}

bool viaProtocolSupported(int via)
{
    return via == -1 || via == 9;
}

bool vialProtocolSupported(qint64 vial)
{
    return vial >= -1 && vial <= 6;
}

int viaProtocol(Link &link)
{
    return be16(link.transact(of({CMD_VIA_GET_PROTOCOL_VERSION})), 1);
}

Identity identity(Link &link)
{
    QByteArray d = link.transact(vialCmd({CMD_VIAL_GET_KEYBOARD_ID}));
    return {qint64(le32(d, 0)), le64(d, 4)};
}

QByteArray uid(Link &link)
{
    return link.transact(vialCmd({CMD_VIAL_GET_KEYBOARD_ID})).mid(4, 8);
}

QByteArray definition(Link &link)
{
    qint64 size = le32(link.transact(vialCmd({CMD_VIAL_GET_SIZE})), 0);
    QByteArray payload;
    payload.reserve(int(size));
    for (quint32 block = 0; size > 0; ++block, size -= hid::kMsgLen) {
        QByteArray m = vialCmd({CMD_VIAL_GET_DEFINITION});
        putLe32(m, block);
        QByteArray d = link.transact(m);
        payload += size < hid::kMsgLen ? d.left(int(size)) : d;
    }
    auto json = lzma::decompress(payload);
    if (!json)
        throw hid::LinkError("the keyboard definition is corrupt");
    return *json;
}

int layerCount(Link &link)
{
    return u8(link.transact(of({CMD_VIA_GET_LAYER_COUNT})), 1);
}

QByteArray keymapBuffer(Link &link, int size)
{
    QByteArray out;
    out.reserve(size);
    for (int off = 0; off < size; off += kBufferChunk) {
        int n = std::min(size - off, kBufferChunk);
        QByteArray m = of({CMD_VIA_KEYMAP_GET_BUFFER});
        putBe16(m, quint16(off));
        m.append(char(n));
        out += link.transact(m).mid(4, n);
    }
    return out;
}

void setKeycode(Link &link, int layer, int row, int col, quint16 code)
{
    QByteArray m = of({CMD_VIA_SET_KEYCODE, layer, row, col});
    putBe16(m, code);
    link.transact(m);
}

QPair<quint16, quint16> encoder(Link &link, int layer, int index)
{
    QByteArray d = link.transact(vialCmd({CMD_VIAL_GET_ENCODER, layer, index}));
    return {be16(d, 0), be16(d, 2)};
}

void setEncoder(Link &link, int layer, int index, int direction, quint16 code)
{
    QByteArray m = vialCmd({CMD_VIAL_SET_ENCODER, layer, index, direction});
    putBe16(m, code);
    link.transact(m);
}

quint32 layoutOptions(Link &link)
{
    return be32(link.transact(of({CMD_VIA_GET_KEYBOARD_VALUE, VIA_LAYOUT_OPTIONS})), 2);
}

void setLayoutOptions(Link &link, quint32 options)
{
    QByteArray m = of({CMD_VIA_SET_KEYBOARD_VALUE, VIA_LAYOUT_OPTIONS});
    putBe32(m, options);
    link.transact(m);
}

MacroInfo macroInfo(Link &link)
{
    MacroInfo info;
    info.count = u8(link.transact(of({CMD_VIA_MACRO_GET_COUNT})), 1);
    info.memory = be16(link.transact(of({CMD_VIA_MACRO_GET_BUFFER_SIZE})), 1);
    return info;
}

QByteArray macroBuffer(Link &link, const MacroInfo &info)
{
    QByteArray raw;
    for (int off = 0; off < info.memory; off += kBufferChunk) {
        int n = std::min(kBufferChunk, info.memory - off);
        QByteArray m = of({CMD_VIA_MACRO_GET_BUFFER});
        putBe16(m, quint16(off));
        m.append(char(n));
        raw += link.transact(m).mid(4, n);
        // only the first `count` macros matter, stop once they are all in
        if (raw.count('\0') > info.count)
            break;
    }
    QList<QByteArray> macros = raw.split('\0');
    while (macros.size() < info.count)
        macros.append(QByteArray());
    QByteArray out;
    for (int i = 0; i < info.count; ++i)
        out += macros[i] + '\0';
    return out;
}

void setMacroBuffer(Link &link, const QByteArray &data, int memory)
{
    if (data.size() > memory)
        throw hid::LinkError("the macros don't fit in the keyboard's memory");
    for (int off = 0; off < data.size(); off += kBufferChunk) {
        QByteArray chunk = data.mid(off, kBufferChunk);
        QByteArray m = of({CMD_VIA_MACRO_SET_BUFFER});
        putBe16(m, quint16(off));
        m.append(char(chunk.size()));
        link.transact(m + chunk);
    }
}

DynamicCounts dynamicCounts(Link &link)
{
    QByteArray d = link.transact(vialCmd({CMD_VIAL_DYNAMIC_ENTRY_OP, DYNAMIC_VIAL_GET_NUMBER_OF_ENTRIES}));
    return {u8(d, 0), u8(d, 1), u8(d, 2), u8(d, 3), u8(d, hid::kMsgLen - 1)};
}

QByteArray dynamicEntry(Link &link, quint8 getOp, int index, int size)
{
    QByteArray d = link.transact(vialCmd({CMD_VIAL_DYNAMIC_ENTRY_OP, getOp, index}));
    if (u8(d, 0) != 0)
        throw hid::LinkError("failed retrieving a dynamic entry from the device");
    return d.mid(1, size);
}

void setDynamicEntry(Link &link, quint8 setOp, int index, const QByteArray &entry, int retries)
{
    link.transact(vialCmd({CMD_VIAL_DYNAMIC_ENTRY_OP, setOp, index}) + entry, retries);
}

QSet<quint16> qmkSettingsSupported(Link &link)
{
    QSet<quint16> out;
    quint16 cur = 0;
    while (cur != 0xFFFF) {
        QByteArray m = vialCmd({CMD_VIAL_QMK_SETTINGS_QUERY});
        putLe16(m, cur);
        QByteArray d = link.transact(m);
        for (int x = 0; x + 1 < d.size(); x += 2) {
            quint16 qsid = le16(d, x);
            cur = std::max(cur, qsid);
            if (qsid != 0xFFFF)
                out.insert(qsid);
        }
    }
    return out;
}

std::optional<QByteArray> qmkSettingGet(Link &link, quint16 qsid)
{
    QByteArray m = vialCmd({CMD_VIAL_QMK_SETTINGS_GET});
    putLe16(m, qsid);
    QByteArray d = link.transact(m);
    if (u8(d, 0) != 0)
        return std::nullopt;
    return d.mid(1);
}

int qmkSettingSet(Link &link, quint16 qsid, const QByteArray &value)
{
    QByteArray m = vialCmd({CMD_VIAL_QMK_SETTINGS_SET});
    putLe16(m, qsid);
    return u8(link.transact(m + value), 0);
}

void qmkSettingsReset(Link &link)
{
    link.transact(vialCmd({CMD_VIAL_QMK_SETTINGS_RESET}), 1);
}

UnlockStatus unlockStatus(Link &link, int vialProtocol, int retries)
{
    UnlockStatus st;
    if (vialProtocol < 0)
        return st; // VIA keyboards are always unlocked
    QByteArray d = link.transact(vialCmd({CMD_VIAL_GET_UNLOCK_STATUS}), retries);
    st.unlocked = u8(d, 0);
    st.inProgress = u8(d, 1);
    for (int x = 0; x < 15; ++x) {
        int row = u8(d, 2 + x * 2), col = u8(d, 3 + x * 2);
        if (row != 255 && col != 255)
            st.keys.append({row, col});
    }
    return st;
}

void unlockStart(Link &link)
{
    link.transact(vialCmd({CMD_VIAL_UNLOCK_START}));
}

QByteArray unlockPoll(Link &link)
{
    return link.transact(vialCmd({CMD_VIAL_UNLOCK_POLL}));
}

void lock(Link &link)
{
    link.transact(vialCmd({CMD_VIAL_LOCK}));
}

QByteArray matrixState(Link &link)
{
    return link.transact(of({CMD_VIA_GET_KEYBOARD_VALUE, VIA_SWITCH_MATRIX_STATE}), 3);
}

QByteArray lightingGet(Link &link, const QByteArray &args, int retries)
{
    return link.transact(of({CMD_VIA_LIGHTING_GET_VALUE}) + args, retries);
}

void lightingSet(Link &link, const QByteArray &args, int retries)
{
    link.transact(of({CMD_VIA_LIGHTING_SET_VALUE}) + args, retries);
}

void lightingSave(Link &link)
{
    link.transact(of({CMD_VIA_LIGHTING_SAVE}));
}

VialRgbInfo vialRgbInfo(Link &link)
{
    QByteArray d = lightingGet(link, of({VIALRGB_GET_INFO})).mid(2);
    return {u8(d, 0) | u8(d, 1) << 8, u8(d, 2)};
}

QSet<quint16> vialRgbSupported(Link &link)
{
    QSet<quint16> out{0};
    quint16 maxEffect = 0;
    while (maxEffect < 0xFFFF) {
        QByteArray args = of({VIALRGB_GET_SUPPORTED});
        putLe16(args, maxEffect);
        QByteArray d = lightingGet(link, args, 1).mid(2);
        for (int x = 0; x + 1 < d.size(); x += 2) {
            quint16 v = le16(d, x);
            if (v != 0xFFFF)
                out.insert(v);
            maxEffect = std::max(maxEffect, v);
        }
    }
    return out;
}

VialRgbMode vialRgbMode(Link &link)
{
    QByteArray d = lightingGet(link, of({VIALRGB_GET_MODE})).mid(2);
    return {le16(d, 0), u8(d, 2), u8(d, 3), u8(d, 4), u8(d, 5)};
}

void setVialRgbMode(Link &link, const VialRgbMode &m)
{
    QByteArray args = of({VIALRGB_SET_MODE});
    putLe16(args, m.mode);
    args += of({m.speed, m.h, m.s, m.v});
    lightingSet(link, args);
}

void jumpToBootloader(Link &link)
{
    // the keyboard reboots instead of answering
    link.write(of({CMD_VIA_BOOTLOADER}));
}

}
