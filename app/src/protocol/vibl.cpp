#include "protocol/vibl.h"

#include "core/bytes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QThread>
#include <QTimeZone>

#include <stdexcept>

namespace vibl {
namespace {

constexpr int kChunk = 64;

QByteArray padded(QByteArray msg)
{
    if (msg.size() > kChunk)
        throw std::runtime_error("vibl message too long");
    msg.append(kChunk - msg.size(), '\0');
    return msg;
}

bool sendRetries(hid::RawHid &dev, const QByteArray &data, int retries = 200)
{
    for (int i = 0; i < retries; ++i) {
        int ret = dev.write(data);
        if (ret == data.size() + 1)
            return true;
        if (ret >= 0)
            return false;
        QThread::msleep(10);
    }
    return false;
}

QByteArray recv8(hid::RawHid &dev)
{
    QByteArray d = dev.read(5000);
    if (d.size() < 8)
        throw std::runtime_error("the bootloader did not answer");
    return d.left(8);
}

}

std::variant<Package, QString> parse(const QByteArray &fw)
{
    if (fw.size() < 64 || (fw.left(8) != "VIALFW00" && fw.left(8) != "VIALFW01"))
        return QStringLiteral("Error: Invalid signature");
    Package p{fw.mid(8, 8), bytes::le64(fw, 16), fw.mid(64)};
    QByteArray expected = fw.mid(32, 32);
    QByteArray got = QCryptographicHash::hash(p.payload, QCryptographicHash::Sha256);
    if (got != expected)
        return QStringLiteral("Error: Firmware failed integrity check\n\texpected=%1\n\tgot=%2")
            .arg(QString::fromLatin1(expected.toHex()), QString::fromLatin1(got.toHex()));
    return p;
}

int version(hid::RawHid &dev)
{
    sendRetries(dev, padded(QByteArray("VC\x00", 3)));
    return quint8(recv8(dev)[0]);
}

QByteArray uid(hid::RawHid &dev)
{
    sendRetries(dev, padded(QByteArray("VC\x01", 3)));
    return recv8(dev);
}

void flash(hid::RawHid &dev, const Package &pkg, bool insecure, const std::function<void(const QString &)> &log,
           const std::function<void(double)> &progress)
{
    log(QStringLiteral("* Firmware build date: %1 (UTC)")
            .arg(QDateTime::fromSecsSinceEpoch(qint64(pkg.timestamp), QTimeZone::UTC).toString("yyyy-MM-dd HH:mm:ss")));

    sendRetries(dev, padded(QByteArray("VC\x00", 3)));
    int ver = quint8(recv8(dev)[0]);
    log(QStringLiteral("* Bootloader version: %1").arg(ver));
    if (ver != 0 && ver != 1)
        throw std::runtime_error("Error: Unsupported bootloader version");

    QByteArray id = uid(dev);
    log(QStringLiteral("* Vial ID: %1").arg(QString::fromLatin1(id.toHex())));
    if (id == QByteArray(8, '\xFF'))
        log(QStringLiteral("\n\n\n!!! WARNING !!!\nBootloader UID is not set, make sure to configure it"
                           " before releasing production firmware\n!!! WARNING !!!\n\n"));
    if (id != pkg.uid)
        throw std::runtime_error(QStringLiteral("Error: Firmware package was built for different device\n\texpected=%1\n\tgot=%2")
                                     .arg(QString::fromLatin1(pkg.uid.toHex()), QString::fromLatin1(id.toHex()))
                                     .toStdString());

    QByteArray payload = pkg.payload;
    while (payload.size() % kChunk)
        payload.append('\0');

    log(QStringLiteral("Flashing..."));
    QByteArray start("VC\x02", 3);
    bytes::putLe16(start, quint16(payload.size() / kChunk));
    sendRetries(dev, padded(start));
    for (int off = 0; off < payload.size(); off += kChunk) {
        if (!sendRetries(dev, payload.mid(off, kChunk)))
            throw std::runtime_error("Error while sending data, firmware is corrupted");
        progress(double(off + kChunk) / payload.size());
    }

    log(QStringLiteral("Rebooting..."));
    // insecure mode on first boot, to restore the keymap and macros
    if (insecure)
        sendRetries(dev, padded(QByteArray("VC\x04", 3)));
    sendRetries(dev, padded(QByteArray("VC\x03", 3)));
}

}
