#pragma once

#include "model/hud.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QString>

#include <array>
#include <memory>
#include <optional>

// Draws a HUD layout the way qmk-armed's quantum/armed/armed_hud.c does on the keyboard, pixel for pixel:
// the keyboard's own QFF fonts, Quantum Painter's colour maths and RGB565.
namespace hud {

using Hsv = std::array<int, 3>;
using Rgb = std::array<quint8, 3>;

inline constexpr int GRAPH_POINTS = 64;
// the IO Type 84 Pro's palette with the amber accent, until the keyboard tells its own
extern const QList<Hsv> DEFAULT_PALETTE;

Rgb hsvToRgb(int h, int s, int v); // quantum/color.c hsv_to_rgb_nocie
Rgb panelRgb(const Hsv &hsv);      // what the ST7789 shows: through RGB565 and back

// Quantum Painter font (docs/quantum_painter_qff.md): ASCII table, greyscale, optional RLE
class QffFont {
public:
    static std::shared_ptr<QffFont> load(const QByteArray &data); // null if it isn't a usable QFF font

    int lineHeight() const { return lineHeight_; }
    int bpp() const { return bpp_; }
    std::optional<int> width(char32_t ch) const;
    int textWidth(const QString &s) const; // 0 if any glyph is missing, like qp_textwidth
    struct Glyph {
        int width = 0;
        QByteArray px; // palette index per pixel, row by row
    };
    const Glyph &glyph(char32_t ch) const;

private:
    QByteArray data_;
    int lineHeight_ = 0, bpp_ = 1, dataStart_ = 0;
    bool compressed_ = false;
    QHash<char32_t, QPair<int, int>> table_; // width, offset
    mutable QHash<char32_t, Glyph> cache_;
};

// armed_hud_value_t
struct Value {
    qint64 num = 0;
    int dec = 0;
    int hint = C_TEXT;
    bool valid = true;
    QString text;
};

QString valueText(const Value &v, int digits);

class Sources {
public:
    virtual ~Sources() = default;
    virtual std::optional<Value> value(int source) const = 0; // nothing: the keyboard doesn't have it
    virtual std::optional<QString> hostText(int slot) const { Q_UNUSED(slot); return std::nullopt; }
    virtual QList<Value> history(int source) const { Q_UNUSED(source); return {}; } // oldest first, 1 per second
};

struct Box {
    int x0, y0, x1, y1;
    bool operator==(const Box &) const = default;
};

class Renderer {
public:
    Renderer(int width, int height, QList<std::shared_ptr<QffFont>> fonts, QList<Hsv> palette, const Sources &sources);

    QHash<int, QString> names; // source id -> name, for indicator labels
    std::optional<Box> area(const Widget &wd) const;
    QImage render(const Layout &layout);

private:
    struct Canvas;

    const QffFont *font(int idx) const;
    std::optional<Value> value(int source) const;
    void draw(const Widget &wd, const std::optional<Value> &v, bool clear);
    QString fit(const QffFont &f, QString s, int width, int *outWidth) const;
    int aligned(const Widget &wd, const Box &b, int width) const;
    int fgRole(const Widget &wd, const std::optional<Value> &v) const;
    QString expand(const Widget &wd, const std::optional<Value> &v) const;
    int level(const Widget &wd, const Value &v) const;

    void drawText(const Widget &wd, const Box &b, const std::optional<Value> &v);
    void drawBar(const Widget &wd, const Box &b, const std::optional<Value> &v);
    void drawGraph(const Widget &wd, const Box &b, const std::optional<Value> &v);
    void drawIndicator(const Widget &wd, const Box &b, const std::optional<Value> &v);
    void drawBattery(const Widget &wd, const Box &b, const std::optional<Value> &v);
    void drawFrame(const Widget &wd, const Box &b);
    void drawTextbox(const Widget &wd, const Box &b, const std::optional<Value> &v);

    // canvas
    Rgb role(int r) const;
    Hsv hsv(int r) const;
    void put(int x, int y, const Rgb &rgb);
    void fill(int x0, int y0, int x1, int y1, int role);
    void fillRgb(int x0, int y0, int x1, int y1, const Rgb &rgb);
    void outline(int x0, int y0, int x1, int y1, int role);
    void line(int x0, int y0, int x1, int y1, int role);
    void text(int x, int y, const QffFont *f, const QString &s, int fg, int bg);

    int w_, h_;
    QList<std::shared_ptr<QffFont>> fonts_;
    QList<Hsv> palette_;
    QList<Rgb> rgb_;
    const Sources &sources_;
    QImage img_;
};

}
