#include "protocol/armed.h"

#include "core/bytes.h"
#include "core/paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTimeZone>

Q_LOGGING_CATEGORY(lcArmed, "armed.protocol")

namespace armed {

using namespace bytes;

namespace {

QByteArray cmd(std::initializer_list<int> v)
{
    return of({CMD_ARMED}) + of(v);
}

QByteArray send(Link &link, const QByteArray &payload)
{
    return link.transact(of({CMD_ARMED}) + payload);
}

QByteArray send(Link &link, std::initializer_list<int> v)
{
    return link.transact(cmd(v));
}

QList<QByteArray> fetchList(Link &link, int kind, int count)
{
    QList<QByteArray> out;
    for (int i = 0; i < count; ++i) {
        QByteArray d = send(link, {SUB_LIST, kind, i});
        if (u8(d, 0) == UNHANDLED)
            break;
        out.append(d.mid(4));
    }
    return out;
}

// sends requests with up to SCREEN_WINDOW in flight; replies come back in order
void pipeline(Link &link, const QList<QByteArray> &requests, const std::function<void(int, const QByteArray &)> &onReply,
              const Cancelled &cancelled, int timeoutMs)
{
    int sent = 0, pending = 0;
    try {
        for (int i = 0; i < requests.size(); ++i) {
            while (sent < requests.size() && pending < SCREEN_WINDOW) {
                link.write(requests[sent++]);
                ++pending;
            }
            QByteArray reply = link.read(timeoutMs);
            if (reply.isEmpty())
                throw Error("the keyboard did not answer");
            --pending;
            onReply(i, reply);
            if (cancelled && cancelled())
                throw Error("cancelled");
        }
    } catch (...) {
        // leave nothing in flight for the next command
        for (; pending > 0; --pending) {
            try {
                if (link.read(2000).isEmpty())
                    break;
            } catch (...) {
                break;
            }
        }
        throw;
    }
}

const QList<QPair<quint16, QList<quint8>>> &featureValues()
{
    static const QList<QPair<quint16, QList<quint8>>> v = {
        {F_RGB_MATRIX, {V_ENABLED, V_EFFECT, V_HUE, V_SAT, V_VAL, V_SPEED}},
        {F_COLOR2, {V_COLOR2}},
        {F_FN_COLOR, {V_FN_COLOR}},
        {F_STRIP, {V_STRIP_MODE, V_STRIP_COLOR, V_STRIP_CAPS}},
        {F_SLEEP, {V_SLEEP}},
        {F_SCREEN, {V_SCREEN}},
        {F_CLOCK, {V_CLOCK}},
        {F_KEYMAP, {V_NKRO, V_GUI_LOCK}},
        {F_SNAPTAP, {V_SNAPTAP}},
        {F_DISPLAY, {V_ACCENT, V_SCREEN_OFF}},
        {F_BT_SLOT, {V_BT_SLOT}},
    };
    return v;
}

Color color(const QByteArray &d)
{
    return {u8(d, 0), u8(d, 1), cstr(d, 2)};
}

}

QString Info::versionString() const
{
    return QStringLiteral("%1.%2.%3").arg(version[0]).arg(version[1]).arg(version[2]);
}

ListCache::ListCache(const QString &dir, quint64 uid, const QString &version, quint16 features)
    : path_(QStringLiteral("%1/cache/%2-%3-%4.json")
                .arg(dir, QString::number(uid, 16).rightJustified(16, QLatin1Char('0')), version,
                     QString::number(features, 16)))
{
}

QList<QByteArray> ListCache::get(const QString &name, const std::function<QList<QByteArray>()> &fetch)
{
    if (path_.isEmpty())
        return fetch();
    QJsonObject store;
    QFile f(path_);
    if (f.open(QIODevice::ReadOnly))
        store = QJsonDocument::fromJson(f.readAll()).object();
    if (store.contains(name)) {
        QList<QByteArray> out;
        for (const auto &v : store.value(name).toArray())
            out.append(QByteArray::fromHex(v.toString().toLatin1()));
        return out;
    }
    QList<QByteArray> out = fetch();
    QJsonArray arr;
    for (const auto &b : out)
        arr.append(QString::fromLatin1(b.toHex()));
    store.insert(name, arr);
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile w(path_);
    if (w.open(QIODevice::WriteOnly)) {
        w.write(QJsonDocument(store).toJson(QJsonDocument::Compact));
        if (!w.commit())
            qCWarning(lcArmed) << "cache:" << w.errorString();
    }
    return out;
}

std::optional<Info> probe(Link &link, qint64 vialProtocol, std::optional<quint64> keyboardId)
{
    if (vialProtocol < 0)
        return std::nullopt;
    QByteArray d;
    try {
        d = send(link, {SUB_INFO});
    } catch (const std::exception &e) {
        qCWarning(lcArmed) << "probe failed:" << e.what();
        return std::nullopt;
    }
    if (u8(d, 0) != CMD_ARMED || d.mid(2, 4) != "ARMD" || u8(d, 6) != PROTOCOL)
        return std::nullopt;

    Info info;
    info.version = {u8(d, 7), u8(d, 8), u8(d, 9)};
    info.features = quint16(u8(d, 10) | u8(d, 11) << 8);
    info.maxBrightness = u8(d, 12 + L_COUNT) ? u8(d, 12 + L_COUNT) : 255;
    QByteArray counts = d.mid(12, L_COUNT);

    ListCache cache = keyboardId ? ListCache(paths::configDir(), *keyboardId, info.versionString(), info.features)
                                 : ListCache();
    auto counted = [&](int kind) {
        int n = u8(counts, kind);
        return cache.get(QStringLiteral("list%1-%2").arg(kind).arg(n), [&] { return fetchList(link, kind, n); });
    };
    auto open = [&](int kind, quint16 feature) -> QList<QByteArray> {
        if (!info.has(feature))
            return {};
        return cache.get(QStringLiteral("list%1").arg(kind), [&] { return fetchList(link, kind, LIST_MAX); });
    };

    for (const auto &e : counted(L_EFFECTS))
        info.effects.append({u8(e, 0), u8(e, 1), cstr(e, 2)});
    for (const auto &e : counted(L_COLORS))
        info.colors.append(color(e));
    for (const auto &e : counted(L_STRIP_MODES))
        info.stripModes.append(cstr(e, 0));
    for (const auto &e : counted(L_SLEEP))
        info.sleepMinutes.append(u8(e, 0));
    for (const auto &e : counted(L_LEDS))
        info.leds.append({u8(e, 0), u8(e, 1), u8(e, 2), u8(e, 3), u8(e, 4)});
    for (const auto &e : open(L_SNAPTAP_MODES, F_SNAPTAP))
        info.snaptapModes.append(cstr(e, 0));
    for (const auto &e : open(L_ACCENTS, F_DISPLAY))
        info.accents.append(color(e));
    for (const auto &e : open(L_SCREEN_OFF, F_DISPLAY))
        info.screenOff.append({le16(e, 0), cstr(e, 2)});
    info.values = readValues(link, info.features);
    return info;
}

QMap<int, quint8> readValues(Link &link, quint16 features)
{
    QMap<int, quint8> out;
    for (const auto &[feature, vids] : featureValues()) {
        if (!(features & feature))
            continue;
        for (quint8 vid : vids) {
            QByteArray d = send(link, {SUB_GET, vid});
            if (u8(d, 0) != UNHANDLED)
                out[vid] = u8(d, 3);
        }
    }
    return out;
}

void set(Link &link, quint8 vid, int value)
{
    send(link, {SUB_SET, vid, value & 0xFF});
}

void save(Link &link)
{
    send(link, {SUB_SAVE});
}

std::optional<QDateTime> clockGet(Link &link)
{
    QByteArray d = send(link, {SUB_CLOCK_GET});
    if (u8(d, 0) == UNHANDLED || !u8(d, 2))
        return std::nullopt;
    QDateTime utc = QDateTime::fromSecsSinceEpoch(le32(d, 3), QTimeZone::UTC);
    return QDateTime(utc.date(), utc.time());
}

void clockSet(Link &link, const QDateTime &local)
{
    QDateTime asUtc(local.date(), local.time(), QTimeZone::UTC);
    qint64 secs = asUtc.toSecsSinceEpoch() + (local.time().msec() >= 500 ? 1 : 0);
    QByteArray m = of({SUB_CLOCK_SET});
    putLe32(m, quint32(secs));
    if (u8(send(link, m), 0) == UNHANDLED)
        throw Error("clock not set");
}

void bootloader(Link &link)
{
    link.write(cmd({SUB_BOOTLOADER}));
}

std::optional<ScreenInfo> screenInfo(Link &link)
{
    QByteArray d = send(link, {SUB_SCREEN_INFO});
    if (u8(d, 0) == UNHANDLED)
        return std::nullopt;
    return ScreenInfo{le16(d, 2), le16(d, 4), u8(d, 6), u8(d, 7), u8(d, 8), le32(d, 9), le16(d, 13)};
}

QByteArray screenRead(Link &link, quint32 offset, quint32 size, const Progress &progress, const Cancelled &cancelled)
{
    QList<QPair<quint32, int>> reqs;
    for (quint32 o = offset; o < offset + size; o += SCREEN_READ_PAYLOAD)
        reqs.append({o, int(std::min<quint32>(SCREEN_READ_PAYLOAD, offset + size - o))});
    QList<QByteArray> msgs;
    for (const auto &[o, n] : reqs) {
        QByteArray m = cmd({SUB_SCREEN_READ});
        putLe32(m, o);
        m.append(char(n));
        msgs.append(m);
    }
    QByteArray out;
    out.reserve(int(size));
    pipeline(link, msgs, [&](int i, const QByteArray &d) {
        auto [o, n] = reqs[i];
        if (u8(d, 0) != CMD_ARMED || u8(d, 1) != SUB_SCREEN_READ || le32(d, 2) != o)
            throw Error(QStringLiteral("read failed at byte %1").arg(o).toStdString());
        out += d.mid(7, n);
        if (progress)
            progress(out.size(), size);
    }, cancelled, 5000);
    return out;
}

void screenUpload(Link &link, quint32 size, const QByteArray &stream, const Progress &progress,
                  const Cancelled &cancelled)
{
    QByteArray begin = of({SUB_SCREEN_BEGIN});
    putLe32(begin, size);
    if (u8(send(link, begin), 0) == UNHANDLED)
        throw Error("the keyboard refused the upload");
    QList<QByteArray> msgs;
    for (int i = 0, off = 0; off < stream.size(); ++i, off += SCREEN_DATA_PAYLOAD) {
        QByteArray chunk = stream.mid(off, SCREEN_DATA_PAYLOAD);
        msgs.append(cmd({SUB_SCREEN_DATA, i & 0xFF, int(chunk.size())}) + chunk);
    }
    try {
        // a 64 KiB flash erase can hold a reply for a second or two
        pipeline(link, msgs, [&](int i, const QByteArray &d) {
            if (u8(d, 1) != SUB_SCREEN_DATA || u8(d, 2) != (i & 0xFF) || u8(d, 3) != 0)
                throw Error(QStringLiteral("upload failed at byte %1").arg(le32(d, 4)).toStdString());
            if (progress)
                progress(le32(d, 4), size);
        }, cancelled, 10000);
    } catch (...) {
        send(link, {SUB_SCREEN_END, 0});
        throw;
    }
    if (u8(send(link, {SUB_SCREEN_END, 1}), 0) == UNHANDLED)
        throw Error("the keyboard rejected the finished upload");
}

std::optional<ThemeInfo> themeInfo(Link &link)
{
    QByteArray d = send(link, {SUB_THEME_INFO});
    if (u8(d, 0) == UNHANDLED)
        return std::nullopt;
    ThemeInfo t{u8(d, 2), u8(d, 3), u8(d, 4), le16(d, 5), u8(d, 7), {}};
    for (int i = 0; i < t.slotCount; ++i)
        t.modes.append(u8(d, 8 + i));
    return t;
}

QByteArray themeRead(Link &link, int slot, int size)
{
    QByteArray out;
    for (int off = 0; off < size; off += THEME_PAYLOAD) {
        int n = std::min(THEME_PAYLOAD, size - off);
        QByteArray d = send(link, {SUB_THEME_READ, slot, off & 0xFF, off >> 8, n});
        if (u8(d, 0) == UNHANDLED)
            throw Error(QStringLiteral("theme read failed at byte %1").arg(off).toStdString());
        out += d.mid(6, n);
    }
    return out;
}

void themeWrite(Link &link, int slot, int offset, const QByteArray &payload)
{
    for (int i = 0; i < payload.size(); i += THEME_PAYLOAD) {
        QByteArray chunk = payload.mid(i, THEME_PAYLOAD);
        int o = offset + i;
        if (u8(send(link, of({SUB_THEME_WRITE, slot, o & 0xFF, o >> 8, int(chunk.size())}) + chunk), 0) == UNHANDLED)
            throw Error(QStringLiteral("theme write failed at byte %1").arg(o).toStdString());
    }
}

void themeSave(Link &link, int slot)
{
    if (u8(send(link, {SUB_THEME_SAVE, slot}), 0) == UNHANDLED)
        throw Error("the keyboard could not store the theme");
}

void themeRevert(Link &link, int slot)
{
    send(link, {SUB_THEME_REVERT, slot});
}

std::optional<HudInfo> hudInfo(Link &link)
{
    QByteArray d = send(link, {SUB_HUD_INFO});
    if (u8(d, 0) == UNHANDLED)
        return std::nullopt;
    return HudInfo{u8(d, 2), u8(d, 3), u8(d, 4), le16(d, 5), le16(d, 7), u8(d, 9), u8(d, 10), u8(d, 11),
                   u8(d, 12), u8(d, 13), u8(d, 14), le16(d, 15), le16(d, 17), u8(d, 19), u8(d, 20)};
}

QList<HudSource> hudSources(Link &link, const HudInfo &info, ListCache &cache)
{
    auto raw = cache.get(QStringLiteral("hud-sources-%1").arg(info.sources), [&] {
        QList<QByteArray> out;
        for (int i = 0; i < info.sources; ++i) {
            QByteArray d = send(link, {SUB_HUD_SOURCE, i});
            if (u8(d, 0) == UNHANDLED)
                break;
            out.append(d.mid(3));
        }
        return out;
    });
    QList<HudSource> out;
    for (const auto &d : raw) {
        HudSource s{u8(d, 0), u8(d, 1), std::nullopt, cstr(d, 3)};
        if (u8(d, 2) != 0xFF)
            s.dec = u8(d, 2);
        out.append(s);
    }
    return out;
}

std::optional<HudValue> hudValue(Link &link, int source)
{
    QByteArray d = send(link, {SUB_HUD_VALUE, source});
    if (u8(d, 0) == UNHANDLED)
        return std::nullopt;
    if (u8(d, 1) != SUB_HUD_VALUE || u8(d, 2) != source)
        throw Error("a late reply to another request");
    return HudValue{bool(u8(d, 3)), u8(d, 4), u8(d, 5), qint32(le32(d, 6)), cstr(d, 10)};
}

QList<std::array<quint8, 3>> hudPalette(Link &link, const HudInfo &info)
{
    QList<std::array<quint8, 3>> out;
    for (int role = 0; role < info.colors; ++role) {
        QByteArray d = send(link, {SUB_HUD_PALETTE, role});
        if (u8(d, 0) != UNHANDLED)
            out.append({u8(d, 3), u8(d, 4), u8(d, 5)});
        else
            out.append({0, 0, 0});
    }
    return out;
}

QByteArray hudFont(Link &link, int index, const Progress &progress)
{
    auto request = [index](quint32 off, int n) {
        QByteArray m = cmd({SUB_HUD_FONT, index});
        putLe32(m, off);
        m.append(char(n));
        return m;
    };
    QByteArray d = link.transact(request(0, HUD_FONT_PAYLOAD));
    if (u8(d, 0) == UNHANDLED)
        throw Error(QStringLiteral("font %1 read failed").arg(index).toStdString());
    // the QFF header holds the font's size at byte 9
    quint32 size = le32(d, 8 + 9);
    QByteArray out = d.mid(8, int(std::min<quint32>(size, HUD_FONT_PAYLOAD)));
    QList<QPair<quint32, int>> reqs;
    for (quint32 o = quint32(out.size()); o < size; o += HUD_FONT_PAYLOAD)
        reqs.append({o, int(std::min<quint32>(HUD_FONT_PAYLOAD, size - o))});

    if (link.isDongle()) {
        for (const auto &[o, n] : reqs) {
            QByteArray r = link.transact(request(o, n));
            if (u8(r, 0) == UNHANDLED)
                throw Error(QStringLiteral("font %1 read failed at byte %2").arg(index).arg(o).toStdString());
            out += r.mid(8, n);
            if (progress)
                progress(out.size(), size);
        }
        return out;
    }
    QList<QByteArray> msgs;
    for (const auto &[o, n] : reqs)
        msgs.append(request(o, n));
    pipeline(link, msgs, [&](int i, const QByteArray &r) {
        auto [o, n] = reqs[i];
        if (u8(r, 0) != CMD_ARMED || u8(r, 1) != SUB_HUD_FONT || le32(r, 3) != o)
            throw Error(QStringLiteral("font %1 read failed at byte %2").arg(index).arg(o).toStdString());
        out += r.mid(8, n);
        if (progress)
            progress(out.size(), size);
    }, {}, 2000);
    return out;
}

QByteArray hudRead(Link &link, const HudInfo &info)
{
    QByteArray out;
    for (int off = 0; off < info.layoutSize; off += HUD_PAYLOAD) {
        int n = std::min(HUD_PAYLOAD, info.layoutSize - off);
        QByteArray d = send(link, {SUB_HUD_READ, off & 0xFF, off >> 8, n});
        if (u8(d, 0) == UNHANDLED)
            throw Error(QStringLiteral("HUD read failed at byte %1").arg(off).toStdString());
        out += d.mid(5, n);
    }
    return out;
}

void hudWrite(Link &link, const QByteArray &blob, const std::optional<QByteArray> &previous)
{
    for (int off = 0; off < blob.size(); off += HUD_PAYLOAD) {
        QByteArray chunk = blob.mid(off, HUD_PAYLOAD);
        if (previous && previous->mid(off, chunk.size()) == chunk)
            continue;
        if (u8(send(link, of({SUB_HUD_WRITE, off & 0xFF, off >> 8, int(chunk.size())}) + chunk), 0) == UNHANDLED)
            throw Error(QStringLiteral("HUD write failed at byte %1").arg(off).toStdString());
    }
    if (u8(send(link, {SUB_HUD_APPLY, 0}), 0) == UNHANDLED)
        throw Error("the keyboard rejected the layout");
}

void hudStore(Link &link)
{
    if (u8(send(link, {SUB_HUD_APPLY, 1}), 0) == UNHANDLED)
        throw Error("the keyboard could not store the layout");
}

void hudRevert(Link &link, bool factory)
{
    send(link, {SUB_HUD_REVERT, factory ? 1 : 0});
}

QList<QByteArray> hudPushPackets(const QList<HostValue> &values, int ttl)
{
    QList<QByteArray> out;
    for (int i = 0; i < values.size(); i += HUD_PUSH_SLOTS) {
        auto part = values.mid(i, HUD_PUSH_SLOTS);
        QByteArray m = cmd({SUB_HUD_PUSH, std::min(255, ttl), int(part.size())});
        for (const auto &v : part) {
            m += of({v.slot, v.dec});
            putLe32(m, quint32(qint32(std::clamp<qint64>(v.num, INT32_MIN, INT32_MAX))));
        }
        out.append(m);
    }
    return out;
}

QList<QByteArray> hudTextPackets(int slot, const QString &text, int ttl, int limit)
{
    QByteArray raw;
    for (char32_t c : text.toUcs4())
        raw.append(c < 128 ? char(c) : '?');
    raw.truncate(limit - 1);
    QList<QByteArray> out;
    int off = 0;
    for (;;) {
        QByteArray chunk = raw.mid(off, HUD_TEXT_PAYLOAD);
        bool last = off + chunk.size() >= raw.size();
        out.append(cmd({SUB_HUD_TEXT, slot, std::min(255, ttl), off, int(chunk.size()), last ? 1 : 0}) + chunk);
        off += chunk.size();
        if (last)
            return out;
    }
}

}
