#include "model/definition.h"

#include <QJsonDocument>

#include <stdexcept>

namespace {

bool pair(const QString &s, int &a, int &b)
{
    const QStringList p = s.split(QLatin1Char(','));
    if (p.size() != 2)
        return false;
    bool ok1 = false, ok2 = false;
    a = p[0].trimmed().toInt(&ok1);
    b = p[1].trimmed().toInt(&ok2);
    return ok1 && ok2;
}

}

std::variant<Definition, QString> Definition::parse(const QByteArray &json)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (!doc.isObject())
        return QStringLiteral("the keyboard definition is not valid JSON: %1").arg(err.errorString());
    return parse(doc.object());
}

std::variant<Definition, QString> Definition::parse(const QJsonObject &json)
{
    Definition d;
    d.json = json;
    d.name = json.value("name").toString();
    const QJsonObject matrix = json.value("matrix").toObject();
    d.rows = matrix.value("rows").toInt();
    d.cols = matrix.value("cols").toInt();
    const QJsonObject layouts = json.value("layouts").toObject();
    d.layoutLabels = layouts.value("labels");
    d.customKeycodes = json.value("customKeycodes").toArray();
    d.lighting = json.value("lighting").toString();
    const QJsonObject vial = json.value("vial").toObject();
    d.vibl = vial.value("vibl").toBool();
    d.midi = vial.value("midi").toString();

    QList<kle::Key> geo;
    try {
        geo = kle::deserialize(layouts.value("keymap").toArray());
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }

    for (const kle::Key &k : geo) {
        LayoutKey key;
        key.geo = k;
        if (k.labels[4] == QLatin1String("e")) {
            if (!pair(k.labels[0], key.encoder, key.direction))
                return QStringLiteral("bad encoder label \"%1\"").arg(k.labels[0]);
            key.role = LayoutKey::Role::Encoder;
            if (!d.encoderIndexes.contains(key.encoder))
                d.encoderIndexes.append(key.encoder);
            d.encoderCount = std::max(d.encoderCount, key.encoder + 1);
        } else if (k.decal || k.labels[0].contains(QLatin1Char(','))) {
            key.row = key.col = 0;
            if (k.labels[0].contains(QLatin1Char(',')) && !pair(k.labels[0], key.row, key.col))
                return QStringLiteral("bad matrix label \"%1\"").arg(k.labels[0]);
            key.role = LayoutKey::Role::Key;
            if (!d.matrix.contains(qMakePair(key.row, key.col)))
                d.matrix.append({key.row, key.col});
        }
        if (!k.labels[8].isEmpty() && !pair(k.labels[8], key.layoutIndex, key.layoutOption))
            return QStringLiteral("bad layout option label \"%1\"").arg(k.labels[8]);
        if (key.role == LayoutKey::Role::Encoder)
            d.encoders.append(key);
        else if (key.role == LayoutKey::Role::Key)
            d.keys.append(key);
    }
    for (const auto &[row, col] : d.matrix) {
        if (row >= d.rows || col >= d.cols)
            return QStringLiteral("malformed vial.json, key references %1,%2 but matrix declares rows=%3 cols=%4")
                .arg(row).arg(col).arg(d.rows).arg(d.cols);
    }
    return d;
}
