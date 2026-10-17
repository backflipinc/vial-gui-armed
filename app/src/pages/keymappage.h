#pragma once

#include "ui/page.h"

class KeyboardView;
class KeycodePalette;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QSplitter;

// Keymap: pick a key on the keyboard, then a keycode below
class KeymapPage : public Page {
    Q_OBJECT

public:
    explicit KeymapPage(Controller *ctl);

    void keyboardChanged() override;

protected:
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void refresh();
    void setLayer(int layer);
    QString codeOf(int keyIndex) const;
    void assign(const QString &code);
    void onKeyClicked(int index, bool mask);
    void anyKeycode();
    void zoom(double delta);
    void fit();

    KeyboardView *view_;
    KeycodePalette *palette_;
    QHBoxLayout *layerRow_;
    QButtonGroup *layers_;
    QLabel *selection_;
    QScrollArea *viewArea_ = nullptr;
    QSplitter *split_ = nullptr;
    double userScale_ = 1.0;
    int layer_ = 0;
    qint64 shownOptions_ = -1;
};
