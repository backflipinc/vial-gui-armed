#pragma once

#include "keycodes/keycodes.h"
#include "model/kle.h"

#include <QWidget>

#include <functional>

class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QTabBar;

// One painted surface of keycode buttons, optionally under a keyboard picture: thousands of keycodes without
// thousands of widgets
class KeycodeGrid : public QWidget {
    Q_OBJECT

public:
    struct Alternative {
        QList<kle::Key> picture; // labels[0] = keycode name
        QList<const keycodes::Keycode *> buttons;
    };

    explicit KeycodeGrid(QWidget *parent = nullptr);
    void setAlternatives(QList<Alternative> alts, QStringList prefix = {});
    void setFilter(std::function<bool(const QString &)> filter);
    bool hasButtons() const;
    void relabel();

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

signals:
    void keycodeClicked(const QString &qmkId);
    void prefixClicked(const QString &title);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    bool event(QEvent *e) override;

private:
    struct Item {
        QRect rect;
        QString id;      // keycode name, or the prefix button's title
        bool prefix = false;
        QString label, tooltip;
        bool overridden = false;
    };
    int layoutItems(int width, QList<Item> *out) const;
    int itemAt(const QPoint &p) const;
    void activate(int i);
    int unit() const;

    QList<Alternative> alts_;
    QStringList prefix_;
    std::function<bool(const QString &)> filter_;
    QList<Item> items_;
    int hover_ = -1, focus_ = -1;
};

// The keycode picker under the keyboard: tabs of keycodes, and a search across all of them
class KeycodePalette : public QWidget {
    Q_OBJECT

public:
    explicit KeycodePalette(QWidget *parent = nullptr);

    // basic keycodes only (the inner part of a masked keycode)
    void setBasicOnly(bool on);
    // after a keyboard (re)load: layers, macros, tap dances... changed
    void rebuild();

signals:
    void keycodeSelected(const QString &qmkId); // normalized
    void anyKeycodeRequested();

private:
    void onPicked(const QString &qmkId);
    void applySearch();

    QTabBar *tabs_;
    QLineEdit *search_;
    QStackedWidget *pages_;
    QList<KeycodeGrid *> grids_;
    QList<QString> titles_;
    KeycodeGrid *results_;
    QScrollArea *resultsArea_;
    QList<QScrollArea *> areas_;
    bool basicOnly_ = false;
};
