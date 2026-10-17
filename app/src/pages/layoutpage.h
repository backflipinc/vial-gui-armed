#pragma once

#include "model/layoutoptions.h"
#include "ui/page.h"

class KeyboardView;
class QGridLayout;

// Layout: the physical options VIA definitions offer (split backspace, bottom row...)
class LayoutPage : public Page {
    Q_OBJECT

public:
    explicit LayoutPage(Controller *ctl);
    void keyboardChanged() override;

private:
    void sync();
    void changed();

    KeyboardView *view_;
    QGridLayout *grid_;
    QList<layoutoptions::Choice> choices_;
    QList<QWidget *> editors_;
    bool updating_ = false;
};
