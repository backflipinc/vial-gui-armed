#pragma once

#include "model/kle.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

#include <variant>

// A keyboard's vial.json: matrix, physical layout and what each drawn key is
struct LayoutKey {
    enum class Role { None, Key, Encoder };

    kle::Key geo;
    Role role = Role::None;
    int row = -1, col = -1;         // Role::Key
    int encoder = -1, direction = -1; // Role::Encoder
    int layoutIndex = -1, layoutOption = -1;
};

struct Definition {
    QJsonObject json;
    QString name;
    int rows = 0, cols = 0;
    QList<LayoutKey> keys;      // matrix keys and decals
    QList<LayoutKey> encoders;
    QList<QPair<int, int>> matrix; // row, col of every matrix key, in layout order, no duplicates
    QList<int> encoderIndexes;     // in layout order, no duplicates
    int encoderCount = 0;
    QJsonValue layoutLabels;
    QJsonArray customKeycodes;
    QString lighting;
    QString midi;
    bool vibl = false;

    static std::variant<Definition, QString> parse(const QByteArray &json);
    static std::variant<Definition, QString> parse(const QJsonObject &json);
};
