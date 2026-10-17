#pragma once

#include "app/controller.h"

#include <QFrame>
#include <QTimer>

class QLabel;

// Short notices over the bottom of the window instead of message boxes: nothing to dismiss, nothing blocks
class Toast : public QFrame {
    Q_OBJECT

public:
    explicit Toast(QWidget *host);
    void show(const QString &text, Controller::ToastKind kind);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    void place();

    QLabel *label_;
    QTimer timer_;
};
