#include "ui/sidenav.h"

#include "ui/theme.h"

#include <QPainter>
#include <QStyledItemDelegate>

namespace {

class NavDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *qp, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        using namespace ui::theme;
        qp->save();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool focus = option.state & QStyle::State_HasFocus;
        QRect r = option.rect.adjusted(8, 1, -8, -1);
        if (selected) {
            qp->setPen(ACC);
            qp->drawRect(r.adjusted(0, 0, -1, -1));
            qp->fillRect(QRect(r.left(), r.top(), 4, r.height()), ACC);
        } else if (hover || focus) {
            qp->setPen(focus ? ACC_D : LINE2);
            qp->drawRect(r.adjusted(0, 0, -1, -1));
        }
        qp->setFont(mono(12));
        qp->setPen(selected ? ACC : DIM);
        qp->drawText(r.adjusted(18, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("%1").arg(index.row() + 1, 2, 10, QLatin1Char('0')));
        qp->setFont(head(17));
        qp->setPen(selected ? ACC : TXT);
        qp->drawText(r.adjusted(46, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, index.data().toString().toUpper());
        qp->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {214, 42}; }
};

}

SideNav::SideNav(QWidget *parent)
    : QListWidget(parent)
{
    setObjectName("armedNav");
    setItemDelegate(new NavDelegate(this));
    setFixedWidth(214);
    setMouseTracking(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAccessibleName(tr("Pages"));
}

void SideNav::setEntries(const QStringList &titles, int current)
{
    QSignalBlocker block(this);
    clear();
    for (const QString &t : titles) {
        auto *item = new QListWidgetItem(t);
        item->setData(Qt::AccessibleTextRole, t);
        addItem(item);
    }
    setCurrentRow(current);
}
