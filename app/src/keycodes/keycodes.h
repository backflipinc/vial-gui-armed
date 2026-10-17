#pragma once

#include "keycodes/tables.h"

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

// Keycode names <-> the keyboard's 16-bit codes, for the Vial protocol the keyboard speaks (v5 or v6).
// Mirrors the Python app's Keycode: one registry for the open keyboard, rebuilt on connect.
namespace keycodes {

using tables::Group;

inline constexpr auto kResetKeycode = "QK_BOOT";

struct Keycode {
    QString qmkId;
    QString label;
    QString tooltip;
    bool masked = false;
    QString printable;
    QStringList alias; // qmkId first
    QString requiresFeature;
    bool hidden = false;
};

// what the keyboard tells about itself that shapes the keycode lists
struct KeyboardTraits {
    qint64 protocol = 0; // Vial protocol: 6 uses the v6 numbering
    int layers = 0;
    int macroCount = 0;
    int tapDanceCount = 0;
    QJsonArray customKeycodes;
    QString midi; // "", "basic" or "advanced"
    QSet<QString> features;
};

class Registry {
public:
    static Registry &instance();

    Registry();
    void rebuild(const KeyboardTraits &kb);
    qint64 protocol() const { return protocol_; }

    // the keycode the name belongs to; "kc" means KC_NO (inner part of a masked template)
    const Keycode *find(const QString &qmkId) const;
    const Keycode *findOuter(const QString &qmkId) const;
    const Keycode *findInner(const QString &qmkId) const;
    const Keycode *findByRecorderAlias(const QString &alias) const;
    const Keycode *findByQmkId(const QString &qmkId) const;
    bool isMask(const QString &qmkId) const;
    bool isBasic(const QString &qmkId) const;
    QString label(const QString &qmkId) const;
    QString tooltip(const QString &qmkId) const;

    QString serialize(int code) const;
    // 0 for anything unknown
    int deserialize(const QString &value) const;
    std::optional<int> deserializeStrict(const QString &value) const;
    // e.g. KC_PERC -> LSFT(KC_5)
    QString normalize(const QString &qmkId) const;
    // a QMK constant in the keyboard's numbering
    std::optional<int> resolve(const QString &constant) const;

    QList<const Keycode *> group(Group g) const;
    const QList<const Keycode *> &layers() const { return layers_; }
    const QList<const Keycode *> &macros() const { return macros_; }
    const QList<const Keycode *> &tapDances() const { return tapDances_; }
    const QList<const Keycode *> &user() const { return user_; }
    const QList<const Keycode *> &midi() const { return midi_; }

private:
    const Keycode *make(const Keycode &k);
    const Keycode *make(const tables::KeycodeDef &d);
    void recreate();

    std::vector<std::unique_ptr<Keycode>> store_;
    QHash<QString, const Keycode *> byQmkId_;        // every keycode ever made, like qmk_id_to_keycode
    QHash<QString, const Keycode *> recorderAlias_;
    QSet<QString> maskedPrefixes_;
    QHash<Group, QList<const Keycode *>> groups_;
    QList<const Keycode *> layers_, macros_, tapDances_, user_, midi_, hidden_, all_;
    QHash<QString, const Keycode *> map_;            // current keycodes, "(kc)" stripped
    QHash<int, const Keycode *> raw_;
    qint64 protocol_ = 0;
    QHash<QString, int> kc_;
    QSet<int> maskedCodes_;
    QHash<QString, int> anyNames_;
};

}
