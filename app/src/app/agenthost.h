#pragma once

#include "agent/hudagent.h"
#include "app/controller.h"

#include <QColor>
#include <QLockFile>
#include <QObject>
#include <QTimer>

#include <memory>

// Runs the HUD agent inside Vial Armed while no other agent holds the lock. Packets go through the session's
// worker as background jobs, so feeding never waits on the device or delays the pages' traffic.
class AgentHost : public QObject {
    Q_OBJECT

public:
    static AgentHost *instance(Controller *ctl);

    agent::HudAgent &agent() { return agent_; }
    bool feeding() const { return bool(agent_.send); }
    std::pair<QColor, QString> status() const;

signals:
    void changed();

private:
    explicit AgentHost(Controller *ctl);
    void onKeyboard();
    void tryLock();
    void attach();
    void tick();

    Controller *ctl_;
    agent::HudAgent agent_;
    QLockFile lock_;
    bool locked_ = false;
    bool hasHud_ = false;
    int textLen_ = 160;
    int inflight_ = 0;
    quint64 serial_ = 0;
    QTimer timer_, lockTimer_;
};
