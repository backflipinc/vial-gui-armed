#pragma once

#include "model/screenmedia.h"
#include "protocol/armed.h"
#include "ui/page.h"

#include <QPixmap>
#include <QTimer>

#include <atomic>
#include <memory>

class BracketFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class Segmented;

// Screen > SETTINGS: main screen mode, display accent and screen-off time, clock
class ScreenSettingsPage : public Page {
    Q_OBJECT
public:
    explicit ScreenSettingsPage(Controller *ctl);
    void keyboardChanged() override;
    void activate() override;

private:
    void load();
    Segmented *mode_, *accent_, *screenOff_, *clockFmt_;
    BracketFrame *display_, *clock_;
    QLabel *clockStatus_;
};

// Screen > ANIMATION: the stored animation, backups, and a new picture / GIF / video upload
class AnimationPage : public Page {
    Q_OBJECT
public:
    explicit AnimationPage(Controller *ctl);
    void keyboardChanged() override;
    void activate() override;
    void deactivate() override;

private:
    void refreshInfo();
    void showInfo();
    void openFiles();
    void prepare();
    void upload(const QList<screenmedia::Frame> &frames, const QByteArray &stream, const QString &label);
    void backup();
    void restore();
    void previewTick();
    void startJob(const QString &label, double rate);
    void progress(qint64 done, qint64 total);
    void endJob(const QString &status);
    void updateButtons();

    std::optional<armed::ScreenInfo> info_;
    QStringList paths_;
    QList<screenmedia::Frame> frames_;
    QByteArray stream_;
    QList<QPixmap> previews_;
    int previewIdx_ = 0;
    QTimer previewTimer_;
    bool busy_ = false;
    double rate_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    quint64 prepareSerial_ = 0;

    QLabel *stored_, *preview_, *files_, *summary_, *jobLabel_, *eta_, *status_;
    QPushButton *backup_, *restore_, *open_, *upload_, *cancelBtn_;
    Segmented *fit_, *fps_, *slide_;
    QProgressBar *bar_;
};
