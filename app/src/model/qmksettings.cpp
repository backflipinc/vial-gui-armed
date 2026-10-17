#include "model/qmksettings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace qmksettings {

Catalog &Catalog::instance()
{
    static Catalog c([] {
        QFile f(QStringLiteral(":/resources/base/qmk_settings.json"));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }());
    return c;
}

Catalog::Catalog(const QByteArray &json)
{
    for (const auto &t : QJsonDocument::fromJson(json).object().value("tabs").toArray()) {
        QJsonObject to = t.toObject();
        Tab tab;
        tab.name = to.value("name").toString();
        for (const auto &f : to.value("fields").toArray()) {
            QJsonObject fo = f.toObject();
            Field field;
            field.type = fo.value("type").toString() == QLatin1String("integer") ? Field::Type::Integer
                                                                                  : Field::Type::Boolean;
            field.title = fo.value("title").toString();
            field.qsid = quint16(fo.value("qsid").toInt());
            field.width = fo.value("width").toInt(1);
            field.bit = fo.value("bit").toInt(0);
            field.min = fo.value("min").toInt();
            field.max = fo.value("max").toInt();
            tab.fields.append(field);
            byQsid_[field.qsid].append(field);
        }
        tabs_.append(tab);
    }
}

int Catalog::width(quint16 qsid) const
{
    const auto fields = byQsid_.value(qsid);
    return fields.isEmpty() ? 1 : fields.first().width;
}

QByteArray Catalog::serialize(quint16 qsid, quint32 value) const
{
    QByteArray out;
    for (int i = 0; i < width(qsid); ++i)
        out += char((value >> (8 * i)) & 0xFF);
    return out;
}

quint32 Catalog::deserialize(quint16 qsid, const QByteArray &data) const
{
    quint32 v = 0;
    for (int i = 0; i < width(qsid) && i < data.size(); ++i)
        v |= quint32(quint8(data[i])) << (8 * i);
    return v;
}

}
