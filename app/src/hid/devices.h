#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

namespace hid {

inline constexpr auto kVialSerialMagic = "vial:f64c2b3c";
inline constexpr auto kViblSerialMagic = "vibl:d4f8159c";

enum class DeviceKind { Vial, ViaStack, Sideload, Dongle, Bootloader };

struct DeviceInfo {
    QByteArray path;
    quint16 vid = 0;
    quint16 pid = 0;
    QString serial;
    QString manufacturer;
    QString product;
    DeviceKind kind = DeviceKind::Vial;

    quint32 viaId() const { return quint32(vid) << 16 | pid; }
    QString title() const;
    bool isDongle() const { return kind == DeviceKind::Dongle; }
    bool operator==(const DeviceInfo &o) const { return path == o.path && kind == o.kind; }
};

struct DiscoveryOptions {
    QSet<quint32> viaStackIds;         // VID << 16 | PID with a downloaded VIA definition
    int sideloadVid = -1, sideloadPid = -1;
    QSet<QByteArray> busyPaths;        // open in a session: present, but don't probe them again
    bool probeDongles = true;          // false: a dongle counts only if an earlier probe found a keyboard
};

bool isDongle(quint16 vid, quint16 pid);

// Blocking (a dongle probe can take a second): call it off the GUI thread
// unprobed: set when probeDongles is off and some dongle still needs asking
QList<DeviceInfo> discover(const DiscoveryOptions &opt, bool *unprobed = nullptr);
// abandons dongle probes in progress, for a quick exit
void cancelDiscovery();

}
