#include "ui/toast.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>

Toast::Toast(QWidget *host)
    : QFrame(host)
    , label_(new QLabel(this))
{
    setObjectName("armedToast");
    setAttribute(Qt::WA_StyledBackground);
    label_->setWordWrap(true);
    label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(14, 10, 14, 10);
    l->addWidget(label_);
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &QWidget::hide);
    host->installEventFilter(this);
    hide();
}

void Toast::show(const QString &text, Controller::ToastKind kind)
{
    static const char *kinds[] = {"info", "ok", "error"};
    setProperty("kind", kinds[int(kind)]);
    style()->unpolish(this);
    style()->polish(this);
    label_->setText(text);
    setAccessibleDescription(text);
    place();
    QWidget::show();
    raise();
    // errors stay longer: they are worth reading
    timer_.start(kind == Controller::ToastKind::Error ? 9000 : 3500);
}

void Toast::place()
{
    QWidget *host = parentWidget();
    const int w = std::min(520, host->width() - 32);
    label_->setFixedWidth(w - 28);
    adjustSize();
    move(host->width() - width() - 16, host->height() - height() - 16);
}

bool Toast::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == parentWidget() && e->type() == QEvent::Resize && isVisible())
        place();
    return QFrame::eventFilter(obj, e);
}

void Toast::mousePressEvent(QMouseEvent *)
{
    hide();
}
