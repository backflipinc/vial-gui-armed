#include "ui/widgets/hudcanvas.h"

#include "ui/theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

using namespace ui::theme;

namespace {

constexpr int HANDLE = 9;

int floorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

}

HudCanvas::HudCanvas(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAccessibleName(tr("HUD layout"));
}

void HudCanvas::setScreen(int w, int h)
{
    w_ = w;
    h_ = h;
    setScale(scale_);
}

void HudCanvas::setScale(int s)
{
    scale_ = std::clamp(s, 1, 6);
    setFixedSize(w_ * scale_ + 2, h_ * scale_ + 2);
    update();
}

void HudCanvas::setContent(const QImage &image, hud::Layout *layout, const QList<std::optional<hud::Box>> &areas)
{
    // the editor re-renders every half second; most of the time nothing changed
    if (layout == layout_ && image == image_ && areas == areas_)
        return;
    image_ = image;
    layout_ = layout;
    areas_ = areas;
    update();
}

void HudCanvas::setSelection(const QList<int> &sel)
{
    if (sel == sel_)
        return;
    sel_ = sel;
    update();
}

QPoint HudCanvas::toPx(const QPoint &pos) const
{
    return {floorDiv(pos.x() - 1, scale_), floorDiv(pos.y() - 1, scale_)};
}

QRect HudCanvas::rectOf(const hud::Box &a) const
{
    return {1 + a.x0 * scale_, 1 + a.y0 * scale_, (a.x1 - a.x0 + 1) * scale_, (a.y1 - a.y0 + 1) * scale_};
}

std::optional<hud::Box> HudCanvas::area(int i) const
{
    return i >= 0 && i < areas_.size() ? areas_[i] : std::nullopt;
}

std::optional<QRect> HudCanvas::handle() const
{
    if (sel_.size() == 1)
        if (auto a = area(sel_.first())) {
            const QRect r = rectOf(*a);
            return QRect(r.right() - HANDLE / 2, r.bottom() - HANDLE / 2, HANDLE, HANDLE);
        }
    return std::nullopt;
}

int HudCanvas::pick(int x, int y) const
{
    for (int i = int(areas_.size()) - 1; i >= 0; --i)
        if (const auto &a = areas_[i]; a && a->x0 <= x && x <= a->x1 && a->y0 <= y && y <= a->y1)
            return i;
    return -1;
}

std::pair<QList<int>, QList<int>> HudCanvas::targets(const QSet<int> &exclude) const
{
    // edges and centres to snap to: the screen's and the other widgets'
    QList<int> xs{0, w_ / 2, w_}, ys{0, h_ / 2, h_};
    for (int i = 0; i < areas_.size(); ++i) {
        const auto &a = areas_[i];
        if (!a || exclude.contains(i))
            continue;
        xs << a->x0 << (a->x0 + a->x1 + 1) / 2 << a->x1 + 1;
        ys << a->y0 << (a->y0 + a->y1 + 1) / 2 << a->y1 + 1;
    }
    return {xs, ys};
}

std::optional<std::pair<int, int>> HudCanvas::snapAxis(const QList<int> &edges, const QList<int> &targets) const
{
    const int limit = std::max(2, 8 / scale_);
    std::optional<std::pair<int, int>> best;
    for (int e : edges)
        for (int t : targets) {
            const int d = t - e;
            if (std::abs(d) <= limit && (!best || std::abs(d) < std::abs(best->first)))
                best = std::pair{d, t};
        }
    return best;
}

QRect HudCanvas::bandRect() const
{
    const auto &[a, b] = *band_;
    return QRect(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())), QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

void HudCanvas::paintEvent(QPaintEvent *)
{
    QPainter qp(this);
    qp.fillRect(rect(), Qt::black);
    qp.setPen(hasFocus() ? ACC_D : LINE2);
    qp.drawRect(rect().adjusted(0, 0, -1, -1));
    if (!image_.isNull())
        qp.drawImage(QRect(1, 1, w_ * scale_, h_ * scale_), image_);
    if (showBounds) {
        qp.setPen(QPen(DIM, 1, Qt::DotLine));
        for (const auto &a : areas_)
            if (a)
                qp.drawRect(rectOf(*a).adjusted(0, 0, -1, -1));
    }
    if (auto a = area(hover_); a && !sel_.contains(hover_) && !drag_) {
        qp.setPen(QPen(MID, 1, Qt::DashLine));
        qp.drawRect(rectOf(*a).adjusted(0, 0, -1, -1));
    }
    for (int i : sel_) {
        if (auto a = area(i)) {
            const bool solid = i == primary() && sel_.size() == 1;
            qp.setPen(QPen(ACC, 1, solid ? Qt::SolidLine : Qt::DashLine));
            qp.drawRect(rectOf(*a).adjusted(0, 0, -1, -1));
        }
    }
    if (auto h = handle())
        qp.fillRect(*h, ACC);
    qp.setPen(QPen(ACC, 1, Qt::DashLine));
    for (const auto &[axis, v] : guides_) {
        const int p = 1 + v * scale_;
        if (axis == 'x')
            qp.drawLine(p, 0, p, height());
        else
            qp.drawLine(0, p, width(), p);
    }
    if (band_) {
        qp.setPen(QPen(ACC, 1, Qt::DotLine));
        qp.fillRect(bandRect(), QColor(255, 170, 0, 30));
        qp.drawRect(bandRect());
    }
}

void HudCanvas::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    const QPoint px = toPx(e->position().toPoint());
    if (e->button() == Qt::RightButton) {
        const int i = pick(px.x(), px.y());
        if (i >= 0 && !sel_.contains(i)) {
            sel_ = {i};
            emit selectionChanged();
        }
        emit contextRequested(e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton || !layout_)
        return;
    if (auto h = handle(); h && h->contains(e->position().toPoint())) {
        const int i = sel_.first();
        const auto a = *areas_[i];
        drag_ = Drag{true, px.x(), px.y(), {{i, QPoint(a.x1 - a.x0 + 1, a.y1 - a.y0 + 1)}}, false};
        return;
    }
    const int i = pick(px.x(), px.y());
    if (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
        if (i >= 0) {
            if (sel_.contains(i))
                sel_.removeAll(i);
            else
                sel_.append(i);
            emit selectionChanged();
        } else {
            band_ = std::pair{e->position().toPoint(), e->position().toPoint()};
            bandBase_ = sel_;
        }
        return;
    }
    if (i < 0) {
        sel_.clear();
        emit selectionChanged();
        band_ = std::pair{e->position().toPoint(), e->position().toPoint()};
        bandBase_.clear();
        return;
    }
    // a click on one of several selected widgets narrows the selection to it unless it turns into a drag
    clickOnly_ = sel_.contains(i) && sel_.size() > 1 ? i : -1;
    if (!sel_.contains(i)) {
        sel_ = {i};
    } else {
        sel_.removeAll(i);
        sel_.append(i);
    }
    emit selectionChanged();
    Drag d{false, px.x(), px.y(), {}, false};
    for (int j : sel_)
        d.start.insert(j, QPoint(layout_->widgets[j].x, layout_->widgets[j].y));
    drag_ = d;
}

void HudCanvas::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint px = toPx(e->position().toPoint());
    const bool inside = px.x() >= 0 && px.x() < w_ && px.y() >= 0 && px.y() < h_;
    emit cursorMoved(inside ? px.x() : -1, inside ? px.y() : -1);
    if (band_) {
        band_->second = e->position().toPoint();
        const QRect r = bandRect();
        QList<int> sel = bandBase_;
        for (int i = 0; i < areas_.size(); ++i)
            if (areas_[i] && r.intersects(rectOf(*areas_[i])) && !sel.contains(i))
                sel.append(i);
        if (sel != sel_) {
            sel_ = sel;
            emit selectionChanged();
        }
        update();
        return;
    }
    if (!drag_) {
        const auto h = handle();
        setCursor(h && h->contains(e->position().toPoint()) ? Qt::SizeFDiagCursor
                  : sel_.contains(pick(px.x(), px.y()))     ? Qt::SizeAllCursor
                                                            : Qt::ArrowCursor);
        const int hv = pick(px.x(), px.y());
        if (hv != hover_) {
            hover_ = hv;
            update();
        }
        return;
    }
    int dx = px.x() - drag_->x0, dy = px.y() - drag_->y0;
    if (!drag_->started) {
        if (!dx && !dy)
            return;
        emit editStarted();
        drag_->started = true;
    }
    const bool snapping = snap && !(e->modifiers() & Qt::AltModifier);
    guides_.clear();
    QSet<int> moving;
    for (auto it = drag_->start.begin(); it != drag_->start.end(); ++it)
        moving.insert(it.key());
    if (!drag_->size) {
        QList<hud::Box> boxes;
        for (int j : moving)
            if (areas_.value(j))
                boxes.append(*areas_[j]);
        if (!boxes.isEmpty() && snapping) {
            int l = INT_MAX, r = INT_MIN, t = INT_MAX, b = INT_MIN;
            for (const auto &bx : boxes) {
                l = std::min(l, bx.x0);
                r = std::max(r, bx.x1 + 1);
                t = std::min(t, bx.y0);
                b = std::max(b, bx.y1 + 1);
            }
            l += dx;
            r += dx;
            t += dy;
            b += dy;
            const auto [xs, ys] = targets(moving);
            if (auto sx = snapAxis({l, (l + r) / 2, r}, xs)) {
                dx += sx->first;
                guides_.append({'x', sx->second});
            }
            if (auto sy = snapAxis({t, (t + b) / 2, b}, ys)) {
                dy += sy->first;
                guides_.append({'y', sy->second});
            }
        }
        for (auto it = drag_->start.begin(); it != drag_->start.end(); ++it) {
            auto &wd = layout_->widgets[it.key()];
            wd.x = std::clamp(it->x() + dx, 0, w_ - 1);
            wd.y = std::clamp(it->y() + dy, 0, h_ - 1);
        }
    } else {
        const int j = drag_->start.firstKey();
        const QPoint s = drag_->start.first();
        auto &wd = layout_->widgets[j];
        int nw = s.x() + dx, nh = s.y() + dy;
        if (snapping) {
            const auto [xs, ys] = targets({j});
            if (auto sx = snapAxis({wd.x + nw}, xs)) {
                nw += sx->first;
                guides_.append({'x', sx->second});
            }
            if (auto sy = snapAxis({wd.y + nh}, ys)) {
                nh += sy->first;
                guides_.append({'y', sy->second});
            }
        }
        wd.w = std::max(1, std::min(w_ - wd.x, nw));
        wd.h = std::max(1, std::min(h_ - wd.y, nh));
    }
    emit edited();
}

void HudCanvas::mouseReleaseEvent(QMouseEvent *)
{
    if (drag_ && !drag_->started && clickOnly_ >= 0) {
        sel_ = {clickOnly_};
        emit selectionChanged();
    }
    clickOnly_ = -1;
    drag_.reset();
    band_.reset();
    guides_.clear();
    update();
}

void HudCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPoint px = toPx(e->position().toPoint());
    const int i = pick(px.x(), px.y());
    if (i >= 0)
        emit activated(i);
}

void HudCanvas::leaveEvent(QEvent *)
{
    hover_ = -1;
    emit cursorMoved(-1, -1);
    update();
}

void HudCanvas::keyPressEvent(QKeyEvent *e)
{
    const int step = e->modifiers() & Qt::ShiftModifier ? 8 : 1;
    int dx = 0, dy = 0;
    switch (e->key()) {
    case Qt::Key_Left: dx = -step; break;
    case Qt::Key_Right: dx = step; break;
    case Qt::Key_Up: dy = -step; break;
    case Qt::Key_Down: dy = step; break;
    default: return QWidget::keyPressEvent(e);
    }
    if (sel_.isEmpty())
        return QWidget::keyPressEvent(e);
    emit nudge(dx, dy);
}
