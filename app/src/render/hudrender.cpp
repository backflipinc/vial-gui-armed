#include "render/hudrender.h"

#include "core/bytes.h"

#include <cstring>

namespace hud {
namespace {

// C integer division: truncates toward zero
int cdiv(qint64 a, qint64 b)
{
    return int(a / b);
}

// Python's //
qint64 fdiv(qint64 a, qint64 b)
{
    qint64 q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

// qp_internal_interpolate_palette: index 0 = background, steps - 1 = foreground
QList<Hsv> interpolate(const Hsv &fg, const Hsv &bg, int steps)
{
    int hf = fg[0], hb = bg[0];
    if (hf - hb >= 128)
        hb += 256;
    else if (hf - hb <= -128)
        hb -= 256;
    QList<Hsv> out;
    for (int i = 0; i < steps; ++i)
        out.append({(cdiv(qint64(hf - hb) * i, steps - 1) + hb) & 0xFF,
                    (cdiv(qint64(fg[1] - bg[1]) * i, steps - 1) + bg[1]) & 0xFF,
                    (cdiv(qint64(fg[2] - bg[2]) * i, steps - 1) + bg[2]) & 0xFF});
    return out;
}

QString formatNum(const Value &v, int digits)
{
    qint64 a = std::abs(v.num);
    qint64 p = 1;
    for (int i = 0; i < v.dec; ++i)
        p *= 10;
    QString s = (v.num < 0 ? QStringLiteral("-") : QString())
        + QString::number(a / p).rightJustified(std::max(1, digits), QLatin1Char('0'));
    if (v.dec)
        s += QLatin1Char('.') + QString::number(a % p).rightJustified(v.dec, QLatin1Char('0'));
    return s;
}

// Python slicing by code points
QString head(const QString &s, int n)
{
    QList<uint> u = s.toUcs4();
    if (u.size() <= n)
        return s;
    u.resize(n);
    return QString::fromUcs4(reinterpret_cast<const char32_t *>(u.constData()), n);
}

}

const QList<Hsv> DEFAULT_PALETTE = {{0, 0, 0}, {0, 0, 16}, {0, 0, 40}, {0, 0, 90}, {0, 0, 220}, {28, 255, 255},
                                    {85, 200, 200}, {0, 220, 230}};

Rgb hsvToRgb(int h, int s, int v)
{
    if (s == 0)
        return {quint8(v), quint8(v), quint8(v)};
    int region = h * 6 / 255;
    int remainder = ((h * 2 - region * 85) * 3) & 0xFF;
    int p = (v * (255 - s)) >> 8;
    int q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    int t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
    case 0:
    case 6: return {quint8(v), quint8(t), quint8(p)};
    case 1: return {quint8(q), quint8(v), quint8(p)};
    case 2: return {quint8(p), quint8(v), quint8(t)};
    case 3: return {quint8(p), quint8(q), quint8(v)};
    case 4: return {quint8(t), quint8(p), quint8(v)};
    default: return {quint8(v), quint8(p), quint8(q)};
    }
}

Rgb panelRgb(const Hsv &hsv)
{
    Rgb c = hsvToRgb(hsv[0], hsv[1], hsv[2]);
    int r = c[0] >> 3, g = c[1] >> 2, b = c[2] >> 3;
    return {quint8((r << 3) | (r >> 2)), quint8((g << 2) | (g >> 4)), quint8((b << 3) | (b >> 2))};
}

std::shared_ptr<QffFont> QffFont::load(const QByteArray &data)
{
    if (data.size() < 25 || data.mid(5, 3) != "QFF")
        return nullptr;
    auto f = std::make_shared<QffFont>();
    f->data_ = data;
    f->lineHeight_ = bytes::u8(data, 17);
    bool hasAscii = bytes::u8(data, 18);
    int numUnicode = bytes::le16(data, 19);
    int fmt = bytes::u8(data, 21);
    f->compressed_ = bytes::u8(data, 23) == 1;
    if (fmt > 3)
        return nullptr; // palette fonts are not supported
    f->bpp_ = 1 << fmt;
    int pos = 25;
    auto le24 = [&data](int o) { return int(bytes::u8(data, o) | bytes::u8(data, o + 1) << 8 | bytes::u8(data, o + 2) << 16); };
    if (hasAscii) {
        for (int i = 0; i < 95; ++i) {
            int v = le24(pos + 5 + i * 3);
            f->table_.insert(char32_t(0x20 + i), {v & 0x3F, v >> 6});
        }
        pos += 5 + 285;
    }
    if (numUnicode) {
        for (int i = 0; i < numUnicode; ++i) {
            int o = pos + 5 + i * 6;
            f->table_.insert(char32_t(le24(o)), {le24(o + 3) & 0x3F, le24(o + 3) >> 6});
        }
        pos += 5 + 6 * numUnicode;
    }
    f->dataStart_ = pos + 5;
    return f;
}

std::optional<int> QffFont::width(char32_t ch) const
{
    auto it = table_.find(ch);
    if (it == table_.end())
        return std::nullopt;
    return it->first;
}

int QffFont::textWidth(const QString &s) const
{
    int w = 0;
    for (char32_t ch : s.toUcs4()) {
        auto gw = width(ch);
        if (!gw)
            return 0;
        w += *gw;
    }
    return w;
}

const QffFont::Glyph &QffFont::glyph(char32_t ch) const
{
    auto cached = cache_.find(ch);
    if (cached != cache_.end())
        return *cached;
    auto [w, off] = table_.value(ch);
    int n = w * lineHeight_;
    int need = (n * bpp_ + 7) / 8;
    const int start = dataStart_ + off;
    QByteArray raw;
    if (compressed_) {
        int i = start;
        while (raw.size() < need && i < data_.size()) {
            int m = quint8(data_[i++]);
            if (m >= 128) { // qp_draw_codec.c reads m - 127 bytes, not m - 128 as the RLE doc says
                raw += data_.mid(i, m - 127);
                i += m - 127;
            } else {
                raw.append(m, i < data_.size() ? data_[i] : '\0');
                ++i;
            }
        }
    } else {
        raw = data_.mid(start, need);
    }
    Glyph g;
    g.width = w;
    g.px.resize(n);
    const int mask = (1 << bpp_) - 1, per = 8 / bpp_;
    for (int k = 0; k < n; ++k) {
        int byte = k / per < raw.size() ? quint8(raw[k / per]) : 0;
        g.px[k] = char((byte >> ((k % per) * bpp_)) & mask);
    }
    return *cache_.insert(ch, g);
}

QString valueText(const Value &v, int digits)
{
    if (!v.text.isEmpty())
        return head(v.text, 23);
    if (!v.valid)
        return QStringLiteral("--");
    return formatNum(v, digits);
}

Renderer::Renderer(int width, int height, QList<std::shared_ptr<QffFont>> fonts, QList<Hsv> palette,
                   const Sources &sources)
    : w_(width)
    , h_(height)
    , fonts_(std::move(fonts))
    , palette_(std::move(palette))
    , sources_(sources)
{
    for (const Hsv &c : palette_)
        rgb_.append(panelRgb(c));
}

const QffFont *Renderer::font(int idx) const
{
    if (idx >= 0 && idx < fonts_.size() && fonts_[idx])
        return fonts_[idx].get();
    for (const auto &f : fonts_)
        if (f)
            return f.get();
    return nullptr;
}

std::optional<Value> Renderer::value(int source) const
{
    if (source == 0)
        return std::nullopt;
    return sources_.value(source);
}

std::optional<Box> Renderer::area(const Widget &wd) const
{
    if (wd.x < 0 || wd.y < 0 || wd.x >= w_ || wd.y >= h_)
        return std::nullopt;
    const QffFont *f = font(wd.font);
    int w = wd.w > 0 ? wd.w : w_ - wd.x;
    int h = wd.h > 0 ? wd.h : (f ? f->lineHeight() : 14);
    return Box{wd.x, wd.y, std::min(wd.x + w, w_) - 1, std::min(wd.y + h, h_) - 1};
}

QImage Renderer::render(const Layout &layout)
{
    img_ = QImage(w_, h_, QImage::Format_RGB888);
    fill(0, 0, w_ - 1, h_ - 1, C_BG);
    for (const Widget &wd : layout.widgets) {
        if (wd.type == W_NONE)
            continue;
        if (wd.isStatic()) {
            draw(wd, std::nullopt, false);
            continue;
        }
        std::optional<Value> v = value(wd.source);
        if (!v)
            v = Value{0, 0, C_LINE, false, {}};
        draw(wd, v, wd.bg != C_BG);
    }
    return img_;
}

void Renderer::draw(const Widget &wd, const std::optional<Value> &v, bool clear)
{
    auto b = area(wd);
    if (!b)
        return;
    if (clear && wd.type != W_FRAME)
        fill(b->x0, b->y0, b->x1, b->y1, wd.bg);
    switch (wd.type) {
    case W_TEXT: drawText(wd, *b, v); break;
    case W_BAR: drawBar(wd, *b, v); break;
    case W_GRAPH: drawGraph(wd, *b, v); break;
    case W_INDICATOR: drawIndicator(wd, *b, v); break;
    case W_BATTERY: drawBattery(wd, *b, v); break;
    case W_FRAME: drawFrame(wd, *b); break;
    case W_TEXTBOX: drawTextbox(wd, *b, v); break;
    }
}

QString Renderer::fit(const QffFont &f, QString s, int width, int *outWidth) const
{
    int w = f.textWidth(s);
    while (!s.isEmpty() && w > width) {
        QList<uint> u = s.toUcs4();
        u.removeLast();
        s = QString::fromUcs4(reinterpret_cast<const char32_t *>(u.constData()), u.size());
        w = f.textWidth(s);
    }
    *outWidth = w;
    return s;
}

int Renderer::aligned(const Widget &wd, const Box &b, int width) const
{
    int a = wd.flags & 3;
    if (a == 1)
        return b.x0 + cdiv(b.x1 - b.x0 + 1 - width, 2);
    if (a == 2)
        return b.x1 + 1 - width;
    return b.x0;
}

int Renderer::fgRole(const Widget &wd, const std::optional<Value> &v) const
{
    int r = wd.fg;
    if (r == C_AUTO)
        r = v ? v->hint : C_TEXT;
    if (v && v->valid && (wd.flags & F_WARN)) {
        if ((wd.flags & F_WARN_BELOW) ? (v->num <= wd.warn) : (v->num >= wd.warn))
            r = C_WARN;
    }
    return r;
}

QString Renderer::expand(const Widget &wd, const std::optional<Value> &v) const
{
    QString val = v ? head(valueText(*v, wd.param & 0x0F), 23) : QString();
    QString label = wd.label ? *wd.label : QStringLiteral("{}");
    int p = label.indexOf(QLatin1String("{}"));
    if (p < 0)
        return head(label, 47);
    return head(label.left(p) + val + label.mid(p + 2), 47);
}

void Renderer::drawText(const Widget &wd, const Box &b, const std::optional<Value> &v)
{
    const QffFont *f = font(wd.font);
    if (!f)
        return;
    int w = 0;
    QString s = fit(*f, expand(wd, v), b.x1 - b.x0 + 1, &w);
    text(aligned(wd, b, w), b.y0, f, s, fgRole(wd, v), wd.bg);
}

int Renderer::level(const Widget &wd, const Value &v) const
{
    if (!v.valid)
        return 0;
    qint64 lo = wd.lo, hi = wd.hi;
    if (hi <= lo) {
        lo = 0;
        hi = 100;
        for (int i = 0; i < v.dec; ++i)
            hi *= 10;
    }
    qint64 r = cdiv((v.num - lo) * 1000, hi - lo);
    return int(std::clamp<qint64>(r, 0, 1000));
}

void Renderer::drawBar(const Widget &wd, const Box &b, const std::optional<Value> &v)
{
    outline(b.x0, b.y0, b.x1, b.y1, C_LINE);
    int x0 = b.x0 + 2, x1 = b.x1 - 2, y0 = b.y0 + 2, y1 = b.y1 - 2;
    int iw = x1 - x0 + 1;
    if (iw <= 0 || y1 < y0)
        return;
    int l = level(wd, *v);
    int fg = fgRole(wd, v);
    if (wd.param < 2) {
        fill(x0, y0, x0 + int(fdiv(qint64(iw) * l, 1000)) - 1, y1, fg);
        return;
    }
    int n = wd.param;
    qint64 on = fdiv(qint64(l) * n + 500, 1000);
    for (int i = 0; i < n; ++i) {
        int sx0 = x0 + int(fdiv(qint64(iw) * i, n)), sx1 = x0 + int(fdiv(qint64(iw) * (i + 1), n)) - 2;
        fill(sx0, y0, std::max(sx0, sx1), y1, i < on ? fg : C_LINE);
    }
}

void Renderer::drawGraph(const Widget &wd, const Box &b, const std::optional<Value> &v)
{
    int w = b.x1 - b.x0 + 1, h = b.y1 - b.y0;
    line(b.x0, b.y1, b.x1, b.y1, C_LINE);
    int pts = std::min(GRAPH_POINTS, int(fdiv(w, 2)));
    if (pts < 2)
        return;
    QList<int> samples;
    for (const Value &s : sources_.history(wd.source))
        samples.append(int(fdiv(qint64(level(wd, s)) * 255, 1000)));
    if (samples.size() > GRAPH_POINTS)
        samples = samples.mid(samples.size() - GRAPH_POINTS);
    int n = std::min(int(samples.size()), pts);
    samples = samples.mid(samples.size() - n);
    int fg = fgRole(wd, v);
    int px = 0, py = 0;
    for (int k = 0; k < samples.size(); ++k) {
        int x = b.x0 + int(fdiv(qint64(w - 1) * (pts - n + k), pts - 1));
        int y = b.y1 - 1 - int(fdiv(qint64(h - 1) * samples[k], 255));
        if (wd.flags & F_FILL) {
            int nx = k + 1 < n ? b.x0 + int(fdiv(qint64(w - 1) * (pts - n + k + 1), pts - 1)) - 1 : x;
            fill(x, y, std::max(x, nx), b.y1 - 1, fg);
        } else if (k) {
            line(px, py, x, y, fg);
        }
        px = x;
        py = y;
    }
}

void Renderer::drawIndicator(const Widget &wd, const Box &b, const std::optional<Value> &v)
{
    bool on = v && v->valid && v->num;
    if (wd.flags & F_DOT) {
        int c = wd.fg == C_AUTO ? (v ? v->hint : C_TEXT) : (on ? wd.fg : C_LINE);
        fill(b.x0, b.y0, b.x1, b.y1, c);
        return;
    }
    int bg = on ? C_PANEL : wd.bg;
    int dot = on ? (wd.fg == C_AUTO ? C_ACCENT : wd.fg) : C_LINE;
    int h = b.y1 - b.y0 + 1;
    fill(b.x0, b.y0, b.x1, b.y1, bg);
    outline(b.x0, b.y0, b.x1, b.y1, C_LINE);
    fill(b.x0 + 4, b.y0 + cdiv(h - 6, 2), b.x0 + 9, b.y0 + cdiv(h - 6, 2) + 5, dot);
    const QffFont *f = font(wd.font);
    if (!f)
        return;
    QString s = wd.label ? *wd.label : names.value(wd.source);
    int tw = 0;
    s = fit(*f, head(s, 23), b.x1 - b.x0 - 18, &tw);
    text(b.x0 + 16, b.y0 + cdiv(h - f->lineHeight() - 1, 2), f, s, on ? C_TEXT : C_DIM, bg);
}

void Renderer::drawBattery(const Widget &wd, const Box &b, const std::optional<Value> &v)
{
    auto c = sources_.value(S_CHARGING);
    bool chg = c && c->num;
    qint64 pct = v && v->valid ? v->num : 0;
    int fg = wd.fg == C_AUTO ? C_ACCENT : wd.fg;
    int x = b.x0, y = b.y0;
    outline(x, y, x + 22, y + 10, C_TEXT);
    fill(x + 23, y + 3, x + 24, y + 7, C_TEXT);
    qint64 n = fdiv(pct + 10, 20);
    for (int i = 0; i < 5; ++i) {
        int r = C_LINE;
        if (i < n)
            r = chg ? C_OK : (pct > 15 ? fg : C_WARN);
        fill(x + 2 + i * 4, y + 2, x + 4 + i * 4, y + 8, r);
    }
}

void Renderer::drawFrame(const Widget &wd, const Box &b)
{
    int c = wd.fg == C_AUTO ? C_LINE : wd.fg;
    switch (wd.param) {
    case FRAME_BRACKETS: {
        int l = wd.lo > 0 ? wd.lo : 6;
        const int segs[8][4] = {{b.x0, b.y0, b.x0 + l, b.y0}, {b.x0, b.y0, b.x0, b.y0 + l},
                                {b.x1 - l, b.y0, b.x1, b.y0}, {b.x1, b.y0, b.x1, b.y0 + l},
                                {b.x0, b.y1, b.x0 + l, b.y1}, {b.x0, b.y1 - l, b.x0, b.y1},
                                {b.x1 - l, b.y1, b.x1, b.y1}, {b.x1, b.y1 - l, b.x1, b.y1}};
        for (const auto &s : segs)
            line(s[0], s[1], s[2], s[3], c);
        break;
    }
    case FRAME_BOX: outline(b.x0, b.y0, b.x1, b.y1, c); break;
    case FRAME_HLINE: line(b.x0, b.y0, b.x1, b.y0, c); break;
    case FRAME_VLINE: line(b.x0, b.y0, b.x0, b.y1, c); break;
    case FRAME_PANEL: fill(b.x0, b.y0, b.x1, b.y1, c); break;
    }
}

void Renderer::drawTextbox(const Widget &wd, const Box &b, const std::optional<Value> &v)
{
    std::optional<QString> src;
    if (wd.source >= S_HOST_TXT && wd.source < S_HOST_TXT + 4 && v && v->valid)
        src = sources_.hostText(wd.source - S_HOST_TXT).value_or(QString());
    else if (v)
        src = v->text;
    const QffFont *f = font(wd.font);
    if (!src || !f)
        return;
    const QList<uint> s = head(*src, 159).toUcs4();
    const int pitch = f->lineHeight() + wd.param;
    const int width = b.x1 - b.x0 + 1;
    const int fitN = int(std::min<qint64>(fdiv(b.y1 - b.y0 + 1 + wd.param, std::max(pitch, 1)), 16));
    const bool tail = wd.flags & F_TAIL;
    if (fitN <= 0)
        return;
    QList<QString> lines;
    int pos = 0;
    const int len = int(s.size());
    while (pos < len && (tail || lines.size() < fitN)) {
        int n = 0, cut = 0, w = 0;
        while (pos + n < len && s[pos + n] != '\n' && n < 63) {
            w += f->width(char32_t(s[pos + n])).value_or(0);
            if (w > width && n) {
                n = cut ? cut : n;
                break;
            }
            ++n;
            if (s[pos + n - 1] == ' ')
                cut = n;
        }
        if (lines.size() == fitN)
            lines.removeFirst();
        lines.append(QString::fromUcs4(reinterpret_cast<const char32_t *>(s.constData() + pos), n));
        pos += n;
        if (pos < len && s[pos] == '\n')
            ++pos;
    }
    int fg = fgRole(wd, v);
    for (int i = 0; i < lines.size(); ++i) {
        int w = 0;
        QString t = fit(*f, lines[i], width, &w);
        text(aligned(wd, b, w), b.y0 + i * pitch, f, t, fg, wd.bg);
    }
}

Rgb Renderer::role(int r) const
{
    return rgb_.value(r >= 0 && r < rgb_.size() ? r : C_TEXT);
}

Hsv Renderer::hsv(int r) const
{
    return palette_.value(r >= 0 && r < palette_.size() ? r : C_TEXT);
}

void Renderer::put(int x, int y, const Rgb &rgb)
{
    if (x >= 0 && x < w_ && y >= 0 && y < h_)
        std::memcpy(img_.scanLine(y) + x * 3, rgb.data(), 3);
}

void Renderer::fill(int x0, int y0, int x1, int y1, int r)
{
    if (x1 < x0 || y1 < y0)
        return;
    fillRgb(x0, y0, x1, y1, role(r));
}

void Renderer::fillRgb(int x0, int y0, int x1, int y1, const Rgb &rgb)
{
    int l = std::max(0, std::min(x0, x1)), r = std::min(w_ - 1, std::max(x0, x1));
    int t = std::max(0, std::min(y0, y1)), b = std::min(h_ - 1, std::max(y0, y1));
    if (r < l || b < t)
        return;
    for (int y = t; y <= b; ++y) {
        uchar *row = img_.scanLine(y) + l * 3;
        for (int x = l; x <= r; ++x, row += 3)
            std::memcpy(row, rgb.data(), 3);
    }
}

void Renderer::outline(int x0, int y0, int x1, int y1, int r)
{
    int l = std::min(x0, x1), rr = std::max(x0, x1), t = std::min(y0, y1), b = std::max(y0, y1);
    fill(l, t, rr, t, r);
    fill(l, b, rr, b, r);
    fill(l, t + 1, l, b - 1, r);
    fill(rr, t + 1, rr, b - 1, r);
}

void Renderer::line(int x0, int y0, int x1, int y1, int r)
{
    // qp_line: Bresenham, straight lines as rectangles
    if (x0 == x1 || y0 == y1) {
        fill(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1), r);
        return;
    }
    Rgb rgb = role(r);
    int x = x0, y = y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int e = dx + dy;
    while (x != x1 || y != y1) {
        put(x, y, rgb);
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y += sy;
        }
    }
    put(x, y, rgb);
}

void Renderer::text(int x, int y, const QffFont *f, const QString &s, int fg, int bg)
{
    if (s.isEmpty() || !f)
        return;
    QList<Rgb> pal;
    for (const Hsv &c : interpolate(hsv(fg), hsv(bg), 1 << f->bpp()))
        pal.append(panelRgb(c));
    for (char32_t ch : s.toUcs4()) {
        if (!f->width(ch))
            return; // qp_drawtext stops at a missing glyph
        const auto &g = f->glyph(ch);
        for (int r = 0; r < f->lineHeight(); ++r)
            for (int c = 0; c < g.width; ++c)
                put(x + c, y + r, pal[quint8(g.px[r * g.width + c])]);
        x += g.width;
    }
}

}
