#include "hid/devices.h"
#include "hid/link.h"

#include <QElapsedTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QMutex>

#include <hidapi.h>

Q_LOGGING_CATEGORY(lcDevices, "armed.devices")

namespace hid {
namespace {

constexpr qint64 kDongleReprobeMs = 5000;

struct Probe {
    bool ok = false;
    qint64 at = 0;
};

QMutex probeMutex;
std::atomic<bool> probeCancel{false};
QHash<QByteArray, Probe> probes;

qint64 nowMs()
{
    static QElapsedTimer t = [] {
        QElapsedTimer e;
        e.start();
        return e;
    }();
    return t.elapsed();
}

QString wide(const wchar_t *s)
{
    return s ? QString::fromWCharArray(s) : QString();
}

bool isRawHid(const hid_device_info *d)
{
    return d->usage_page == 0xFF60 && d->usage == 0x61;
}

bool canOpen(const QByteArray &path)
{
#ifdef Q_OS_LINUX
    // a missing udev rule shows up here; mac and Windows won't reopen an open device, so skip them
    return HidapiDevice::open(path) != nullptr;
#else
    Q_UNUSED(path);
    return true;
#endif
}

// the dongle enumerates even when the keyboard is off or runs other firmware, so ask the keyboard
bool dongleHasVial(const QByteArray &path)
{
    {
        QMutexLocker lock(&probeMutex);
        Probe p = probes.value(path);
        if (p.ok || (p.at && nowMs() - p.at < kDongleReprobeMs))
            return p.ok;
    }
    bool ok = false;
    if (auto dev = HidapiDevice::open(path)) {
        Link link(std::move(dev), true);
        link.cancel = &probeCancel;
        try {
            QByteArray reply = link.transact(QByteArray("\xFE\x00", 2), 2);
            ok = reply.size() >= 12 && reply.mid(4, 8) != QByteArray(8, '\0');
        } catch (const LinkError &) {
        }
    }
    if (probeCancel)
        return false;
    QMutexLocker lock(&probeMutex);
    probes[path] = {ok, nowMs()};
    return ok;
}

}

void cancelDiscovery()
{
    probeCancel = true;
}

bool isDongle(quint16 vid, quint16 pid)
{
    return vid == 0x0C45 && pid == 0xFEFE;
}

QString DeviceInfo::title() const
{
    if (kind == DeviceKind::Bootloader)
        return QStringLiteral("Vial Bootloader [%1:%2]")
            .arg(QString::number(vid, 16).rightJustified(4, QLatin1Char('0')).toUpper(),
                 QString::number(pid, 16).rightJustified(4, QLatin1Char('0')).toUpper());
    QString s = (manufacturer + QLatin1Char(' ') + product).trimmed();
    switch (kind) {
    case DeviceKind::Sideload: return s + QStringLiteral(" [sideload]");
    case DeviceKind::ViaStack: return s + QStringLiteral(" [VIA]");
    case DeviceKind::Dongle: return s + QStringLiteral(" [2.4G]");
    default: return s;
    }
}

QList<DeviceInfo> discover(const DiscoveryOptions &opt)
{
    QList<DeviceInfo> out;
    hid_device_info *all = hid_enumerate(0, 0);
    for (hid_device_info *d = all; d; d = d->next) {
        DeviceInfo info;
        info.path = QByteArray(d->path);
        info.vid = d->vendor_id;
        info.pid = d->product_id;
        info.serial = wide(d->serial_number);
        info.manufacturer = wide(d->manufacturer_string);
        info.product = wide(d->product_string);
        const bool busy = opt.busyPaths.contains(info.path);

        if (info.vid == opt.sideloadVid && info.pid == opt.sideloadPid) {
            info.kind = DeviceKind::Sideload;
            if (!isRawHid(d) || (!busy && !canOpen(info.path)))
                continue;
        } else if (info.serial.contains(QLatin1String(kVialSerialMagic))) {
            info.kind = DeviceKind::Vial;
            if (!isRawHid(d) || (!busy && !canOpen(info.path)))
                continue;
        } else if (info.serial.contains(QLatin1String(kViblSerialMagic))) {
            info.kind = DeviceKind::Bootloader;
        } else if (isDongle(info.vid, info.pid)) {
            info.kind = DeviceKind::Dongle;
            if (!isRawHid(d) || (!busy && (!canOpen(info.path) || !dongleHasVial(info.path))))
                continue;
        } else if (opt.viaStackIds.contains(info.viaId())) {
            info.kind = DeviceKind::ViaStack;
            if (!isRawHid(d) || (!busy && !canOpen(info.path)))
                continue;
        } else {
            continue;
        }
        qCDebug(lcDevices) << "found" << info.title() << info.path;
        out.append(info);
    }
    hid_free_enumeration(all);
    return out;
}

}
