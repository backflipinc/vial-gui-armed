#pragma once

#include "ui/page.h"

#include <QMap>
#include <QTimer>

class BracketFrame;
class QLabel;
class Segmented;

// Device: the keyboard menu's options Vial doesn't cover (NKRO, Win lock, SnapTap, Bluetooth slot, auto-sleep)
class ArmedDevicePage : public Page {
    Q_OBJECT

public:
    explicit ArmedDevicePage(Controller *ctl);
    void keyboardChanged() override;
    void activate() override;
    void deactivate() override;

private:
    struct Row {
        QLabel *label;
        Segmented *seg;
        QLabel *note;
    };
    Segmented *row(BracketFrame *frame, quint8 vid, const QString &label, const QStringList &labels, const QString &hint);
    void load();
    void refreshStatus();

    BracketFrame *keyboardFrame_, *wirelessFrame_, *powerFrame_;
    QMap<quint8, Row> rows_;
    QLabel *snaptapNow_, *linkNow_;
    QList<int> sleepOrder_;
    QTimer timer_;
};
