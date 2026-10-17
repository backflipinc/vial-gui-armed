#pragma once

#include "app/controller.h"

#include <QWidget>

#include <functional>

// One screen of the app. Pages are built the first time they are shown, and only for keyboards they apply to.
class Page : public QWidget {
    Q_OBJECT

public:
    explicit Page(Controller *ctl, QWidget *parent = nullptr)
        : QWidget(parent)
        , ctl(ctl)
    {
    }

    virtual void activate() {}
    virtual void deactivate() {}
    // a keyboard finished loading (or a new one replaced it)
    virtual void keyboardChanged() {}

protected:
    Controller *ctl;
};

struct PageSpec {
    QString title;
    std::function<Page *(Controller *)> make;
};

// a navigation entry: one page, or several shown as sub-pages
struct NavEntry {
    QString title;
    std::function<bool(const Controller &)> available;
    QList<PageSpec> parts;
};
