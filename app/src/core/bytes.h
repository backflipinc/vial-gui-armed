#pragma once

#include <QByteArray>
#include <QString>

#include <initializer_list>

namespace bytes {

inline QByteArray of(std::initializer_list<int> v)
{
    QByteArray out;
    out.reserve(int(v.size()));
    for (int b : v)
        out.append(char(b & 0xFF));
    return out;
}

inline quint8 u8(const QByteArray &d, int off)
{
    return off < d.size() ? quint8(d[off]) : 0;
}

inline quint16 le16(const QByteArray &d, int off)
{
    return quint16(u8(d, off) | u8(d, off + 1) << 8);
}

inline quint16 be16(const QByteArray &d, int off)
{
    return quint16(u8(d, off) << 8 | u8(d, off + 1));
}

inline quint32 le32(const QByteArray &d, int off)
{
    return quint32(le16(d, off)) | quint32(le16(d, off + 2)) << 16;
}

inline quint32 be32(const QByteArray &d, int off)
{
    return quint32(be16(d, off)) << 16 | be16(d, off + 2);
}

inline quint64 le64(const QByteArray &d, int off)
{
    return quint64(le32(d, off)) | quint64(le32(d, off + 4)) << 32;
}

inline void putLe16(QByteArray &d, quint16 v)
{
    d.append(char(v & 0xFF));
    d.append(char(v >> 8));
}

inline void putBe16(QByteArray &d, quint16 v)
{
    d.append(char(v >> 8));
    d.append(char(v & 0xFF));
}

inline void putLe32(QByteArray &d, quint32 v)
{
    putLe16(d, quint16(v));
    putLe16(d, quint16(v >> 16));
}

inline void putBe32(QByteArray &d, quint32 v)
{
    putBe16(d, quint16(v >> 16));
    putBe16(d, quint16(v));
}

// the C string at off, up to the first NUL
inline QString cstr(const QByteArray &d, int off)
{
    QByteArray s = d.mid(off);
    int nul = s.indexOf('\0');
    return QString::fromLatin1(nul < 0 ? s : s.left(nul));
}

}
