#include "app/agenthost.h"

#include "core/paths.h"
#include "protocol/armed.h"
#include "ui/theme.h"

#include <QDir>
#include <QPointer>

namespace {

constexpr int MAX_INFLIGHT = 8; // values resend every second and texts before their TTL, so dropping is safe

}

AgentHost *AgentHost::instance(Controller *ctl)
{
    static QPointer<AgentHost> h;
    if (!h)
        h = new AgentHost(ctl);
    return h;
}

AgentHost::AgentHost(Controller *ctl)
    : QObject(ctl)
    , ctl_(ctl)
    , lock_((QDir().mkpath(paths::configDir()), paths::configDir() + "/hud-agent.lock"))
{
    lock_.setStaleLockTime(0);
    connect(ctl, &Controller::keyboardChanged, this, &AgentHost::onKeyboard);
    connect(ctl, &Controller::busyChanged, this, [this] {
        attach();
        emit changed();
    });
    connect(&timer_, &QTimer::timeout, this, &AgentHost::tick);
    connect(&lockTimer_, &QTimer::timeout, this, &AgentHost::tryLock);
    timer_.start(250);
    lockTimer_.start(5000);
    tryLock();
    onKeyboard();
}

void AgentHost::tryLock()
{
    if (locked_)
        return;
    locked_ = lock_.tryLock(0);
    if (locked_) {
        attach();
        emit changed();
    }
}

void AgentHost::onKeyboard()
{
    agent_.detach();
    hasHud_ = false;
    inflight_ = 0;
    const quint64 serial = ++serial_;
    const KeyboardState *kb = ctl_->keyboard();
    if (!kb || !kb->armed || !kb->armed->has(armed::F_HUD) || !ctl_->session()) {
        emit changed();
        return;
    }
    ctl_->session()->run(hid::Priority::Background, this, [](hid::Link &link) { return armed::hudInfo(link); },
                         [this, serial](hid::Result<std::optional<armed::HudInfo>> r) {
        if (serial != serial_)
            return;
        hasHud_ = true;
        textLen_ = r.ok() && *r.value ? (*r.value)->textLen : 160;
        attach();
        emit changed();
    });
    emit changed();
}

void AgentHost::attach()
{
    if (!hasHud_ || !locked_ || ctl_->busy() || !ctl_->session()) {
        agent_.detach();
        return;
    }
    if (agent_.send)
        return;
    const quint64 serial = serial_;
    agent_.attach([this, serial](const QList<QByteArray> &packets) {
        if (serial != serial_ || !ctl_->session())
            return false;
        if (inflight_ >= MAX_INFLIGHT)
            return true;
        ++inflight_;
        ctl_->session()->run(hid::Priority::Background, this, [packets](hid::Link &link) {
            for (const auto &p : packets)
                link.write(p);
        }, [this, serial](hid::Result<hid::Unit> r) {
            if (serial != serial_)
                return;
            --inflight_;
            if (!r.ok() && agent_.send) {
                agent_.lastError = r.error;
                agent_.detach();
                emit changed();
            }
        });
        return true;
    }, textLen_);
}

void AgentHost::tick()
{
    const bool was = feeding();
    agent_.tick();
    if (was && !feeding())
        emit changed();
}

std::pair<QColor, QString> AgentHost::status() const
{
    using namespace ui::theme;
    if (!locked_)
        return {WARN, tr("ANOTHER VIAL ARMED OR `vial-armed --agent` FEEDS THE HUD")};
    if (!hasHud_)
        return {LINE2, tr("NO KEYBOARD WITH A HUD")};
    if (ctl_->busy())
        return {ACC, tr("PAUSED WHILE A TRANSFER RUNS")};
    if (!feeding())
        return {WARN, tr("STOPPED: ") + agent_.lastError};
    return {OK, tr("FEEDING THE KEYBOARD")};
}
