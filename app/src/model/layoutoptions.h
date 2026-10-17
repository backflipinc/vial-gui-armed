#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

// VIA layout options ("labels" in vial.json): checkboxes and selects packed into one number, first one highest
namespace layoutoptions {

struct Choice {
    QString label;
    QStringList options; // empty: a yes / no choice
    int bits() const;
};

QList<Choice> parse(const QJsonValue &labels);
QList<int> unpack(quint32 value, const QList<Choice> &choices);
quint32 pack(const QList<int> &values, const QList<Choice> &choices);

}
