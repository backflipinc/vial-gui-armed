#pragma once

#include "protocol/armed.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <variant>

// qmk-armed lighting themes: the model, its wire format (docs/armed.md in qmk-armed), presets and key groups
namespace theme {

inline constexpr quint8 MAGIC = 0xA7;
inline constexpr int FORMAT = 1;
inline constexpr int NAME_LEN = 16;
inline constexpr int HEADER = 20;
inline constexpr int LAYER_HEAD = 20;

enum Type : quint8 { NONE, SOLID, GRADIENT, RAINBOW, PULSE, WAVE, SPARKLE, REACTIVE, RIPPLE, PAINT };
enum Blend : quint8 { NORMAL, ADD, MULTIPLY };
enum Flag : quint8 { F_RADIAL = 0x01, F_MIRROR = 0x02, F_REVERSE = 0x04, F_HIDDEN = 0x80 };
enum Cond : quint8 { C_ALWAYS = 0x00, C_CAPS = 0x01, C_NO_CAPS = 0x02, C_LAYER = 0x10, C_NOT_LAYER = 0x20 };

using Rgb = std::array<quint8, 3>;

struct LayerType {
    int id;
    QString name;
    QString hint;
    QSet<QString> controls; // a, b = colours; dir; speed; size; param; mirror; paint
};
const QList<LayerType> &types();
const LayerType *typeById(int id);
inline const QStringList kBlends = {"NORMAL", "ADD", "MULTIPLY"};

struct Layer {
    int type = SOLID, blend = NORMAL, opacity = 255, speed = 0, dir = 0, size = 128, flags = 0, cond = C_ALWAYS;
    Rgb a{255, 170, 0}, b{0, 0, 0};
    int param = 255, cx = 112, cy = 32;
    QSet<int> mask; // LED indexes

    static Layer make(int type, const QSet<int> &leds = {}); // with the type's defaults
    bool hidden() const { return flags & F_HIDDEN; }
    bool flag(int f) const { return flags & f; }
    void setFlag(int f, bool on) { flags = on ? (flags | f) : (flags & ~f); }
    QByteArray pack(int nLeds) const;
    static Layer unpack(const QByteArray &data, int nLeds);
    bool operator==(const Layer &o) const = default;
};

struct Theme {
    QString name = "THEME";
    QList<Layer> layers;
    QList<Rgb> paint;

    static Theme blank(const QString &name, int nLeds);
    QByteArray pack(int nLeds, int maxLayers) const;
    static Theme unpack(const QByteArray &data, int nLeds, int maxLayers);
    // the .json export; keys are named by matrix position, so they survive a different LED order
    QByteArray toJson(const QList<armed::Led> &leds) const;
    static std::variant<Theme, QString> fromJson(const QByteArray &json, const QList<armed::Led> &leds, int maxLayers);
    bool operator==(const Theme &o) const = default;
};

QString hexColor(const Rgb &c);
Rgb parseColor(const QString &s, const Rgb &fallback);
QString keyName(const QList<armed::Led> &leds, int i);

// keymap lookup: layer, row, col -> keycode name (null when the layout has no key there)
using KeymapFn = std::function<QString(int layer, int row, int col)>;

inline const QStringList kGroups = {"ALL", "LETTERS", "NUMBERS", "F-ROW", "MODS", "ARROWS", "WASD", "FN LAYER"};
QMap<QString, QSet<int>> keyGroups(const QList<armed::Led> &leds, const KeymapFn &keymap);

QStringList presetNames();
Theme buildPreset(int index, const QList<armed::Led> &leds, const KeymapFn &keymap);

}
