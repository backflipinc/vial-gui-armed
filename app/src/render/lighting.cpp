#include "render/lighting.h"

#include <QColor>

#include <cmath>

namespace lighting {
namespace {

// Python's //
qint64 fdiv(qint64 a, qint64 b)
{
    qint64 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

int u8(qint64 x)
{
    return int(x & 0xFF);
}

// a float sine, like the Python previews (ties to even, like Python's round)
int sin8(qint64 x)
{
    return int(std::nearbyint(128 + 127 * std::sin(u8(x) * M_PI / 128)));
}

int cos8(qint64 x)
{
    return sin8(u8(x) + 64);
}

int scale8(qint64 i, qint64 scale)
{
    return int((i * scale) >> 8);
}

qint64 scale16by8(qint64 i, qint64 scale)
{
    return ((i & 0xFFFF) * scale) >> 8;
}

int atan2_8(double dy, double dx)
{
    return u8(qint64(std::atan2(dy, dx) * 128 / M_PI));
}

Rgb hsvRgb(qint64 h, qint64 s, qint64 v)
{
    QColor c = QColor::fromHsv(int(u8(h) * 359 / 255.0), int(std::clamp<qint64>(s, 0, 255)),
                               int(std::clamp<qint64>(v, 0, 255)));
    return {quint8(c.red()), quint8(c.green()), quint8(c.blue())};
}

Rgb lerpRgb(const Rgb &a, const Rgb &b, int t)
{
    Rgb out;
    for (int k = 0; k < 3; ++k)
        out[size_t(k)] = quint8(int(a[size_t(k)] + (b[size_t(k)] - a[size_t(k)]) * t / 255.0));
    return out;
}

QString effectKey(const QString &name)
{
    return name.trimmed().toUpper().replace(QLatin1Char(' '), QLatin1Char('_'));
}

const QSet<QString> kRainbow = {"CYCLE_ALL", "CYCLE_LEFT_RIGHT", "CYCLE_UP_DOWN", "RAINBOW_MOVING_CHEVRON",
                                "CYCLE_OUT_IN", "CYCLE_PINWHEEL", "CYCLE_SPIRAL", "RAINBOW_BEACON", "DIGITAL_RAIN",
                                "TYPING_HEATMAP"};
const QSet<QString> kTwoColor = {"TWO_TONE", "GRADIENT_2", "CROSSFADE_2", "SCANNER_2", "STARLIGHT_2", "REACTIVE_2",
                                 "RIPPLE_2"};
const QSet<QString> kReactive = {"SOLID_REACTIVE_SIMPLE", "SOLID_REACTIVE", "SOLID_REACTIVE_WIDE",
                                 "SOLID_REACTIVE_MULTIWIDE", "SOLID_REACTIVE_CROSS", "SOLID_REACTIVE_MULTICROSS",
                                 "SOLID_REACTIVE_NEXUS", "SOLID_REACTIVE_MULTINEXUS", "SPLASH", "MULTISPLASH",
                                 "SOLID_SPLASH", "SOLID_MULTISPLASH", "TYPING_HEATMAP", "REACTIVE_2", "RIPPLE_2"};

}

int sqrt16(qint64 x)
{
    return std::min(255, int(std::sqrt(double(std::max<qint64>(0, x)))));
}

int qadd8(int a, int b)
{
    return std::min(255, a + b);
}

int qsub8(int a, int b)
{
    return std::max(0, a - b);
}

// ---- effects

EffectPreview::EffectPreview(QList<armed::Led> leds)
    : leds_(std::move(leds))
{
    reset();
}

void EffectPreview::reset()
{
    const int n = int(leds_.size());
    buf_ = QList<int>(n, 0);
    star_ = QList<int>(n, 0);
    drops_ = QList<int>(n, 0);
    rainColors_.reset();
    lastT_.reset();
    lastDrop_ = lastDecay_ = 0;
}

void EffectPreview::setEffect(const QString &name)
{
    QString key = effectKey(name);
    if (key != effect) {
        effect = key;
        reset();
    }
}

bool EffectPreview::reactive() const
{
    return kReactive.contains(effect);
}

void EffectPreview::hit(int idx, qint64 t)
{
    const armed::Led &led = leds_[idx];
    hits_.append({idx, led.x, led.y, t});
    if (hits_.size() > HITS_TO_REMEMBER)
        hits_ = hits_.mid(hits_.size() - HITS_TO_REMEMBER);
    for (int j = 0; j < leds_.size(); ++j) {
        if (j == idx) {
            buf_[j] = qadd8(buf_[j], 32);
        } else {
            int dx = led.x - leds_[j].x, dy = led.y - leds_[j].y;
            int d = sqrt16(dx * dx + dy * dy);
            if (d <= 40)
                buf_[j] = qadd8(buf_[j], std::min(16, 40 - d));
        }
    }
}

Rgb EffectPreview::c1() const
{
    return color1Black ? Rgb{0, 0, 0} : hsvRgb(h, s, v);
}

Rgb EffectPreview::c2() const
{
    return color2Black ? Rgb{0, 0, 0} : hsvRgb(color2[0], color2[1], v);
}

QList<Rgb> EffectPreview::runI(qint64 t, const std::function<Hsv(int, const armed::Led &, int)> &fn) const
{
    int time = u8(scale16by8(t, qadd8(speed / 4, 1)));
    QList<Rgb> out;
    for (int i = 0; i < leds_.size(); ++i) {
        Hsv c = fn(i, leds_[i], time);
        out.append(hsvRgb(c[0], c[1], c[2]));
    }
    return out;
}

QList<Rgb> EffectPreview::runDist(qint64 t, const std::function<Hsv(int, int, int, int)> &fn, int speedDiv) const
{
    int time = u8(scale16by8(t, speed / speedDiv));
    QList<Rgb> out;
    for (const auto &led : leds_) {
        int dx = led.x - CENTER_X, dy = led.y - CENTER_Y;
        Hsv c = fn(dx, dy, sqrt16(dx * dx + dy * dy), time);
        out.append(hsvRgb(c[0], c[1], c[2]));
    }
    return out;
}

QList<std::array<qint64, 4>> EffectPreview::hitTicks(qint64 t) const
{
    QList<std::array<qint64, 4>> out;
    for (const Hit &h : hits_)
        out.append({h.idx, h.x, h.y, scale16by8(t - h.t, qadd8(speed, 1))});
    return out;
}

QList<Rgb> EffectPreview::runReactive(qint64 t, const std::function<Hsv(int)> &fn) const
{
    qint64 maxTick = 65535 / qadd8(speed, 1);
    QList<Rgb> out;
    for (int i = 0; i < leds_.size(); ++i) {
        qint64 tick = maxTick;
        for (auto it = hits_.crbegin(); it != hits_.crend(); ++it) {
            if (it->idx == i && t - it->t < tick) {
                tick = t - it->t;
                break;
            }
        }
        Hsv c = fn(int(scale16by8(std::min<qint64>(tick, 65535), qadd8(speed, 1))));
        out.append(hsvRgb(c[0], c[1], c[2]));
    }
    return out;
}

QList<Rgb> EffectPreview::runSplash(qint64 t, const std::function<Hsv(int, int, int, int, int, int, int)> &fn,
                                    bool multi) const
{
    auto ticks = hitTicks(t);
    if (!multi && !ticks.isEmpty())
        ticks = ticks.mid(ticks.size() - 1);
    QList<Rgb> out;
    for (const auto &led : leds_) {
        int hh = h, ss = s, vv = 0;
        for (const auto &tk : ticks) {
            int dx = led.x - int(tk[1]), dy = led.y - int(tk[2]);
            Hsv c = fn(hh, ss, vv, dx, dy, sqrt16(dx * dx + dy * dy), int(tk[3]));
            hh = c[0];
            ss = c[1];
            vv = c[2];
        }
        out.append(hsvRgb(hh, ss, scale8(vv, v)));
    }
    return out;
}

QList<Rgb> EffectPreview::frame(qint64 t)
{
    qint64 dt = lastT_ ? std::max<qint64>(0, t - *lastT_) : 0;
    lastT_ = t;
    const int n = int(leds_.size());
    QList<Rgb> out;
    const QString &e = effect;

    auto wide = [](int h, int s, int v, int, int, int dist, int tick) -> Hsv {
        return {h, s, qadd8(v, 255 - std::min(255, tick + dist * 5))};
    };
    auto cross = [](int h, int s, int v, int dx, int dy, int dist, int tick) -> Hsv {
        int effect = tick + dist;
        effect += std::min(std::min(255, std::abs(dx) * 16), std::min(255, std::abs(dy) * 16));
        return {h, s, qadd8(v, 255 - std::min(255, effect))};
    };
    auto nexus = [this](int, int s, int v, int dx, int dy, int dist, int tick) -> Hsv {
        int effect = tick - dist;
        if (effect < 0 || effect > 255 || dist > 72 || ((dx > 8 || dx < -8) && (dy > 8 || dy < -8)))
            effect = 255;
        return {int(h + fdiv(dy, 4)), s, qadd8(v, 255 - effect)};
    };
    auto splash = [](int h, int s, int v, int, int, int dist, int tick) -> Hsv {
        int effect = tick - dist;
        if (effect < 0 || effect > 255)
            effect = 255;
        return {h + effect, s, qadd8(v, 255 - effect)};
    };
    auto solidSplash = [](int h, int s, int v, int, int, int dist, int tick) -> Hsv {
        int effect = tick - dist;
        if (effect < 0 || effect > 255)
            effect = 255;
        return {h, s, qadd8(v, 255 - effect)};
    };

    if (e == "BREATHING") {
        out = runI(t, [this](int, const armed::Led &, int time) -> Hsv {
            return {h, s, scale8(std::abs(sin8(time / 2) - 128) * 2, v)};
        });
    } else if (e == "BAND_SAT") {
        out = runI(t, [this](int, const armed::Led &led, int time) -> Hsv {
            int sat = s - std::abs(scale8(led.x, 228) + 28 - time) * 8;
            return {h, scale8(std::max(0, sat), s), v};
        });
    } else if (e == "BAND_VAL") {
        out = runI(t, [this](int, const armed::Led &led, int time) -> Hsv {
            int val = v - std::abs(scale8(led.x, 228) + 28 - time) * 8;
            return {h, s, scale8(std::max(0, val), v)};
        });
    } else if (e == "CYCLE_ALL") {
        out = runI(t, [this](int, const armed::Led &, int time) -> Hsv { return {time, s, v}; });
    } else if (e == "CYCLE_LEFT_RIGHT") {
        out = runI(t, [this](int, const armed::Led &led, int time) -> Hsv { return {led.x - time, s, v}; });
    } else if (e == "CYCLE_UP_DOWN") {
        out = runI(t, [this](int, const armed::Led &led, int time) -> Hsv { return {led.y - time, s, v}; });
    } else if (e == "RAINBOW_MOVING_CHEVRON") {
        out = runI(t, [this](int, const armed::Led &led, int time) -> Hsv {
            return {h + std::abs(led.y - CENTER_Y) + led.x - time, s, v};
        });
    } else if (e == "CYCLE_OUT_IN") {
        out = runDist(t, [this](int, int, int dist, int time) -> Hsv { return {3 * dist / 2 + time, s, v}; });
    } else if (e == "CYCLE_PINWHEEL") {
        out = runDist(t, [this](int dx, int dy, int, int time) -> Hsv { return {atan2_8(dy, dx) + time, s, v}; });
    } else if (e == "CYCLE_SPIRAL") {
        out = runDist(t, [this](int dx, int dy, int dist, int time) -> Hsv {
            return {dist - time - atan2_8(dy, dx), s, v};
        });
    } else if (e == "RAINBOW_BEACON") {
        qint64 time = scale16by8(t, speed / 4);
        int cv = cos8(time) - 128, sv = sin8(time) - 128;
        for (const auto &led : leds_)
            out.append(hsvRgb(h + fdiv((led.y - CENTER_Y) * 2 * sv + (led.x - CENTER_X) * 2 * cv, 128), s, v));
    } else if (e == "HUE_BREATHING") {
        out = runI(t, [this](int, const armed::Led &, int time) -> Hsv {
            return {h + scale8(std::abs(sin8(time / 2) - 128) * 2, 12), s, v};
        });
    } else if (e == "HUE_PENDULUM") {
        out = runI(t, [this](int, const armed::Led &led, int time) -> Hsv {
            return {h + scale8(std::abs(sin8(time) + led.x - 128) * 2, 12), s, v};
        });
    } else if (e == "HUE_WAVE") {
        out = runI(t, [this](int, const armed::Led &led, int time) -> Hsv {
            return {h + scale8(std::abs(led.x - time), 24), s, v};
        });
    } else if (e == "RAINDROPS") {
        auto drop = [this] { return hsvRgb(h + 32 * int(rng_() % 3), s, v); };
        if (!rainColors_) {
            rainColors_ = QList<Rgb>();
            for (int i = 0; i < n; ++i)
                rainColors_->append(drop());
        }
        if (t - lastDrop_ > std::max(20, 400 - speed) && n) {
            lastDrop_ = t;
            (*rainColors_)[int(rng_() % n)] = drop();
        }
        out = *rainColors_;
    } else if (e == "SOLID_REACTIVE_SIMPLE") {
        out = runReactive(t, [this](int off) -> Hsv { return {h, s, scale8(255 - std::min(off, 255), v)}; });
    } else if (e == "SOLID_REACTIVE") {
        out = runReactive(t, [this](int off) -> Hsv { return {h + scale8(255 - std::min(off, 255), 64), s, v}; });
    } else if (e == "SOLID_REACTIVE_WIDE" || e == "SOLID_REACTIVE_MULTIWIDE") {
        out = runSplash(t, wide, e.contains("MULTI"));
    } else if (e == "SOLID_REACTIVE_CROSS" || e == "SOLID_REACTIVE_MULTICROSS") {
        out = runSplash(t, cross, e.contains("MULTI"));
    } else if (e == "SOLID_REACTIVE_NEXUS" || e == "SOLID_REACTIVE_MULTINEXUS") {
        out = runSplash(t, nexus, e.contains("MULTI"));
    } else if (e == "SPLASH" || e == "MULTISPLASH") {
        out = runSplash(t, splash, e == "MULTISPLASH");
    } else if (e == "SOLID_SPLASH" || e == "SOLID_MULTISPLASH") {
        out = runSplash(t, solidSplash, e == "SOLID_MULTISPLASH");
    } else if (e == "DIGITAL_RAIN") {
        QMap<int, QList<int>> rows;
        for (int i = 0; i < n; ++i)
            rows[leds_[i].row].append(i);
        if (t - lastDrop_ > 90) {
            lastDrop_ = t;
            QList<int> order(n);
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [this](int a, int b) { return leds_[a].row > leds_[b].row; });
            for (int i : order) {
                if (drops_[i] == 255) {
                    drops_[i] = 200;
                    for (int j : rows.value(leds_[i].row + 1)) {
                        if (std::abs(leds_[j].x - leds_[i].x) < 8) {
                            drops_[j] = 255;
                            break;
                        }
                    }
                }
            }
            QList<int> top = rows.isEmpty() ? QList<int>() : rows.first();
            if (!top.isEmpty() && rng_() % 2 == 0)
                drops_[top[int(rng_() % top.size())]] = 255;
        }
        for (int i = 0; i < n; ++i) {
            int d = drops_[i];
            if (d && d != 255)
                drops_[i] = std::max<qint64>(0, d - std::max<qint64>(1, dt / 6));
            int g = scale8(std::min(d, 255), v);
            out.append(d == 255 ? Rgb{quint8(scale8(g, 160)), quint8(g), quint8(scale8(g, 160))}
                                : Rgb{0, quint8(g), 0});
        }
    } else if (e == "TYPING_HEATMAP") {
        if (t - lastDecay_ >= 25) {
            int steps = int((t - lastDecay_) / 25);
            lastDecay_ = t;
            for (int &b : buf_)
                b = qsub8(b, steps);
        }
        for (int b : buf_)
            out.append(hsvRgb(170 - qsub8(b, 85), s, scale8((qadd8(170, b) - 170) * 3, v)));
    } else if (e == "TWO_TONE") {
        Rgb a = c1(), b = c2();
        for (const auto &led : leds_)
            out.append(led.modifier() ? b : a);
    } else if (e == "GRADIENT_2") {
        Rgb a = c1(), b = c2();
        qint64 shift = scale16by8(t, speed / 8 + 1);
        for (const auto &led : leds_) {
            int pos = u8(led.x * 255 / 224 + shift);
            out.append(lerpRgb(a, b, pos < 128 ? pos * 2 : (255 - pos) * 2));
        }
    } else if (e == "CROSSFADE_2") {
        Rgb c = lerpRgb(c1(), c2(), sin8(scale16by8(t, speed / 4 + 1)));
        out = QList<Rgb>(n, c);
    } else if (e == "SCANNER_2") {
        Rgb a = c1(), b = c2();
        int ph = u8(scale16by8(t, speed / 4 + 1));
        int tri = ph < 128 ? ph * 2 : (255 - ph) * 2;
        int pos = tri * 224 / 255;
        for (const auto &led : leds_) {
            int d = std::abs(led.x - pos);
            out.append(lerpRgb(a, b, d >= 48 ? 0 : 255 - d * 255 / 48));
        }
    } else if (e == "STARLIGHT_2") {
        Rgb a = c1(), b = c2();
        unsigned spark = unsigned(speed / 32 + 1);
        for (qint64 k = 0; k < std::max<qint64>(1, dt / 16); ++k) {
            for (int i = 0; i < n; ++i) {
                if (star_[i])
                    star_[i] = qsub8(star_[i], 3);
                else if (rng_() % 256 < spark && rng_() % 256 < 64)
                    star_[i] = 255;
            }
        }
        for (int st : star_)
            out.append(lerpRgb(a, b, st));
    } else if (e == "REACTIVE_2") {
        Rgb a = c1(), b = c2();
        qint64 maxTick = 65535 / qadd8(speed, 1);
        for (int i = 0; i < n; ++i) {
            qint64 tick = maxTick;
            for (auto it = hits_.crbegin(); it != hits_.crend(); ++it) {
                if (it->idx == i && t - it->t < tick) {
                    tick = t - it->t;
                    break;
                }
            }
            qint64 offset = scale16by8(std::min<qint64>(tick, 65535), qadd8(speed, 1));
            out.append(lerpRgb(a, b, offset > 255 ? 0 : int(255 - offset)));
        }
    } else if (e == "RIPPLE_2") {
        Rgb a = c1(), b = c2();
        auto ticks = hitTicks(t);
        for (const auto &led : leds_) {
            int tt = 0;
            for (const auto &tk : ticks) {
                qint64 tick = tk[3];
                if (tick > 255 + 64)
                    continue;
                int dx = led.x - int(tk[1]), dy = led.y - int(tk[2]);
                int dist = sqrt16(dx * dx + dy * dy);
                qint64 d = std::abs(tick - dist);
                int fade = tick > 255 ? 0 : int(255 - tick);
                if (d < 24)
                    tt = qadd8(tt, scale8(255 - d * 10, fade));
            }
            out.append(lerpRgb(a, b, tt));
        }
    } else {
        out = QList<Rgb>(n, hsvRgb(h, s, v));
    }
    if (color1Black && !kRainbow.contains(e) && !kTwoColor.contains(e))
        out = QList<Rgb>(n, Rgb{0, 0, 0});
    return out;
}

// ---- light bar

Rgb StripPreview::base(int v) const
{
    return colorBlack ? Rgb{0, 0, 0} : hsvRgb(color[0], color[1], v);
}

QList<Rgb> StripPreview::frame(qint64 t) const
{
    auto dim = [](const Rgb &c, int k) {
        return Rgb{quint8(scale8(c[0], k)), quint8(scale8(c[1], k)), quint8(scale8(c[2], k))};
    };
    auto bar = [&](qint64 n256, const Rgb &c) {
        QList<Rgb> out;
        for (int i = 0; i < LEDS; ++i) {
            qint64 lo = i * 256;
            int k = n256 >= lo + 256 ? 255 : (n256 > lo ? int(n256 - lo) : 0);
            out.append(dim(c, k));
        }
        return out;
    };
    if (caps)
        return QList<Rgb>(LEDS, base());
    const QString m = effectKey(mode);
    int wpm = int(70 + 60 * std::sin(double(t) / 2300.0));
    if (m == "WPM")
        return QList<Rgb>(LEDS, dim(base(), std::min(255, wpm * 255 / 200)));
    if (m == "WPM_BAR")
        return bar(qint64(std::min(wpm, 108)) * LEDS * 256 / 108, base());
    if (m == "BATTERY")
        return bar(80 * LEDS * 256 / 100, hsvRgb(85, 255, 255));
    if (m == "LINK")
        return QList<Rgb>(LEDS, hsvRgb(128, 255, 160));
    if (m == "SCANNER") {
        qint64 ph = (t / 3) % 512;
        qint64 pos = ph < 256 ? ph : 511 - ph;
        qint64 x = pos * (LEDS - 1) * 16 / 255;
        QList<Rgb> out;
        for (int i = 0; i < LEDS; ++i) {
            qint64 d = std::abs(i * 16 - x);
            out.append(dim(base(), d >= 24 ? 0 : int(255 - d * 255 / 24)));
        }
        return out;
    }
    return QList<Rgb>(LEDS, Rgb{0, 0, 0});
}

// ---- themes (armed_theme.c)

namespace {

constexpr int FRAME_MS = 16;
constexpr int B_M16[] = {0, 49, 49, 41, 90, 27, 117, 10};

int lerp8(int a, int b, int t)
{
    return a + (b - a) * t / 255;
}

Rgb lerp8Rgb(const Rgb &a, const Rgb &b, int t)
{
    return {quint8(lerp8(a[0], b[0], t)), quint8(lerp8(a[1], b[1], t)), quint8(lerp8(a[2], b[2], t))};
}

int tri8(int x)
{
    return x < 128 ? x * 2 : (255 - x) * 2;
}

int dist8(int dx, int dy)
{
    return sqrt16(qint64(dx) * dx + qint64(dy) * dy);
}

int cos8Exact(int theta)
{
    return sin8Exact(theta + 64);
}

}

int sin8Exact(int theta)
{
    theta &= 0xFF;
    int offset = (theta & 0x40) ? 255 - theta : theta;
    offset &= 0x3F;
    int sec = offset & 0x0F;
    if (theta & 0x40)
        ++sec;
    int s2 = (offset >> 4) * 2;
    int y = ((B_M16[s2 + 1] * sec) >> 4) + B_M16[s2];
    y = ((y + 128) & 0xFF) - 128;
    if (theta & 0x80)
        y = -y;
    return (y + 128) & 0xFF;
}

Rgb hsvToRgbQmk(int h, int s, int v)
{
    if (s == 0)
        return {quint8(v), quint8(v), quint8(v)};
    int region = h * 6 / 255;
    int rem = ((h * 2 - region * 85) * 3) & 0xFF;
    int p = (v * (255 - s)) >> 8;
    int q = (v * (255 - ((s * rem) >> 8))) >> 8;
    int t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
    switch (region) {
    case 1: return {quint8(q), quint8(v), quint8(p)};
    case 2: return {quint8(p), quint8(v), quint8(t)};
    case 3: return {quint8(p), quint8(q), quint8(v)};
    case 4: return {quint8(t), quint8(p), quint8(v)};
    case 5: return {quint8(v), quint8(p), quint8(q)};
    default: return {quint8(v), quint8(t), quint8(p)};
    }
}

ThemeRenderer::ThemeRenderer(QList<armed::Led> leds)
    : leds_(std::move(leds))
{
}

void ThemeRenderer::hit(int idx, qint64 t)
{
    hits_.append({idx, leds_[idx].x, leds_[idx].y, t});
    if (hits_.size() > HITS_TO_REMEMBER)
        hits_ = hits_.mid(hits_.size() - HITS_TO_REMEMBER);
}

bool ThemeRenderer::condition(int cond) const
{
    int n = cond & 0x0F, kind = cond & 0xF0;
    if (kind == 0)
        return cond == theme::C_CAPS ? caps : (cond == theme::C_NO_CAPS ? !caps : true);
    int top = layersOn.isEmpty() ? 0 : *std::max_element(layersOn.begin(), layersOn.end());
    // the active layer, as quantum/armed/armed_theme.c condition()
    if (kind == theme::C_LAYER)
        return top == n;
    if (kind == theme::C_NOT_LAYER)
        return top != n;
    return true;
}

ThemeRenderer::Frame ThemeRenderer::setup(const theme::Layer &l, qint64 t)
{
    Frame f;
    f.phase = int((t * l.speed >> 8) & 0xFF);
    if (l.flags & theme::F_RADIAL) {
        f.maxd = std::max({1, dist8(l.cx, l.cy), dist8(224 - l.cx, l.cy), dist8(l.cx, 64 - l.cy),
                           dist8(224 - l.cx, 64 - l.cy)});
    } else {
        f.c = cos8Exact(l.dir) - 128;
        f.s = sin8Exact(l.dir) - 128;
        f.e = std::max(1, 112 * std::abs(f.c) + 32 * std::abs(f.s));
    }
    return f;
}

int ThemeRenderer::position(const theme::Layer &l, const Frame &f, int x, int y)
{
    qint64 p;
    if (l.flags & theme::F_RADIAL) {
        p = qint64(dist8(x - l.cx, y - l.cy)) * 255 / f.maxd;
    } else {
        qint64 num = (qint64(x - 112) * f.c + qint64(y - 32) * f.s + f.e) * 255;
        p = num / (2 * f.e);
    }
    p = std::clamp<qint64>(p, 0, 255);
    return int((l.flags & theme::F_REVERSE) ? 255 - p : p);
}

int ThemeRenderer::spread(const theme::Layer &l, int pos)
{
    return l.size ? (pos * 128 / l.size) & 0xFF : 0;
}

QPair<Rgb, int> ThemeRenderer::color(const theme::Theme &th, int li, const theme::Layer &l, const Frame &f, int i,
                                     const armed::Led &led, qint64 t, qint64 steps)
{
    using namespace theme;
    switch (l.type) {
    case SOLID: return {l.a, 255};
    case GRADIENT: {
        int k = (spread(l, position(l, f, led.x, led.y)) - f.phase) & 0xFF;
        if (l.flags & F_MIRROR)
            k = tri8(k);
        return {lerp8Rgb(l.a, l.b, k), 255};
    }
    case RAINBOW: {
        int hue = (spread(l, position(l, f, led.x, led.y)) - f.phase) & 0xFF;
        return {hsvToRgbQmk(hue, l.param, 255), 255};
    }
    case PULSE: {
        int k = sin8Exact((f.phase - spread(l, position(l, f, led.x, led.y))) & 0xFF);
        return {lerp8Rgb(l.a, l.b, k), 255};
    }
    case WAVE: {
        int pos = position(l, f, led.x, led.y);
        int center = (l.flags & F_MIRROR) ? tri8(f.phase) : f.phase;
        int d = std::abs(pos - center);
        if (!(l.flags & F_MIRROR) && d > 128)
            d = 256 - d;
        int w = l.size / 2 + 1;
        return {l.a, d >= w ? 0 : 255 - d * 255 / w};
    }
    case SPARKLE: {
        qint64 key = qint64(li) << 16 | i;
        auto [lv, mix] = spark_.value(key, {0, 0});
        for (qint64 k = 0; k < std::min<qint64>(steps, 32); ++k) {
            if (lv)
                lv = qsub8(lv, 1 + (255 - l.size) / 16);
            else if (int(rng_() % 65536) < (l.speed + 1) * 4) {
                lv = 255;
                mix = int(rng_() % 256);
            }
        }
        spark_.insert(key, {lv, mix});
        return {lerp8Rgb(l.a, l.b, mix), lv};
    }
    case REACTIVE: return {l.a, reactive(l, i, led, t)};
    case RIPPLE: return {l.a, ripple(l, led, t)};
    case PAINT: return {th.paint.value(i, Rgb{0, 0, 0}), 255};
    }
    return {Rgb{0, 0, 0}, 0};
}

int ThemeRenderer::reactive(const theme::Layer &l, int i, const armed::Led &led, qint64 t) const
{
    int fade = 100 + l.size * 8;
    int best = 0;
    for (const Hit &h : hits_) {
        qint64 tick = std::min<qint64>(65535, t - h.t);
        if (tick >= fade)
            continue;
        int lvl = int(255 - tick * 255 / fade);
        if (l.param == 0 || l.param == 255) {
            if (h.idx != i)
                continue;
        } else {
            int d = dist8(led.x - h.x, led.y - h.y);
            if (d > l.param)
                continue;
            lvl = lvl * (l.param + 1 - d) / (l.param + 1);
        }
        best = std::max(best, lvl);
    }
    return best;
}

int ThemeRenderer::ripple(const theme::Layer &l, const armed::Led &led, qint64 t) const
{
    int w = 4 + l.size / 8;
    int best = 0;
    for (const Hit &h : hits_) {
        qint64 r = (std::min<qint64>(65535, t - h.t) * (l.speed + 8)) >> 8;
        if (r >= 255)
            continue;
        qint64 d = std::abs(dist8(led.x - h.x, led.y - h.y) - r);
        if (d >= w)
            continue;
        best = std::max(best, int((255 - d * 255 / w) * (255 - r) / 255));
    }
    return best;
}

QList<Rgb> ThemeRenderer::frame(const theme::Theme &th, qint64 t, int v)
{
    qint64 steps = lastT_ ? std::max<qint64>(0, fdiv(t - *lastT_, FRAME_MS)) : 1;
    if (!lastT_ || steps)
        lastT_ = t;
    QList<std::optional<Frame>> frames;
    for (const auto &l : th.layers)
        frames.append(l.type && !(l.flags & theme::F_HIDDEN) && condition(l.cond) ? std::optional(setup(l, t))
                                                                                 : std::nullopt);
    QList<QSet<int>> covers;
    for (const auto &l : th.layers)
        covers.append(theme::coverage(l, int(leds_.size()), layerKeys));
    QList<Rgb> out;
    out.reserve(leds_.size());
    for (int i = 0; i < leds_.size(); ++i) {
        int o[3] = {0, 0, 0};
        for (int li = 0; li < th.layers.size(); ++li) {
            const auto &l = th.layers[li];
            if (!frames[li] || !covers[li].contains(i))
                continue;
            auto [c, a] = color(th, li, l, *frames[li], i, leds_[i], t, steps);
            a = a * l.opacity / 255;
            if (!a)
                continue;
            for (int k = 0; k < 3; ++k) {
                if (l.blend == theme::ADD)
                    o[k] = qadd8(o[k], c[size_t(k)] * a / 255);
                else if (l.blend == theme::MULTIPLY)
                    o[k] = lerp8(o[k], o[k] * c[size_t(k)] / 255, a);
                else
                    o[k] = lerp8(o[k], c[size_t(k)], a);
            }
        }
        out.append({quint8(o[0] * v / 255), quint8(o[1] * v / 255), quint8(o[2] * v / 255)});
    }
    return out;
}

}
