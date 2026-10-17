#pragma once

#include "protocol/armed.h"

#include <QFrame>
#include <QList>
#include <QWidget>

#include <array>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QSlider;
class QVBoxLayout;

// Panel with the corner brackets of the design code
class BracketFrame : public QFrame {
    Q_OBJECT

public:
    explicit BracketFrame(const QString &title = {}, bool accent = false, QWidget *parent = nullptr);
    void setAccent(bool on);
    QVBoxLayout *box() const { return box_; }
    QHBoxLayout *header() const { return header_; }
    QLabel *title() const { return title_; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVBoxLayout *box_;
    QHBoxLayout *header_ = nullptr;
    QLabel *title_ = nullptr;
    bool accent_;
};

// A row of the keyboard's preset colours; emits the index
class Swatches : public QWidget {
    Q_OBJECT

public:
    explicit Swatches(QWidget *parent = nullptr);
    void setColors(const QList<armed::Color> &colors);
    void setCurrent(int index);
    int current() const { return current_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    static QColor color(const armed::Color &c);

signals:
    void changed(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool event(QEvent *e) override;

private:
    QRect cell(int i) const;
    QList<armed::Color> colors_;
    int current_ = -1;
};

// Exclusive row of buttons; emits the index
class Segmented : public QWidget {
    Q_OBJECT

public:
    explicit Segmented(const QStringList &labels = {}, QWidget *parent = nullptr);
    void setLabels(const QStringList &labels);
    void setCurrent(int index);

signals:
    void changed(int index);

private:
    QHBoxLayout *layout_;
    QButtonGroup *group_;
};

// Preview of the 9-LED light bar
class StripView : public QWidget {
    Q_OBJECT

public:
    explicit StripView(QWidget *parent = nullptr);
    void setColors(const QList<std::array<quint8, 3>> &colors);
    QSize sizeHint() const override { return {9 * 30, 16}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QList<std::array<quint8, 3>> colors_;
};

// "NAME  [slider]  value": a labelled slider row
QHBoxLayout *sliderRow(const QString &name, QSlider *slider, bool percent = false);
QSlider *armedSlider(int maximum);
