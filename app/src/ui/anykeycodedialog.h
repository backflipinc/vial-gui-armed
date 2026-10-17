#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;

// Any keycode: an expression like LCTL(KC_A), LT(2, KC_SPC) or 0x7C00
class AnyKeycodeDialog : public QDialog {
    Q_OBJECT

public:
    AnyKeycodeDialog(const QString &initial, QWidget *parent = nullptr);
    QString value() const { return value_; }

private:
    void onChange();

    QLineEdit *entry_;
    QLabel *computed_;
    QDialogButtonBox *buttons_;
    QString value_;
};
