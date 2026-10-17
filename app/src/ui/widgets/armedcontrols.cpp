#include "ui/widgets/armedcontrols.h"

#include "ui/theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QToolTip>
#include <QVBoxLayout>

using namespace ui::theme;

BracketFrame::BracketFrame(const QString &title, bool accent, QWidget *parent)
    : QFrame(parent)
    , box_(new QVBoxLayout(this))
    , accent_(accent)
{
    box_->setContentsMargins(16, 12, 16, 12);
    box_->setSpacing(9);
    if (!title.isNull()) {
        title_ = new QLabel(title);
        title_->setFont(head(17));
        header_ = new QHBoxLayout;
        header_->setSpacing(10);
        header_->addWidget(title_);
        header_->addStretch(1);
        box_->addLayout(header_);
    }
}

void BracketFrame::setAccent(bool on)
{
    accent_ = on;
    update();
}

void BracketFrame::paintEvent(QPaintEvent *)
{
    QPainter qp(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    qp.fillRect(r, PANEL);
    qp.setPen(LINE);
    qp.drawRect(r.adjusted(4, 4, -4, -4));
    QPen pen(accent_ ? ACC : MID);
    pen.setWidth(2);
    qp.setPen(pen);
    const int n = 12;
    const int x0 = r.left() + 1, y0 = r.top() + 1, x1 = r.right(), y1 = r.bottom();
    for (auto [x, y, dx, dy] : {std::tuple{x0, y0, 1, 1}, std::tuple{x1, y0, -1, 1}, std::tuple{x0, y1, 1, -1},
                                std::tuple{x1, y1, -1, -1}}) {
        qp.drawLine(x, y, x + dx * n, y);
        qp.drawLine(x, y, x, y + dy * n);
    }
}

// ---- swatches

namespace {
constexpr int SIZE = 18, GAP = 7;
}

Swatches::Swatches(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QColor Swatches::color(const armed::Color &c)
{
    if (c.black())
        return Qt::black;
    return QColor::fromHsv(c.h * 359 / 255, c.s, 255);
}

void Swatches::setColors(const QList<armed::Color> &colors)
{
    colors_ = colors;
    updateGeometry();
    update();
}

void Swatches::setCurrent(int index)
{
    current_ = index;
    update();
}

QSize Swatches::sizeHint() const
{
    const int n = std::max(1, int(colors_.size()));
    return {n * (SIZE + GAP) + 4, SIZE + 8};
}

QRect Swatches::cell(int i) const
{
    return {4 + i * (SIZE + GAP), 4, SIZE, SIZE};
}

void Swatches::paintEvent(QPaintEvent *)
{
    QPainter qp(this);
    for (int i = 0; i < colors_.size(); ++i) {
        const QRect r = cell(i);
        qp.fillRect(r, color(colors_[i]));
        qp.setPen(LINE2);
        qp.drawRect(r.adjusted(0, 0, -1, -1));
        if (i == current_) {
            QPen pen(hasFocus() ? TXT : ACC);
            pen.setWidth(2);
            qp.setPen(pen);
            qp.drawRect(r.adjusted(-3, -3, 2, 2));
        }
    }
}

void Swatches::mousePressEvent(QMouseEvent *e)
{
    for (int i = 0; i < colors_.size(); ++i) {
        if (cell(i).adjusted(-3, -3, 3, 3).contains(e->position().toPoint())) {
            setCurrent(i);
            emit changed(i);
            return;
        }
    }
}

void Swatches::keyPressEvent(QKeyEvent *e)
{
    int next = current_;
    if (e->key() == Qt::Key_Left)
        next = std::max(0, current_ - 1);
    else if (e->key() == Qt::Key_Right)
        next = std::min(int(colors_.size()) - 1, current_ + 1);
    else
        return QWidget::keyPressEvent(e);
    if (next != current_ && next >= 0) {
        setCurrent(next);
        emit changed(next);
    }
}

bool Swatches::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        for (int i = 0; i < colors_.size(); ++i)
            if (cell(i).contains(he->pos())) {
                QToolTip::showText(he->globalPos(), colors_[i].name, this);
                return true;
            }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

// ---- segmented

Segmented::Segmented(const QStringList &labels, QWidget *parent)
    : QWidget(parent)
    , layout_(new QHBoxLayout(this))
    , group_(new QButtonGroup(this))
{
    setObjectName("armedSubNav");
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::idClicked, this, &Segmented::changed);
    setLabels(labels);
}

void Segmented::setLabels(const QStringList &labels)
{
    const auto buttons = group_->buttons();
    if (buttons.size() == labels.size()) {
        for (int i = 0; i < labels.size(); ++i)
            group_->button(i)->setText(labels[i]);
        return;
    }
    for (auto *b : buttons) {
        group_->removeButton(b);
        b->deleteLater();
    }
    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QPushButton(labels[i]);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        group_->addButton(b, i);
        layout_->addWidget(b);
    }
}

void Segmented::setCurrent(int index)
{
    if (auto *b = group_->button(index))
        b->setChecked(true);
}

// ---- light bar

StripView::StripView(QWidget *parent)
    : QWidget(parent)
    , colors_(9, {0, 0, 0})
{
    setFixedHeight(16);
}

void StripView::setColors(const QList<std::array<quint8, 3>> &colors)
{
    colors_ = colors;
    update();
}

void StripView::paintEvent(QPaintEvent *)
{
    QPainter qp(this);
    const int n = int(colors_.size());
    if (!n)
        return;
    const int w = std::min(26, (width() - (n - 1) * 6) / n);
    for (int i = 0; i < n; ++i) {
        const QRect r(i * (w + 6), 4, w, 8);
        const auto &c = colors_[i];
        if (std::max({c[0], c[1], c[2]}) < 8) {
            qp.fillRect(r, LINE);
        } else {
            QColor glow(c[0], c[1], c[2], 70);
            qp.fillRect(r.adjusted(-2, -3, 2, 3), glow);
            qp.fillRect(r, QColor(c[0], c[1], c[2]));
        }
    }
}

QSlider *armedSlider(int maximum)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(0, maximum);
    s->setMinimumWidth(140);
    return s;
}

QHBoxLayout *sliderRow(const QString &name, QSlider *slider, bool percent)
{
    auto *row = new QHBoxLayout;
    auto *label = new QLabel(name);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(MID.name()));
    label->setFixedWidth(58);
    label->setBuddy(slider);
    auto *num = new QLabel;
    num->setFixedWidth(44);
    num->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto show = [num, slider, percent](int v) {
        num->setText(percent ? QStringLiteral("%1%").arg(v * 100 / std::max(1, slider->maximum())) : QString::number(v));
    };
    QObject::connect(slider, &QSlider::valueChanged, num, show);
    QObject::connect(slider, &QSlider::rangeChanged, num, [show, slider] { show(slider->value()); });
    show(slider->value());
    row->addWidget(label);
    row->addWidget(slider, 1);
    row->addWidget(num);
    return row;
}
