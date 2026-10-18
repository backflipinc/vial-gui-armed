#pragma once

#include "ui/widgets/keyboardview.h"

#include <functional>

// One keycap holding a keycode, for editors (macros, tap dance, combos...): click it to pick from the keycode
// tray, double-click for any keycode, Delete clears
class KeyButton : public KeyboardView {
    Q_OBJECT

public:
    explicit KeyButton(QWidget *parent = nullptr);
    ~KeyButton() override;

    QString keycode() const { return code_; }
    void setKeycode(const QString &code); // no changed() signal
    // only basic keycodes (older Vial macros store one byte)
    void setBasicOnly(bool on) { basicOnly_ = on; }

    // from the tray: the inner part of a masked keycode keeps its outer one
    void pick(const QString &code);
    void anyKeycode();

signals:
    void changed(const QString &code);

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void refresh();
    void apply(const QString &code);

    QString code_ = "KC_NO";
    bool basicOnly_ = false;
};

// The keycode palette at the bottom of the window, serving whichever KeyButton was clicked last
class KeycodeTray : public QWidget {
    Q_OBJECT

public:
    static KeycodeTray *instance();
    explicit KeycodeTray(QWidget *parent = nullptr);
    ~KeycodeTray() override;

    void open(KeyButton *target, bool basicOnly);
    void close();
    KeyButton *target() const { return target_; }
    void rebuild();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    class KeycodePalette *palette();
    class KeycodePalette *palette_ = nullptr;
    KeyButton *target_ = nullptr;
};
