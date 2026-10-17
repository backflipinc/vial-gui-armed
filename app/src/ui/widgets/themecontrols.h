#pragma once

#include "model/theme.h"

#include <QListWidget>
#include <QMetaType>
#include <QWidget>

// layer list items carry a copy of their layer for painting
Q_DECLARE_METATYPE(theme::Layer)

// Controls of the theme editor: colour button with the palette, direction dial, layer list
class ColorButton : public QWidget {
    Q_OBJECT

public:
    explicit ColorButton(const QString &label = {}, QWidget *parent = nullptr);
    void setColor(const theme::Rgb &c);
    theme::Rgb color() const { return color_; }
    void setLabel(const QString &l);
    QSize sizeHint() const override { return {130, 30}; }

signals:
    void changed(const theme::Rgb &c);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    void openMenu();
    QString label_;
    theme::Rgb color_{0, 0, 0};
};

// 0..255 = a full turn, 0 points right, 64 down; snaps to the 8 compass points unless Shift is held
class DirectionDial : public QWidget {
    Q_OBJECT

public:
    explicit DirectionDial(QWidget *parent = nullptr);
    void setValue(int v, bool radial);

signals:
    void changed(int v);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override { drag(e); }
    void mouseMoveEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void drag(QMouseEvent *e);
    int value_ = 0;
    bool radial_ = false;
};

void layerSwatch(QPainter &qp, const QRect &r, const theme::Layer &l);
QString layerSummary(const theme::Layer &l);

// Top layer first; clicking ON / OFF toggles a layer without selecting it
class LayerList : public QListWidget {
    Q_OBJECT

public:
    explicit LayerList(QWidget *parent = nullptr);
    static QRect eyeRect(const QRect &row);

signals:
    void toggled(int row);

protected:
    void mousePressEvent(QMouseEvent *e) override;
};
