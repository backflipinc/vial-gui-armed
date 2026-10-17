#include "pages/armeddevice.h"

#include "model/hud.h"
#include "ui/theme.h"
#include "ui/widgets/armedcontrols.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace armed;

ArmedDevicePage::ArmedDevicePage(Controller *ctl)
    : Page(ctl)
    , keyboardFrame_(new BracketFrame(tr("KEYBOARD")))
    , wirelessFrame_(new BracketFrame(tr("WIRELESS")))
    , powerFrame_(new BracketFrame(tr("POWER")))
    , snaptapNow_(new QLabel)
    , linkNow_(new QLabel)
{
    row(keyboardFrame_, V_NKRO, tr("NKRO"), {"OFF", "ON"}, tr("All keys at once instead of six. Also in the menu: SYSTEM > NKRO."));
    row(keyboardFrame_, V_GUI_LOCK, tr("WIN LOCK"), {"OFF", "ON"}, tr("Turns the Win keys off, e.g. for games. Also SYSTEM > WIN LOCK."));
    row(keyboardFrame_, V_SNAPTAP, tr("SNAPTAP"), {},
        tr("When both keys of a pair are held: LAST — the last pressed wins, NEUTRAL — neither. "
           "The switch on the case turns SnapTap on."));
    keyboardFrame_->box()->addWidget(snaptapNow_);
    row(wirelessFrame_, V_BT_SLOT, tr("BT SLOT"), {"1", "2", "3", "4", "5"},
        tr("Which paired host Bluetooth mode connects to. Pairing: WIRELESS > PAIR NOW in the menu, or the pairing "
           "keys on the Fn layer."));
    wirelessFrame_->box()->addWidget(linkNow_);
    row(powerFrame_, V_SLEEP, tr("AUTO-SLEEP"), {},
        tr("In BT and 2.4 GHz the keyboard sleeps after this much idle time; a key press wakes it."));
    for (auto *f : {keyboardFrame_, wirelessFrame_, powerFrame_})
        f->box()->addStretch(1);
    auto *grid = new QGridLayout;
    grid->setSpacing(18);
    grid->addWidget(keyboardFrame_, 0, 0);
    grid->addWidget(wirelessFrame_, 0, 1);
    grid->addWidget(powerFrame_, 1, 0);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->addLayout(grid);
    l->addStretch(1);
    timer_.setInterval(2000);
    connect(&timer_, &QTimer::timeout, this, &ArmedDevicePage::refreshStatus);
    connect(ctl, &Controller::armedValuesChanged, this, &ArmedDevicePage::load);
}

Segmented *ArmedDevicePage::row(BracketFrame *frame, quint8 vid, const QString &label, const QStringList &labels,
                                const QString &hint)
{
    auto *line = new QHBoxLayout;
    auto *lbl = new QLabel(label);
    lbl->setStyleSheet(QStringLiteral("color: %1;").arg(ui::theme::MID.name()));
    lbl->setFixedWidth(110);
    auto *seg = new Segmented(labels);
    connect(seg, &Segmented::changed, this, [this, vid](int pos) {
        if (vid == V_SLEEP)
            pos = sleepOrder_.value(pos);
        else if (vid == V_BT_SLOT)
            pos += 1;
        this->ctl->armedSet(vid, pos);
        refreshStatus();
    });
    line->addWidget(lbl);
    line->addWidget(seg);
    line->addStretch(1);
    frame->box()->addLayout(line);
    auto *note = new QLabel(hint);
    note->setObjectName("armedHint");
    note->setWordWrap(true);
    frame->box()->addWidget(note);
    rows_.insert(vid, {lbl, seg, note});
    return seg;
}

void ArmedDevicePage::keyboardChanged()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed)
        return;
    const Info &io = *kb->armed;
    // firmware order is the Fn+S cycle; show ascending with OFF last
    sleepOrder_.clear();
    for (int i = 0; i < io.sleepMinutes.size(); ++i)
        sleepOrder_.append(i);
    std::stable_sort(sleepOrder_.begin(), sleepOrder_.end(), [&io](int a, int b) {
        auto key = [&io](int i) { return io.sleepMinutes[i] ? io.sleepMinutes[i] : 1000000; };
        return key(a) < key(b);
    });
    QStringList sleepLabels;
    for (int i : sleepOrder_)
        sleepLabels << (io.sleepMinutes[i] ? QStringLiteral("%1M").arg(io.sleepMinutes[i]) : QStringLiteral("OFF"));
    rows_[V_SLEEP].seg->setLabels(sleepLabels);
    rows_[V_SNAPTAP].seg->setLabels(io.snaptapModes);
    load();
}

void ArmedDevicePage::load()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed)
        return;
    const Info &io = *kb->armed;
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
        const quint8 vid = it.key();
        const bool shown = io.values.contains(vid) && !it->seg->findChildren<QWidget *>().isEmpty();
        for (QWidget *w : {static_cast<QWidget *>(it->label), static_cast<QWidget *>(it->seg), static_cast<QWidget *>(it->note)})
            w->setVisible(shown);
        if (!shown)
            continue;
        int v = io.values.value(vid);
        if (vid == V_SLEEP)
            v = int(sleepOrder_.indexOf(v));
        else if (vid == V_BT_SLOT)
            v -= 1;
        it->seg->setCurrent(v);
    }
    keyboardFrame_->setVisible(io.values.contains(V_NKRO) || io.values.contains(V_GUI_LOCK) || io.values.contains(V_SNAPTAP));
    wirelessFrame_->setVisible(io.values.contains(V_BT_SLOT));
    powerFrame_->setVisible(io.values.contains(V_SLEEP));
    refreshStatus();
}

void ArmedDevicePage::refreshStatus()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed || !kb->armed->has(F_HUD) || !ctl->session()) {
        snaptapNow_->hide();
        linkNow_->hide();
        return;
    }
    // live state from the HUD sources
    ctl->session()->run(hid::Priority::Poll, this, [](hid::Link &link) {
        return std::pair{hudValue(link, hud::S_SNAPTAP), hudValue(link, hud::S_LINK)};
    }, [this](hid::Result<std::pair<std::optional<HudValue>, std::optional<HudValue>>> r) {
        if (!r.ok())
            return;
        auto show = [](QLabel *l, const std::optional<HudValue> &v, const QString &fmt) {
            l->setVisible(v.has_value());
            if (v)
                l->setText(fmt.arg(!v->text.isEmpty() ? v->text : (v->num ? QStringLiteral("ON") : QStringLiteral("OFF"))));
        };
        show(snaptapNow_, r.value->first, tr("SWITCH: %1"));
        show(linkNow_, r.value->second, tr("NOW: %1"));
    }, QStringLiteral("device-status"));
}

void ArmedDevicePage::activate()
{
    ctl->armedReload();
    timer_.start();
}

void ArmedDevicePage::deactivate()
{
    timer_.stop();
}
