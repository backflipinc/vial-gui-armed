#pragma once

#include "hid/rawhid.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <variant>

// Vial bootloader (vibl): 64-byte reports, no replies to data packets
namespace vibl {

struct Package {
    QByteArray uid;
    quint64 timestamp = 0; // UTC build time
    QByteArray payload;
};

// a checked .vfw, or why it was rejected
std::variant<Package, QString> parse(const QByteArray &firmware);

int version(hid::RawHid &dev);
QByteArray uid(hid::RawHid &dev);

// throws std::runtime_error; log and progress (0..1) are called on the calling thread
void flash(hid::RawHid &dev, const Package &pkg, bool insecure, const std::function<void(const QString &)> &log,
           const std::function<void(double)> &progress);

}
