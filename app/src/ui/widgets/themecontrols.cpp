#include "ui/widgets/themecontrols.h"

#include "ui/theme.h"

#include <QColorDialog>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QWidgetAction>

#include <cmath>

using namespace ui::theme;
using theme::Rgb;

namespace {

const Rgb kPalette[] = {
    {255, 255, 255}, {255, 170, 0}, {255, 90, 0}, {255, 0, 0}, {255, 0, 90}, {255, 0, 255}, {150, 0, 255},
    {60, 0, 255}, {0, 0, 255}, {0, 120, 255}, {0, 200, 255}, {0, 255, 200}, {0, 255, 60}, {120, 255, 0},
    {255, 255, 0}, {255, 200, 120}, {120, 60, 0}, {60, 60, 60}, {20, 20, 20}, {0, 0, 0},
};

QColor qc(const Rgb &c)
{
    return QColor(c[0], c[1], c[2]);
}

class PaletteGrid : public QWidget {
public:
    static constexpr int CELL = 20, GAP = 5, COLS = 10;
    std::function<void(const Rgb &)> picked;
    Rgb current;

    explicit PaletteGrid(const Rgb &cur)
        : current(cur)
    {
        setCursor(Qt::PointingHandCursor);
        const int rows = int((std::size(kPalette) + COLS - 1) / COLS);
        setFixedSize(COLS * (CELL + GAP) + 12, rows * (CELL + GAP) + 12);
    }
    QRect cell(int i) const { return {8 + (i % COLS) * (CELL + GAP), 8 + (i / COLS) * (CELL + GAP), CELL, CELL}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter qp(this);
        for (int i = 0; i < int(std::size(kPalette)); ++i) {
            const QRect r = cell(i);
            qp.fillRect(r, qc(kPalette[i]));
            QPen pen(kPalette[i] == current ? ACC : LINE2);
            pen.setWidth(kPalette[i] == current ? 2 : 1);
            qp.setPen(pen);
            qp.drawRect(r.adjusted(0, 0, -1, -1));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < int(std::size(kPalette)); ++i)
            if (cell(i).contains(e->position().toPoint()) && picked)
                picked(kPalette[i]);
    }
};

class LayerDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *qp, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QVariant v = index.data(Qt::UserRole);
        if (!v.canConvert<theme::Layer>())
            return;
        const theme::Layer layerValue = v.value<theme::Layer>();
        const theme::Layer *layer = &layerValue;
        qp->save();
        const bool selected = option.state & QStyle::State_Selected;
        const QRect r = option.rect.adjusted(2, 2, -4, -2);
        if (selected) {
            qp->setPen(ACC);
            qp->drawRect(r.adjusted(0, 0, -1, -1));
            qp->fillRect(QRect(r.left(), r.top(), 4, r.height()), ACC);
        } else if (option.state & QStyle::State_MouseOver) {
            qp->setPen(LINE2);
            qp->drawRect(r.adjusted(0, 0, -1, -1));
        }
        const bool dim = layer->hidden();
        qp->setFont(mono(12));
        qp->setPen(selected ? ACC : DIM);
        qp->drawText(r.adjusted(12, 0, 0, 0), Qt::AlignVCenter,
                     QStringLiteral("%1").arg(index.data(Qt::UserRole + 1).toInt(), 2, 10, QLatin1Char('0')));
        const QRect sw(r.left() + 40, r.center().y() - 6, 34, 12);
        layerSwatch(*qp, sw, *layer);
        qp->setPen(LINE2);
        qp->drawRect(sw);
        qp->setFont(head(15));
        qp->setPen(dim ? DIM : selected ? ACC : TXT);
        qp->drawText(r.adjusted(84, 0, -32, 0), Qt::AlignVCenter, index.data(Qt::DisplayRole).toString());
        qp->setPen(dim ? DIM : ACC);
        qp->setFont(mono(11));
        qp->drawText(LayerList::eyeRect(option.rect), Qt::AlignCenter, dim ? QStringLiteral("OFF") : QStringLiteral("ON"));
        qp->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {260, 34}; }
};

}


// ---- colour button

ColorButton::ColorButton(const QString &label, QWidget *parent)
    : QWidget(parent)
    , label_(label)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(30);
    setMinimumWidth(118);
    setToolTip(tr("Pick a colour"));
}

void ColorButton::setColor(const Rgb &c)
{
    color_ = c;
    update();
}

void ColorButton::setLabel(const QString &l)
{
    label_ = l;
    update();
}

void ColorButton::paintEvent(QPaintEvent *)
{
    QPainter qp(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    qp.fillRect(r, KEY);
    qp.setPen(underMouse() || hasFocus() ? ACC : LINE2);
    qp.drawRect(r);
    const QRect chip(5, 5, 26, r.height() - 9);
    qp.fillRect(chip, qc(color_));
    qp.setPen(LINE2);
    qp.drawRect(chip);
    qp.setFont(mono(12));
    qp.setPen(DIM);
    qp.drawText(r.adjusted(38, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, label_);
    qp.setPen(TXT);
    qp.drawText(r.adjusted(38, 0, -6, 0), Qt::AlignVCenter | Qt::AlignRight, theme::hexColor(color_).toUpper());
}

void ColorButton::mousePressEvent(QMouseEvent *)
{
    openMenu();
}

void ColorButton::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        openMenu();
    else
        QWidget::keyPressEvent(e);
}

void ColorButton::openMenu()
{
    QMenu menu(this);
    auto *grid = new PaletteGrid(color_);
    auto *act = new QWidgetAction(&menu);
    act->setDefaultWidget(grid);
    menu.addAction(act);
    menu.addSeparator();
    QAction *custom = menu.addAction(tr("ANY COLOUR…"));
    std::optional<Rgb> chosen;
    grid->picked = [&menu, &chosen](const Rgb &c) {
        chosen = c;
        menu.close();
    };
    QAction *a = menu.exec(mapToGlobal(rect().bottomLeft()));
    if (a == custom) {
        const QColor c = QColorDialog::getColor(qc(color_), this, tr("Colour"));
        if (c.isValid())
            chosen = Rgb{quint8(c.red()), quint8(c.green()), quint8(c.blue())};
    }
    if (chosen) {
        setColor(*chosen);
        emit changed(color_);
    }
}

// ---- dial

DirectionDial::DirectionDial(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(86, 86);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(tr("Direction: drag, or use the arrow keys (Shift for fine steps)"));
}

void DirectionDial::setValue(int v, bool radial)
{
    value_ = v & 0xFF;
    radial_ = radial;
    update();
}

void DirectionDial::paintEvent(QPaintEvent *)
{
    QPainter qp(this);
    qp.setRenderHint(QPainter::Antialiasing);
    const QPointF c(width() / 2.0, height() / 2.0);
    const double rad = std::min(width(), height()) / 2.0 - 6;
    qp.setPen(QPen(hasFocus() ? ACC_D : LINE2, 1));
    qp.setBrush(KEY);
    qp.drawEllipse(c, rad, rad);
    qp.setPen(QPen(LINE2, 1));
    for (int k = 0; k < 8; ++k) {
        const double a = k * M_PI / 4;
        qp.drawLine(c + QPointF(std::cos(a), std::sin(a)) * (rad - 4), c + QPointF(std::cos(a), std::sin(a)) * rad);
    }
    const QColor acc = radial_ ? DIM : ACC;
    const double a = value_ * 2 * M_PI / 256;
    const QPointF dir(std::cos(a), std::sin(a));
    const QPointF tip = c + dir * (rad - 8), tail = c - dir * (rad - 8);
    qp.setPen(QPen(acc, 2));
    qp.drawLine(tail, tip);
    const QPointF side = QPointF(-std::sin(a), std::cos(a)) * 6;
    const QPointF back = c + dir * (rad - 20);
    qp.setBrush(acc);
    qp.drawPolygon(QPolygonF{tip, back + side, back - side});
}

void DirectionDial::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton)
        drag(e);
}

void DirectionDial::drag(QMouseEvent *e)
{
    const double dx = e->position().x() - width() / 2.0, dy = e->position().y() - height() / 2.0;
    if (dx * dx + dy * dy < 16)
        return;
    int v = int(std::lround(std::atan2(dy, dx) * 128 / M_PI)) & 0xFF;
    if (!(e->modifiers() & Qt::ShiftModifier)) {
        const int snap = int(std::lround(v / 32.0)) * 32 & 0xFF;
        if (std::min(std::abs(v - snap), 256 - std::abs(v - snap)) <= 6)
            v = snap;
    }
    if (v != value_ || radial_) {
        value_ = v;
        update();
        emit changed(v);
    }
}

void DirectionDial::keyPressEvent(QKeyEvent *e)
{
    const int step = e->modifiers() & Qt::ShiftModifier ? 1 : 32;
    int v = value_;
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Down)
        v = (value_ - step) & 0xFF;
    else if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Up)
        v = (value_ + step) & 0xFF;
    else
        return QWidget::keyPressEvent(e);
    value_ = v;
    update();
    emit changed(v);
}

// ---- layers

void layerSwatch(QPainter &qp, const QRect &r, const theme::Layer &l)
{
    if (l.type == theme::RAINBOW) {
        QLinearGradient g(r.left(), 0, r.right(), 0);
        for (int k = 0; k < 7; ++k)
            g.setColorAt(k / 6.0, QColor::fromHsv(k * 359 / 6, 255, 255));
        qp.fillRect(r, g);
    } else if (l.type == theme::PAINT) {
        const int w = r.width() / 4;
        const Rgb cs[] = {{255, 170, 0}, {0, 200, 255}, {255, 0, 90}, {120, 255, 0}};
        for (int k = 0; k < 4; ++k)
            qp.fillRect(QRect(r.left() + k * w, r.top(), w, r.height()), qc(cs[k]));
    } else if (const auto *t = theme::typeById(l.type); t && t->controls.contains("b")) {
        QLinearGradient g(r.left(), 0, r.right(), 0);
        g.setColorAt(0, qc(l.a));
        g.setColorAt(1, qc(l.b));
        qp.fillRect(r, g);
    } else {
        qp.fillRect(r, qc(l.a));
    }
}

QString layerSummary(const theme::Layer &l)
{
    const auto *t = theme::typeById(l.type);
    QString name = t ? t->name : QStringLiteral("?");
    if (t && t->controls.contains("dir") && l.flag(theme::F_RADIAL))
        name += QStringLiteral(" ◎");
    return name;
}

LayerList::LayerList(QWidget *parent)
    : QListWidget(parent)
{
    setItemDelegate(new LayerDelegate(this));
    setMouseTracking(true);
}

QRect LayerList::eyeRect(const QRect &row)
{
    return {row.right() - 36, row.top() + 4, 30, row.height() - 8};
}

void LayerList::mousePressEvent(QMouseEvent *e)
{
    QListWidgetItem *it = itemAt(e->position().toPoint());
    if (it && eyeRect(visualItemRect(it)).contains(e->position().toPoint())) {
        emit toggled(row(it));
        return;
    }
    QListWidget::mousePressEvent(e);
}
