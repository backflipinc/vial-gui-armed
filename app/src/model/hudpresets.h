#pragma once

#include "model/hud.h"

#include <functional>

// Starting points for the HUD editor; host slots follow agent::Config::defaults() (H1 CPU load, H2 CPU temp, ...)
namespace hud {

struct Preset {
    QString name;
    std::function<Layout()> make; // empty: the keyboard's own default layout
};
const QList<Preset> &presets();

}
