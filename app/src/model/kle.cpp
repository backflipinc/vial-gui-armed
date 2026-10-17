#include "model/kle.h"

#include <QJsonObject>

#include <stdexcept>

namespace kle {
namespace {

// [align][position in the KLE string] -> label slot
constexpr int kLabelMap[8][12] = {
    {0, 6, 2, 8, 9, 11, 3, 5, 1, 4, 7, 10},
    {1, 7, -1, -1, 9, 11, 4, -1, -1, -1, -1, 10},
    {3, -1, 5, -1, 9, 11, -1, -1, 4, -1, -1, 10},
    {4, -1, -1, -1, 9, 11, -1, -1, -1, -1, -1, 10},
    {0, 6, 2, 8, 10, -1, 3, 5, 1, 4, 7, -1},
    {1, 7, -1, -1, 10, -1, 4, -1, -1, -1, -1, -1},
    {3, -1, 5, -1, 10, -1, -1, -1, 4, -1, -1, -1},
    {4, -1, -1, -1, 10, -1, -1, -1, -1, -1, -1, -1},
};

}

QList<Key> deserialize(const QJsonArray &rows)
{
    QList<Key> keys;
    Key current;
    double clusterX = 0, clusterY = 0;
    int align = 4;

    for (const QJsonValue &rowValue : rows) {
        if (!rowValue.isArray())
            continue;
        const QJsonArray row = rowValue.toArray();
        for (int k = 0; k < row.size(); ++k) {
            const QJsonValue item = row[k];
            if (item.isString()) {
                Key key = current;
                key.width2 = current.width2 == 0 ? current.width : current.width2;
                key.height2 = current.height2 == 0 ? current.height : current.height2;
                key.labels = {};
                const QStringList parts = item.toString().split(QLatin1Char('\n'));
                if (align < 0 || align > 7)
                    throw std::runtime_error("bad label alignment");
                for (int i = 0; i < parts.size() && i < 12; ++i) {
                    int slot = kLabelMap[align][i];
                    if (!parts[i].isEmpty() && slot >= 0)
                        key.labels[size_t(slot)] = parts[i];
                }
                keys.append(key);

                current.x += current.width;
                current.width = current.height = 1;
                current.x2 = current.y2 = current.width2 = current.height2 = 0;
                current.decal = false;
                continue;
            }
            if (!item.isObject())
                continue;
            const QJsonObject o = item.toObject();
            if (k != 0 && (o.contains("r") || o.contains("rx") || o.contains("ry")))
                throw std::runtime_error("rotation can only be specified on the first key in a row");
            if (o.contains("r"))
                current.rotationAngle = o.value("r").toDouble();
            if (o.contains("rx")) {
                current.rotationX = clusterX = o.value("rx").toDouble();
                current.x = clusterX;
                current.y = clusterY;
            }
            if (o.contains("ry")) {
                current.rotationY = clusterY = o.value("ry").toDouble();
                current.x = clusterX;
                current.y = clusterY;
            }
            if (o.contains("a"))
                align = o.value("a").toInt();
            if (o.contains("x"))
                current.x += o.value("x").toDouble();
            if (o.contains("y"))
                current.y += o.value("y").toDouble();
            if (o.contains("w"))
                current.width = current.width2 = o.value("w").toDouble();
            if (o.contains("h"))
                current.height = current.height2 = o.value("h").toDouble();
            if (o.contains("x2"))
                current.x2 = o.value("x2").toDouble();
            if (o.contains("y2"))
                current.y2 = o.value("y2").toDouble();
            if (o.contains("w2"))
                current.width2 = o.value("w2").toDouble();
            if (o.contains("h2"))
                current.height2 = o.value("h2").toDouble();
            if (o.contains("d"))
                current.decal = o.value("d").toBool();
            // never reset: the Python parser carries it to the following keys too
            if (o.value("g").toBool())
                current.ghost = true;
        }
        current.y += 1;
        current.x = current.rotationX;
    }
    return keys;
}

}
