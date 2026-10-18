#include "ui/widgets/keyboardview.h"

#include "ui/theme.h"

#include <QEvent>
#include <QHelpEvent>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QToolTip>
#include <QTransform>

namespace {

constexpr double KEY_SIZE_RATIO = 3.2;
constexpr double KEY_SPACING_RATIO = 0.2;
constexpr double PADDING = 5;
constexpr double MASK_HEIGHT = 0.65;
constexpr double NONMASK_PADDING = 0.02;
constexpr double SIDE_PADDING = 0.1;
constexpr double BOTTOM_PADDING = 0.15;

QRectF rounded(double x, double y, double w, double h)
{
    // the Python widget used integer QRects; keep the same pixel grid
    return QRectF(std::round(x), std::round(y), std::round(w), std::round(h));
}

}

KeyboardView::KeyboardView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    choice_ = [](int) { return 0; };
}

void KeyboardView::setKeys(const QList<LayoutKey> &keys, const QList<LayoutKey> &encoders)
{
    all_.clear();
    for (const auto &k : keys)
        all_.append(Key{k, false});
    for (const auto &k : encoders)
        all_.append(Key{k, true});
    active_ = -1;
    relayout();
}

void KeyboardView::setLayoutChoice(std::function<int(int)> choice)
{
    choice_ = std::move(choice);
    relayout();
}

void KeyboardView::setScale(double s)
{
    scale_ = std::clamp(s, 0.2, 3.0);
    if (!qobject_cast<FittedKeyboard *>(parentWidget()))
        preferred_ = scale_;
    relayout();
}

QPolygonF KeyboardView::mapped(const Key &k, const QRectF &r) const
{
    // QRect's bottom-right is one pixel inside, as in the Python widget
    const QPointF pts[] = {r.topLeft(), QPointF(r.left(), r.bottom() - 1), QPointF(r.right() - 1, r.bottom() - 1),
                           QPointF(r.right() - 1, r.top())};
    QTransform t;
    t.translate(k.shiftX, k.shiftY);
    t.translate(k.rotX, k.rotY);
    t.rotate(k.def.geo.rotationAngle);
    t.translate(-k.rotX, -k.rotY);
    QPolygonF poly;
    for (const QPointF &p : pts)
        poly << t.map(p);
    poly << poly.first();
    return poly;
}

void KeyboardView::place(Key &k, double unit, double shiftX, double shiftY) const
{
    const kle::Key &g = k.def.geo;
    k.size = unit * (KEY_SIZE_RATIO + KEY_SPACING_RATIO);
    const double spacing = unit * KEY_SPACING_RATIO;
    k.shiftX = shiftX;
    k.shiftY = shiftY;
    k.rotX = k.size * g.rotationX;
    k.rotY = k.size * g.rotationY;
    const double x = k.size * g.x, y = k.size * g.y;
    const double w = k.size * g.width - spacing, h = k.size * g.height - spacing;
    k.rect = rounded(x, y, w, h);
    const double x2 = x + k.size * g.x2, y2 = y + k.size * g.y2;
    k.rect2 = rounded(x2, y2, k.size * g.width2 - spacing, k.size * g.height2 - spacing);
    const bool has2 = g.width2 != g.width || g.height2 != g.height || g.x2 != 0 || g.y2 != 0;

    k.polygon = mapped(k, k.rect).united(mapped(k, k.rect2));
    k.background = QPainterPath();
    if (k.encoder) {
        k.background.addEllipse(k.rect);
    } else {
        k.background.addRect(k.rect);
        if (has2) {
            QPainterPath p2;
            p2.addRect(k.rect2);
            k.background = k.background.united(p2);
        }
    }
    k.plain = !k.encoder && !has2 && k.def.geo.rotationAngle == 0;
    k.nonmaskRect = rounded(x, y + k.size * NONMASK_PADDING, w, h * (1 - MASK_HEIGHT));
    k.maskRect = rounded(x + k.size * SIDE_PADDING, y + h * (1 - MASK_HEIGHT), w - 2 * k.size * SIDE_PADDING,
                         h * MASK_HEIGHT - k.size * BOTTOM_PADDING);
    k.maskPolygon = mapped(k, k.maskRect);

    k.extra = QPainterPath();
    if (k.encoder) {
        const double p = h, ax = x, ay = y + p / 2;
        const double d = k.def.direction == 0 ? 1 : -1; // counterclockwise points down
        k.extra.moveTo(std::round(ax), std::round(ay));
        k.extra.lineTo(std::round(ax + p / 10), std::round(ay - d * p / 10));
        k.extra.lineTo(std::round(ax), std::round(ay + d * p / 10));
        k.extra.lineTo(std::round(ax - p / 10), std::round(ay - d * p / 10));
        k.extra.lineTo(std::round(ax), std::round(ay));
    }
}

void KeyboardView::relayout()
{
    const double unit = QFontMetricsF(font()).height();
    auto identity = [](const Key &k) {
        return std::make_tuple(k.def.geo.x, k.def.geo.y, k.encoder, k.def.direction, k.def.layoutIndex,
                               k.def.layoutOption);
    };
    const auto wasDef = active_ >= 0 && active_ < visible_.size() ? std::optional(identity(visible_[active_]))
                                                                  : std::nullopt;
    // what pages put on the keys survives a new scale or layout choice
    QMap<decltype(identity(Key{})), Key> shown;
    for (const Key &k : visible_)
        shown.insert(identity(k), k);

    QList<Key> out;
    QHash<QPair<int, int>, QPointF> topLeft;
    for (Key &k : all_) {
        place(k, unit, 0, 0);
        if (k.def.layoutIndex < 0)
            continue;
        QPointF p = k.polygon.boundingRect().topLeft();
        auto key = qMakePair(k.def.layoutIndex, k.def.layoutOption);
        QPointF cur = topLeft.value(key, QPointF(1e6, 1e6));
        topLeft[key] = QPointF(std::min(cur.x(), p.x()), std::min(cur.y(), p.y()));
    }
    for (Key &k : all_) {
        if (k.def.layoutIndex < 0) {
            out.append(k);
            continue;
        }
        if (k.def.layoutOption != choice_(k.def.layoutIndex))
            continue;
        QPointF mine = topLeft.value({k.def.layoutIndex, k.def.layoutOption});
        QPointF base = topLeft.value({k.def.layoutIndex, 0}, mine);
        place(k, unit, -(mine.x() - base.x()), -(mine.y() - base.y()));
        out.append(k);
    }
    double topX = 1e6, topY = 1e6;
    for (const Key &k : out) {
        if (k.def.geo.decal)
            continue;
        QPointF p = k.polygon.boundingRect().topLeft();
        topX = std::min(topX, p.x());
        topY = std::min(topY, p.y());
    }
    if (out.isEmpty())
        topX = topY = 0;
    visible_.clear();
    for (Key &k : out) {
        if (k.def.geo.decal)
            continue;
        place(k, unit, k.shiftX - topX + PADDING, k.shiftY - topY + PADDING);
        auto it = shown.find(identity(k));
        if (it != shown.end()) {
            k.text = it->text;
            k.maskText = it->maskText;
            k.tooltip = it->tooltip;
            k.masked = it->masked;
            k.on = it->on;
            k.pressed = it->pressed;
            k.active = it->active;
            k.color = it->color;
            k.maskColor = it->maskColor;
            k.glow = it->glow;
        }
        visible_.append(k);
    }
    std::stable_sort(visible_.begin(), visible_.end(), [](const Key &a, const Key &b) {
        return std::make_pair(a.rect.y(), a.rect.x()) < std::make_pair(b.rect.y(), b.rect.x());
    });

    active_ = -1;
    if (wasDef) {
        for (int i = 0; i < visible_.size(); ++i)
            if (identity(visible_[i]) == *wasDef)
                active_ = i;
    }

    double maxW = 0, maxH = 0;
    for (const Key &k : visible_) {
        QPointF p = k.polygon.boundingRect().bottomRight();
        maxW = std::max(maxW, p.x() * scale_);
        maxH = std::max(maxH, p.y() * scale_);
    }
    size_ = QSize(int(std::round(maxW + 2 * PADDING)), int(std::round(maxH + 2 * PADDING)));
    natural_ = QSize(int(std::round(maxW / scale_ + 2 * PADDING)), int(std::round(maxH / scale_ + 2 * PADDING)));
    fitCache_.clear();
    updateGeometry();
    update();
}

QSize KeyboardView::sizeHint() const
{
    return size_;
}

void KeyboardView::setActive(int index, bool mask)
{
    active_ = index;
    activeMask_ = mask;
    update();
}

void KeyboardView::selectNext()
{
    if (visible_.isEmpty() || active_ < 0)
        return;
    active_ = (active_ + 1) % int(visible_.size());
    activeMask_ = false;
    emit clicked(active_, false);
    update();
}

void KeyboardView::deselect()
{
    if (active_ < 0)
        return;
    active_ = -1;
    emit deselected();
    update();
}

std::pair<int, bool> KeyboardView::hit(const QPointF &pos) const
{
    const QPointF p = pos / scale_;
    for (int i = 0; i < visible_.size(); ++i) {
        const Key &k = visible_[i];
        if (k.masked && k.maskPolygon.containsPoint(p, Qt::OddEvenFill))
            return {i, true};
        if (k.polygon.containsPoint(p, Qt::OddEvenFill))
            return {i, false};
    }
    return {-1, false};
}

void KeyboardView::mousePressEvent(QMouseEvent *e)
{
    if (!isEnabled())
        return;
    auto [i, mask] = hit(e->position());
    if (i >= 0)
        emit keyPressed(i);
    if (!selectable_)
        return;
    active_ = i;
    activeMask_ = mask;
    if (i >= 0)
        emit clicked(i, mask);
    else
        emit deselected();
    update();
}

void KeyboardView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (selectable_ && active_ >= 0 && hit(e->position()).first == active_)
        emit doubleClicked(active_);
}

bool KeyboardView::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        int i = hit(he->pos()).first;
        if (i >= 0 && !visible_[i].tooltip.isEmpty())
            QToolTip::showText(he->globalPos(), visible_[i].tooltip, this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void KeyboardView::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::FontChange)
        relayout();
    QWidget::changeEvent(e);
}

QFont KeyboardView::fitted(const QFont &base, const QString &text, double width) const
{
    auto key = qMakePair(text, int(width));
    auto it = fitCache_.find(key);
    if (it != fitCache_.end())
        return *it;
    // shrink until the widest word of the legend fits
    QFont f(base);
    const QStringList words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (int i = 0; i < 4; ++i) {
        QFontMetricsF fm(f);
        double widest = 0;
        for (const QString &w : words)
            widest = std::max(widest, fm.horizontalAdvance(w));
        if (widest <= width)
            break;
        f.setPixelSize(std::max(1, int(std::round(f.pixelSize() * 0.88))));
    }
    fitCache_.insert(key, f);
    return f;
}

void KeyboardView::transformTo(QPainter &qp, const Key &k) const
{
    qp.scale(scale_, scale_);
    qp.translate(k.shiftX, k.shiftY);
    qp.translate(k.rotX, k.rotY);
    qp.rotate(k.def.geo.rotationAngle);
    qp.translate(-k.rotX, -k.rotY);
}

QRect KeyboardView::bounds(const Key &k) const
{
    QTransform t;
    t.scale(scale_, scale_);
    t.translate(k.shiftX, k.shiftY);
    t.translate(k.rotX, k.rotY);
    t.rotate(k.def.geo.rotationAngle);
    t.translate(-k.rotX, -k.rotY);
    return t.map(k.background).boundingRect().toAlignedRect().adjusted(-3, -3, 3, 3);
}

void KeyboardView::setGlows(const QList<std::pair<int, QColor>> &glows)
{
    QList<int> changed;
    for (const auto &[i, c] : glows) {
        if (i < 0 || i >= visible_.size() || visible_[i].glow == c)
            continue;
        visible_[i].glow = c;
        changed.append(i);
    }
    // a region of many small rects paints slower than the whole view
    if (changed.size() > visible_.size() / 4) {
        update();
        return;
    }
    for (int i : changed)
        update(bounds(visible_[i]));
}

void KeyboardView::paintEvent(QPaintEvent *e)
{
    using namespace ui::theme;
    QPainter qp(this);
    qp.setRenderHint(QPainter::Antialiasing);
    QPen line(LINE2);
    line.setCosmetic(true);
    QPen activePen(ACC);
    activePen.setCosmetic(true);
    activePen.setWidthF(2);
    QFont font = this->font();
    if (font.pixelSize() <= 0)
        font.setPixelSize(13);
    QFont maskFont(font);
    maskFont.setPixelSize(int(std::round(font.pixelSize() * 0.8)));

    const bool partial = e->rect() != rect();
    for (int i = 0; i < visible_.size(); ++i) {
        const Key &k = visible_[i];
        if (partial && !e->region().intersects(bounds(k)))
            continue;
        const bool active = k.active || (i == active_ && !activeMask_);
        // LED previews repaint every key 30 times a second: plain keycaps skip the path machinery
        if (k.glow && !active && k.plain) {
            const QColor c = *k.glow;
            const int lum = std::max({c.red(), c.green(), c.blue()});
            const QRectF r = k.rect.translated(k.shiftX, k.shiftY);
            const QRect px(QPoint(int(std::round(r.left() * scale_)), int(std::round(r.top() * scale_))),
                           QPoint(int(std::round(r.right() * scale_)) - 1, int(std::round(r.bottom() * scale_)) - 1));
            // pixel-aligned: no antialiasing needed, and filling is several times cheaper without it
            qp.setRenderHint(QPainter::Antialiasing, false);
            qp.fillRect(px, lum < 8 ? KEY : QColor(c.red() * 3 / 5, c.green() * 3 / 5, c.blue() * 3 / 5));
            qp.setPen(lum >= 8 ? c : LINE);
            qp.setBrush(Qt::NoBrush);
            qp.drawRect(px.adjusted(0, 0, -1, -1));
            qp.setRenderHint(QPainter::Antialiasing, true);
            continue;
        }
        qp.save();
        transformTo(qp, k);

        if (k.glow) {
            const QColor c = *k.glow;
            const int lum = std::max({c.red(), c.green(), c.blue()});
            QPen pen(lum >= 8 ? c : LINE);
            pen.setCosmetic(true);
            qp.setPen(pen);
            qp.setBrush(lum < 8 ? KEY : QColor(c.red() * 3 / 5, c.green() * 3 / 5, c.blue() * 3 / 5));
            qp.drawPath(k.background);
            if (active) {
                qp.setPen(activePen);
                qp.setBrush(Qt::NoBrush);
                qp.drawPath(k.background);
            }
            qp.restore();
            continue;
        }
        qp.setBrush(k.pressed ? ACC : k.on ? ACC_D : active ? ACC_BG : KEY);
        qp.setPen(active ? activePen : line);
        qp.drawPath(k.background);

        QColor pen = k.pressed                           ? BG
                     : k.text == QStringLiteral("▽") ? LINE2
                     : k.color.isValid()                  ? k.color
                     : active                             ? ACC
                                                          : TXT;
        const int pad = int(std::round(k.size * 0.12));
        if (k.masked) {
            qp.setFont(maskFont);
            qp.setPen(pen);
            qp.drawText(k.nonmaskRect.adjusted(pad, 0, -pad, 0), Qt::AlignTop | Qt::AlignLeft, k.text);
            const bool maskActive = i == active_ && activeMask_;
            qp.setPen(maskActive ? activePen : line);
            qp.setBrush(maskActive ? ACC_BG : PANEL2);
            qp.drawRect(k.maskRect);
            qp.setPen(k.maskColor.isValid() ? k.maskColor : maskActive ? ACC : TXT);
            qp.drawText(k.maskRect, Qt::AlignCenter, k.maskText);
        } else if (k.encoder) {
            qp.setFont(maskFont);
            qp.setPen(pen);
            qp.drawText(k.rect, Qt::AlignCenter, k.text);
        } else {
            QRectF r = k.rect.adjusted(pad, pad, -pad, -pad);
            qp.setFont(fitted(font, k.text, r.width()));
            qp.setPen(pen);
            qp.drawText(r, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, k.text);
        }
        if (!k.extra.isEmpty()) {
            qp.setPen(Qt::NoPen);
            qp.setBrush(active ? ACC : MID);
            qp.drawPath(k.extra);
        }
        qp.restore();
    }
}

FittedKeyboard::FittedKeyboard(KeyboardView *view, QWidget *parent)
    : QWidget(parent)
    , view_(view)
{
    view_->setParent(this);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void FittedKeyboard::setPreferredScale(double s)
{
    view_->preferred_ = std::clamp(s, 0.2, 3.0);
    fit();
}

void FittedKeyboard::fit()
{
    const QSize nat = view_->naturalSize();
    double s = view_->preferred_;
    if (nat.width() > 0 && width() > 20)
        s = std::min(s, double(width() - 2) / nat.width());
    if (std::abs(s - view_->scale()) > 0.003) {
        view_->scale_ = std::clamp(s, 0.2, 3.0);
        view_->relayout();
    }
    const QSize vs = view_->sizeHint();
    view_->setGeometry((width() - vs.width()) / 2, 0, vs.width(), vs.height());
    updateGeometry();
}

QSize FittedKeyboard::sizeHint() const
{
    const QSize nat = view_->naturalSize();
    return {int(nat.width() * view_->preferred_), int(nat.height() * view_->preferred_)};
}

int FittedKeyboard::heightForWidth(int w) const
{
    const QSize nat = view_->naturalSize();
    if (nat.width() <= 0)
        return 0;
    const double s = std::min(view_->preferred_, double(w - 2) / nat.width());
    return int(std::ceil(nat.height() * s));
}

void FittedKeyboard::resizeEvent(QResizeEvent *)
{
    fit();
}

bool FittedKeyboard::event(QEvent *e)
{
    // the view relaid itself out (new keys, layout option): fit again
    if (e->type() == QEvent::LayoutRequest)
        fit();
    return QWidget::event(e);
}
