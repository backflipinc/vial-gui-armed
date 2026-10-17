#pragma once

#include <QHash>
#include <QString>

#include <functional>
#include <optional>

namespace keycodes {

// "Any keycode" expressions: LCTL(KC_A), LT(2, KC_SPC), MT(MOD_LCTL | MOD_LSFT, KC_ESC), 0x7C00 | 5 ...
// Integer subset of what the Python app evaluated with simpleeval.
std::optional<qint64> evaluate(const QString &expr, const QHash<QString, int> &names,
                               const std::function<std::optional<int>(const QString &)> &resolve);

}
