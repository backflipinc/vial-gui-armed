#pragma once

#include "model/hud.h"
#include "render/hudrender.h"

#include <QImage>
#include <QWidget>

#include <optional>

// The HUD editor's surface: the screen at an integer zoom, selection, moving with smart guides, resizing
class HudCanvas : public QWidget {
    Q_OBJECT

public:
    explicit HudCanvas(QWidget *parent = nullptr);

    void setScreen(int w, int h);
    void setScale(int s);
    int scale() const { return scale_; }
    void setContent(const QImage &image, hud::Layout *layout, const QList<std::optional<hud::Box>> &areas);

    QList<int> selection() const { return sel_; }
    void setSelection(const QList<int> &sel);
    int primary() const { return sel_.isEmpty() ? -1 : sel_.last(); }
    bool snap = true;
    bool showBounds = false;

signals:
    void selectionChanged();
    void editStarted();
    void edited();
    void contextRequested(const QPoint &global);
    void cursorMoved(int x, int y); // screen px, -1 when outside
    void nudge(int dx, int dy);
    void activated(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    struct Drag {
        bool size = false; // false: move
        int x0 = 0, y0 = 0;
        QMap<int, QPoint> start; // move: widget -> x, y; size: widget -> w, h
        bool started = false;
    };
    QPoint toPx(const QPoint &pos) const;
    QRect rectOf(const hud::Box &a) const;
    std::optional<QRect> handle() const;
    std::optional<hud::Box> area(int i) const;
    int pick(int x, int y) const;
    std::pair<QList<int>, QList<int>> targets(const QSet<int> &exclude) const;
    std::optional<std::pair<int, int>> snapAxis(const QList<int> &edges, const QList<int> &targets) const;
    QRect bandRect() const;

    QImage image_;
    int scale_ = 2, w_ = 320, h_ = 172;
    hud::Layout *layout_ = nullptr;
    QList<std::optional<hud::Box>> areas_;
    QList<int> sel_;
    int hover_ = -1;
    std::optional<Drag> drag_;
    std::optional<std::pair<QPoint, QPoint>> band_;
    QList<int> bandBase_;
    QList<std::pair<char, int>> guides_;
    int clickOnly_ = -1;
};
