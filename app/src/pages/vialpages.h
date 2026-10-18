#pragma once

#include "ui/page.h"

#include <QTimer>

class KeyboardView;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;

class QmkSettingsPage : public Page {
    Q_OBJECT
public:
    explicit QmkSettingsPage(Controller *ctl);
    void keyboardChanged() override;

private:
    void setValue(quint16 qsid, quint32 value);
    QTabWidget *tabs_;
};

class MatrixPage : public Page {
    Q_OBJECT
public:
    explicit MatrixPage(Controller *ctl);
    void keyboardChanged() override;
    void activate() override;
    void deactivate() override;

private:
    void poll();
    void reset();

    KeyboardView *view_;
    QLabel *lockHint_;
    QPushButton *unlock_;
    QTimer timer_;
    QWidget *grabber_;
    bool inFlight_ = false;
    int ticks_ = 0;
    QByteArray last_;
};

class FirmwarePage : public Page {
    Q_OBJECT
public:
    explicit FirmwarePage(Controller *ctl);
    void keyboardChanged() override;

private:
    void selectFile();
    void flash();
    void log(const QString &line);
    void waitForBootloader(int tries);
    void flashBootloader(const QByteArray &path);
    void finished(bool ok, const QString &msg);

    QLineEdit *file_;
    QPlainTextEdit *log_;
    QProgressBar *progress_;
    QPushButton *flash_, *select_;
    QCheckBox *restore_;
    QByteArray firmware_;
    QByteArray uid_;
    QByteArray savedLayout_;
    bool pendingRestore_ = false;
};

class RgbPage : public Page {
    Q_OBJECT
public:
    explicit RgbPage(Controller *ctl);
    void keyboardChanged() override;

private:
    QWidget *body_ = nullptr;
};
