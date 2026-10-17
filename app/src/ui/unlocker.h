#pragma once

#include "app/controller.h"

#include <QDialog>
#include <QTimer>

#include <functional>

class KeyboardView;
class QProgressBar;

// Vial's unlock: hold the keys the keyboard names until the bar fills. Asynchronous: done(true) once unlocked.
class Unlocker : public QDialog {
    Q_OBJECT

public:
    // done(true) right away when the keyboard is already unlocked
    static void ensure(Controller *ctl, QWidget *parent, std::function<void(bool)> done);

private:
    Unlocker(Controller *ctl, QWidget *parent);
    void start();
    void poll();

    Controller *ctl_;
    KeyboardView *view_;
    QProgressBar *progress_;
    QTimer timer_;
    int maxCounter_ = 1;
};
