#pragma once

#include "model/theme.h"
#include "protocol/armed.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <random>

// Previews of the keyboard's lighting: QMK RGB Matrix effects, the IO Type 84 Pro two-colour effects, the light
// bar, and qmk-armed themes (a port of armed_theme.c, kept in step with it)
namespace lighting {

using Rgb = theme::Rgb;

inline constexpr int CENTER_X = 112, CENTER_Y = 32;
inline constexpr int HITS_TO_REMEMBER = 8;

struct Hit {
    int idx, x, y;
    qint64 t;
};

int sqrt16(qint64 x);
int qadd8(int a, int b);
int qsub8(int a, int b);

class EffectPreview {
public:
    explicit EffectPreview(QList<armed::Led> leds);

    QString effect = "SOLID_COLOR";
    int h = 0, s = 255, v = 255, speed = 128;
    std::array<int, 2> color2{170, 255};
    bool color1Black = false, color2Black = false;

    void setEffect(const QString &name);
    bool reactive() const;
    void hit(int idx, qint64 t);
    QList<Rgb> frame(qint64 t);
    void reset();

private:
    using Hsv = std::array<int, 3>;
    Rgb c1() const;
    Rgb c2() const;
    QList<Rgb> runI(qint64 t, const std::function<Hsv(int, const armed::Led &, int)> &fn) const;
    QList<Rgb> runDist(qint64 t, const std::function<Hsv(int, int, int, int)> &fn, int speedDiv = 2) const;
    QList<Rgb> runReactive(qint64 t, const std::function<Hsv(int)> &fn) const;
    QList<Rgb> runSplash(qint64 t, const std::function<Hsv(int, int, int, int, int, int, int)> &fn, bool multi) const;
    QList<std::array<qint64, 4>> hitTicks(qint64 t) const;

    QList<armed::Led> leds_;
    QList<Hit> hits_;
    QList<int> buf_, star_, drops_;
    std::optional<QList<Rgb>> rainColors_;
    std::optional<qint64> lastT_;
    qint64 lastDrop_ = 0, lastDecay_ = 0;
    std::mt19937 rng_{12345};
};

// the 9-LED light bar under the screen (strip.c)
class StripPreview {
public:
    static constexpr int LEDS = 9;
    QString mode = "WPM";
    std::array<int, 2> color{0, 255};
    bool colorBlack = false, caps = false;

    QList<Rgb> frame(qint64 t) const;

private:
    Rgb base(int v = 255) const;
};

class ThemeRenderer {
public:
    explicit ThemeRenderer(QList<armed::Led> leds);

    bool caps = false;
    QSet<int> layersOn;
    // LEDs of keymap layer n, for layers that follow the keymap (theme::Layer::keys); empty: the mask is used
    std::function<QSet<int>(int n)> layerKeys;

    void hit(int idx, qint64 t);
    QList<Rgb> frame(const theme::Theme &th, qint64 t, int v = 255);

private:
    struct Frame {
        int phase = 0, maxd = 1, c = 0, s = 0, e = 1;
    };
    bool condition(int cond) const;
    static Frame setup(const theme::Layer &l, qint64 t);
    static int position(const theme::Layer &l, const Frame &f, int x, int y);
    static int spread(const theme::Layer &l, int pos);
    QPair<Rgb, int> color(const theme::Theme &th, int li, const theme::Layer &l, const Frame &f, int i,
                          const armed::Led &led, qint64 t, qint64 steps);
    int reactive(const theme::Layer &l, int i, const armed::Led &led, qint64 t) const;
    int ripple(const theme::Layer &l, const armed::Led &led, qint64 t) const;

    QList<armed::Led> leds_;
    QList<Hit> hits_;
    QHash<qint64, QPair<int, int>> spark_; // layer << 16 | led -> level, mix
    std::optional<qint64> lastT_;
    std::mt19937 rng_{54321};
};

// lib8tion's sin8_C, bit for bit (the theme renderer); the effect previews use a float sine like the Python app
int sin8Exact(int theta);
Rgb hsvToRgbQmk(int h, int s, int v);

}
