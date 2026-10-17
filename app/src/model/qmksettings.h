#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>

// The QMK Settings page's fields (qmk_settings.json) and how each setting travels to the keyboard
namespace qmksettings {

struct Field {
    enum class Type { Boolean, Integer };
    Type type = Type::Boolean;
    QString title;
    quint16 qsid = 0;
    int width = 1;
    int bit = 0;     // Boolean
    int min = 0, max = 0; // Integer
};

struct Tab {
    QString name;
    QList<Field> fields;
};

class Catalog {
public:
    static Catalog &instance(); // loaded from :/resources/base/qmk_settings.json
    explicit Catalog(const QByteArray &json);

    const QList<Tab> &tabs() const { return tabs_; }
    bool supported(quint16 qsid) const { return byQsid_.contains(qsid); }
    QByteArray serialize(quint16 qsid, quint32 value) const;
    quint32 deserialize(quint16 qsid, const QByteArray &data) const;

private:
    int width(quint16 qsid) const;

    QList<Tab> tabs_;
    QHash<quint16, QList<Field>> byQsid_;
};

}
