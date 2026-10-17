#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>

// Keycode legends under the chosen country layout (Keyboard layout menu): KC_Q may read "Й"
class KeycodeDisplay : public QObject {
    Q_OBJECT

public:
    static KeycodeDisplay &instance();

    void setKeymap(const QString &title); // a title from keycodes::tables::keymaps()
    QString keymap() const { return title_; }

    bool overridden(const QString &code) const;
    QString label(const QString &code) const;

    struct Legend {
        QString text, maskText, tooltip;
        bool masked = false;
        QColor color, maskColor; // invalid: the default
    };
    Legend legend(const QString &code) const;

signals:
    void changed();

private:
    QString title_ = "QWERTY";
    QHash<QString, QString> override_;
};
