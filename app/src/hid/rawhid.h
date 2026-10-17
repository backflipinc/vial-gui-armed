#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

struct hid_device_;

namespace hid {

inline constexpr int kMsgLen = 32;

// One open HID interface; the seam the tests replace with a fake keyboard
class RawHid {
public:
    virtual ~RawHid() = default;
    // the report without hidapi's leading report id; returns bytes written or -1
    virtual int write(const QByteArray &report) = 0;
    // empty on timeout; throws on a dead device
    virtual QByteArray read(int timeoutMs) = 0;
};

class HidapiDevice : public RawHid {
public:
    static std::unique_ptr<HidapiDevice> open(const QByteArray &path);
    ~HidapiDevice() override;

    int write(const QByteArray &report) override;
    QByteArray read(int timeoutMs) override;

private:
    explicit HidapiDevice(hid_device_ *dev) : dev_(dev) {}
    hid_device_ *dev_;
};

}
