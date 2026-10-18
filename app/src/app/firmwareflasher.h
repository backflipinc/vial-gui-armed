#pragma once

#include "app/controller.h"
#include "protocol/vibl.h"

#include <QObject>
#include <QStringList>

// Flashing a .vfw through the Vial bootloader. Outlives the pages: the keyboard leaves and comes back meanwhile.
class FirmwareFlasher : public QObject {
    Q_OBJECT

public:
    static FirmwareFlasher *instance(Controller *ctl);

    bool running() const { return running_; }
    const QStringList &logLines() const { return log_; }
    double progress() const { return progress_; }

    // restore: put the current layout back once the new firmware runs
    void start(const QByteArray &firmware, bool restore);

signals:
    void logged(const QString &line);
    void progressed(double p);
    void runningChanged(bool running);

private:
    explicit FirmwareFlasher(Controller *ctl);
    void log(const QString &line);
    void enterBootloader();
    void waitForBootloader(int triesLeft);
    void flashAt(const QByteArray &path);
    void finish(bool ok, const QString &msg);
    void onKeyboard();

    Controller *ctl_;
    QStringList log_;
    double progress_ = 0;
    bool running_ = false;
    vibl::Package pkg_;
    QByteArray uid_;
    std::optional<QByteArray> layout_;
};
