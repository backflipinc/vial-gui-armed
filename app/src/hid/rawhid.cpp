#include "hid/rawhid.h"
#include "hid/link.h"

#include <hidapi.h>

namespace hid {

std::unique_ptr<HidapiDevice> HidapiDevice::open(const QByteArray &path)
{
    hid_device *dev = hid_open_path(path.constData());
    if (!dev)
        return nullptr;
    return std::unique_ptr<HidapiDevice>(new HidapiDevice(dev));
}

HidapiDevice::~HidapiDevice()
{
    hid_close(dev_);
}

int HidapiDevice::write(const QByteArray &report)
{
    QByteArray buf(1, '\0');
    buf += report;
    return hid_write(dev_, reinterpret_cast<const unsigned char *>(buf.constData()), size_t(buf.size()));
}

QByteArray HidapiDevice::read(int timeoutMs)
{
    QByteArray buf(kMsgLen, '\0');
    int n = hid_read_timeout(dev_, reinterpret_cast<unsigned char *>(buf.data()), size_t(buf.size()), timeoutMs);
    if (n < 0)
        throw LinkError("the device went away");
    buf.truncate(n);
    return buf;
}

}
