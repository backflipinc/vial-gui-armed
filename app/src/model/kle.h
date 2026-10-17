#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>

#include <array>

// keyboard-layout-editor.com JSON, the geometry Vial uses (port of ijprest/kle-serial as the Python app has it)
namespace kle {

struct Key {
    double x = 0, y = 0, width = 1, height = 1;
    double x2 = 0, y2 = 0, width2 = 1, height2 = 1;
    double rotationX = 0, rotationY = 0, rotationAngle = 0;
    bool decal = false, ghost = false;
    std::array<QString, 12> labels; // null: no label at that position
};

// throws std::runtime_error on a malformed layout
QList<Key> deserialize(const QJsonArray &rows);

}
