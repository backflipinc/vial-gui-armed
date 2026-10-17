#pragma once

#include "model/definition.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPainterPath>
#include <QPolygonF>
#include <QWidget>

#include <functional>
#include <optional>

// A keyboard drawn from its KLE layout: keycaps with legends, encoders, a selected key. Used by every page that
// shows the keyboard (keymap, lighting, matrix tester...).
class KeyboardView : public QWidget {
    Q_OBJECT

public:
    struct Key {
        LayoutKey def;
        bool encoder = false;
        // what is shown
        QString text, maskText, tooltip;
        bool masked = false, on = false, pressed = false, active = false;
        QColor color, maskColor;
        std::optional<QColor> glow; // keycap lit by an LED preview
        // geometry, in unscaled widget units
        double size = 0, shiftX = 0, shiftY = 0, rotX = 0, rotY = 0;
        QRectF rect, rect2, nonmaskRect, maskRect;
        QPolygonF polygon, maskPolygon;
        QPainterPath background, extra;
    };

    explicit KeyboardView(QWidget *parent = nullptr);

    // layout keys of the keyboard; encoders are drawn as knobs
    void setKeys(const QList<LayoutKey> &keys, const QList<LayoutKey> &encoders);
    // which option each layout choice (index -> option) shows
    void setLayoutChoice(std::function<int(int)> choice);
    void relayout();

    QList<Key> &keys() { return visible_; }
    int activeIndex() const { return active_; }
    bool activeMask() const { return activeMask_; }
    void setActive(int index, bool mask = false);
    void selectNext();
    void deselect();

    double scale() const { return scale_; }
    void setScale(double s);
    // clicks select keys; read-only views (lighting preview) can turn selection off
    void setSelectable(bool on) { selectable_ = on; }

    // the size at scale 1
    QSize naturalSize() const { return natural_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    // the scale it was given, before a container fitted it
    double preferredScale() const { return preferred_; }

signals:
    void clicked(int index, bool mask);
    void keyPressed(int index); // any press on a key, even when selection is off
    void deselected();
    void doubleClicked(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    bool event(QEvent *e) override;
    void changeEvent(QEvent *e) override;
    // key index and whether the inner (masked) part was hit, at a widget position
    std::pair<int, bool> hit(const QPointF &pos) const;
    // paints in the key's own coordinates (rotation included)
    void transformTo(QPainter &qp, const Key &k) const;

private:
    void place(Key &k, double unit, double shiftX, double shiftY) const;
    QPolygonF mapped(const Key &k, const QRectF &r) const;
    QFont fitted(const QFont &base, const QString &text, double width) const;

    QList<Key> all_;     // every key with its layout option
    QList<Key> visible_; // the ones the current layout choice shows
    std::function<int(int)> choice_;
    double scale_ = 1.0;
    double preferred_ = 1.0;
    friend class FittedKeyboard;
    int active_ = -1;
    bool activeMask_ = false;
    bool selectable_ = true;
    QSize size_, natural_;
    mutable QHash<QPair<QString, int>, QFont> fitCache_;
};

// A KeyboardView that shrinks to the width it gets (never larger than its preferred scale): pages stay usable in
// small windows without forcing the window wide
class FittedKeyboard : public QWidget {
    Q_OBJECT

public:
    explicit FittedKeyboard(KeyboardView *view, QWidget *parent = nullptr);
    KeyboardView *view() const { return view_; }
    void setPreferredScale(double s);
    void fit();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {160, 60}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool event(QEvent *e) override;

private:
    KeyboardView *view_;
};
