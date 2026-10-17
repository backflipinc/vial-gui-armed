#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>

#include <array>

// Vial dynamic entries: the wire form (little-endian structs) and the .vil layout file form
namespace dynamic {

struct TapDance {
    std::array<QString, 4> keys{"KC_NO", "KC_NO", "KC_NO", "KC_NO"}; // tap, hold, double tap, tap+hold
    int tappingTerm = 0;

    static constexpr int kSize = 10;
    static TapDance unpack(const QByteArray &d);
    QByteArray pack() const;
    QJsonValue save() const;
    static TapDance restore(const QJsonValue &v);
    bool operator==(const TapDance &o) const = default;
};

struct Combo {
    std::array<QString, 4> keys{"KC_NO", "KC_NO", "KC_NO", "KC_NO"};
    QString output = "KC_NO";

    static constexpr int kSize = 10;
    static Combo unpack(const QByteArray &d);
    QByteArray pack() const;
    QJsonValue save() const;
    static Combo restore(const QJsonValue &v);
    bool operator==(const Combo &o) const = default;
};

struct KeyOverride {
    enum Option : quint8 {
        ActivationTriggerDown = 1 << 0,
        ActivationRequiredModDown = 1 << 1,
        ActivationNegativeModUp = 1 << 2,
        OneMod = 1 << 3,
        NoReregisterTrigger = 1 << 4,
        NoUnregisterOnOtherKeyDown = 1 << 5,
        Enabled = 1 << 7,
    };
    QString trigger = "KC_NO", replacement = "KC_NO";
    quint16 layers = 0;
    quint8 triggerMods = 0, negativeModMask = 0, suppressedMods = 0, options = 0;

    static constexpr int kSize = 10;
    static KeyOverride unpack(const QByteArray &d);
    QByteArray pack() const;
    QJsonValue save() const;
    static KeyOverride restore(const QJsonValue &v);
    // like the Python app: equal when they pack the same
    bool operator==(const KeyOverride &o) const { return pack() == o.pack(); }
};

struct AltRepeatKey {
    enum Option : quint8 {
        DefaultToThisAltKey = 1 << 0,
        Bidirectional = 1 << 1,
        IgnoreModHandedness = 1 << 2,
        Enabled = 1 << 3,
    };
    QString keycode = "KC_NO", altKeycode = "KC_NO";
    quint8 allowedMods = 0, options = 0;

    static constexpr int kSize = 6;
    static AltRepeatKey unpack(const QByteArray &d);
    QByteArray pack() const;
    QJsonValue save() const;
    static AltRepeatKey restore(const QJsonValue &v);
    bool operator==(const AltRepeatKey &o) const { return pack() == o.pack(); }
};

}
