#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

// Vial macros: a macro is a list of actions; the keyboard stores them NUL-separated in one buffer
namespace macro {

enum class Kind { Text, Tap, Down, Up, Delay };

struct Action {
    Kind kind = Kind::Text;
    QString text;      // Text
    QStringList keys;  // Tap / Down / Up, keycode names
    int delay = 0;     // Delay, ms

    bool operator==(const Action &o) const = default;
    QString tag() const;
};

using Macro = QList<Action>;

// throws std::runtime_error (a delay before protocol 2)
QByteArray serialize(const Macro &m, qint64 vialProtocol);
Macro deserialize(const QByteArray &data, qint64 vialProtocol);

QList<Macro> deserializeAll(const QByteArray &buffer, int count, qint64 vialProtocol);
// throws if the list doesn't hold exactly `count` macros
QByteArray serializeAll(const QList<Macro> &macros, int count, qint64 vialProtocol);

// the .vil layout file form: [["tap", "KC_A"], ["text", "hi"], ["delay", 100]]
QJsonArray save(const Macro &m);
Macro restore(const QJsonArray &actions);

}
