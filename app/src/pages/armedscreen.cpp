#include "pages/armedscreen.h"

#include "ui/theme.h"
#include "ui/widgets/armedcontrols.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

using namespace armed;

namespace {

constexpr int FPS_CHOICES[] = {5, 10, 15, 20, 25};
constexpr int SLIDE_MS[] = {1000, 2000, 3000, 5000};
constexpr double UPLOAD_RATE = 26 * 1024; // stream bytes per second over Vial's raw HID, measured
constexpr double READ_RATE = 25 * 1024;

QString kib(qint64 n)
{
    return QStringLiteral("%1 KIB").arg((n + 1023) / 1024);
}

QString duration(double seconds)
{
    const int s = int(std::round(seconds));
    return s >= 60 ? QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'))
                   : QStringLiteral("%1 S").arg(std::max(1, s));
}

QLabel *hint(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("armedHint");
    l->setWordWrap(true);
    return l;
}

QLabel *key(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("color: %1;").arg(ui::theme::MID.name()));
    return l;
}

}

// ---- settings

ScreenSettingsPage::ScreenSettingsPage(Controller *ctl)
    : Page(ctl)
    , mode_(new Segmented({"HUD", "ANIMATION"}))
    , accent_(new Segmented)
    , screenOff_(new Segmented)
    , clockFmt_(new Segmented({"24H", "12H", "OFF"}))
    , display_(new BracketFrame(tr("DISPLAY")))
    , clock_(new BracketFrame(tr("CLOCK")))
    , clockStatus_(new QLabel)
{
    auto *main = new BracketFrame(tr("MAIN SCREEN"));
    auto *row = new QHBoxLayout;
    row->addWidget(mode_);
    row->addStretch(1);
    main->box()->addLayout(row);
    main->box()->addWidget(hint(tr("What the keyboard shows when the menu is closed. Also in its menu: DISPLAY > MAIN SCREEN.")));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->addWidget(key(tr("ACCENT")), 0, 0);
    grid->addWidget(accent_, 0, 1, Qt::AlignLeft);
    grid->addWidget(key(tr("SCREEN OFF")), 1, 0);
    grid->addWidget(screenOff_, 1, 1, Qt::AlignLeft);
    grid->setColumnStretch(2, 1);
    display_->box()->addLayout(grid);
    display_->box()->addWidget(hint(tr("The accent colours the menu and HUD widgets set to ACCENT. The screen turns off "
                                       "after this much idle time, a key press wakes it. Also in its menu: DISPLAY.")));

    auto *crow = new QHBoxLayout;
    crow->addWidget(clockFmt_);
    crow->addStretch(1);
    auto *sync = new QPushButton(tr("SYNC NOW"));
    connect(sync, &QPushButton::clicked, ctl, &Controller::syncClock);
    crow->addWidget(sync);
    clock_->box()->addLayout(crow);
    clock_->box()->addWidget(clockStatus_);
    clock_->box()->addWidget(hint(tr("Shown on the HUD under WPM and battery. The keyboard's own clock chip keeps time "
                                     "through sleep and unplugging; Vial Armed sets it from this computer when it "
                                     "connects. Also in its menu: DISPLAY > CLOCK.")));

    auto *g = new QGridLayout;
    g->setSpacing(18);
    g->addWidget(main, 0, 0);
    g->addWidget(display_, 0, 1);
    g->addWidget(clock_, 1, 0, 1, 2);
    g->setColumnStretch(0, 1);
    g->setColumnStretch(1, 1);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->addLayout(g);
    l->addStretch(1);

    connect(mode_, &Segmented::changed, this, [this](int i) { this->ctl->armedSet(V_SCREEN, i); });
    connect(accent_, &Segmented::changed, this, [this](int i) { this->ctl->armedSet(V_ACCENT, i); });
    connect(screenOff_, &Segmented::changed, this, [this](int i) { this->ctl->armedSet(V_SCREEN_OFF, i); });
    connect(clockFmt_, &Segmented::changed, this, [this](int i) { this->ctl->armedSet(V_CLOCK, i); });
    connect(ctl, &Controller::armedValuesChanged, this, &ScreenSettingsPage::load);
    connect(ctl, &Controller::clockSynced, this, [this] { clockStatus_->setText(this->ctl->clockStatus()); });
}

void ScreenSettingsPage::keyboardChanged()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed)
        return;
    const Info &io = *kb->armed;
    clock_->setVisible(io.has(F_CLOCK));
    display_->setVisible(io.has(F_DISPLAY) && !io.accents.isEmpty());
    QStringList accents, off;
    for (const auto &c : io.accents)
        accents << c.name;
    for (const auto &o : io.screenOff)
        off << o.name;
    accent_->setLabels(accents);
    screenOff_->setLabels(off);
    mode_->parentWidget()->setVisible(io.has(F_SCREEN));
    clockStatus_->setText(ctl->clockStatus());
    load();
}

void ScreenSettingsPage::load()
{
    mode_->setCurrent(ctl->armedValue(V_SCREEN));
    clockFmt_->setCurrent(ctl->armedValue(V_CLOCK));
    accent_->setCurrent(ctl->armedValue(V_ACCENT));
    screenOff_->setCurrent(ctl->armedValue(V_SCREEN_OFF));
}

void ScreenSettingsPage::activate()
{
    ctl->armedReload();
}

// ---- animation

AnimationPage::AnimationPage(Controller *ctl)
    : Page(ctl)
    , stored_(new QLabel)
    , preview_(new QLabel)
    , files_(hint(tr("Picture, GIF or video. Several files make a slideshow.")))
    , summary_(new QLabel)
    , jobLabel_(key({}))
    , eta_(hint({}))
    , status_(hint({}))
    , backup_(new QPushButton(tr("SAVE BACKUP...")))
    , restore_(new QPushButton(tr("RESTORE BACKUP...")))
    , open_(new QPushButton(tr("OPEN FILES...")))
    , upload_(new QPushButton(tr("UPLOAD TO KEYBOARD")))
    , cancelBtn_(new QPushButton(tr("CANCEL")))
    , fit_(new Segmented({"CONTAIN", "COVER", "STRETCH"}))
    , fps_(new Segmented({"5", "10", "15", "20", "25"}))
    , slide_(new Segmented({"1 S", "2 S", "3 S", "5 S"}))
    , bar_(new QProgressBar)
{
    auto *stored = new BracketFrame(tr("ON THE KEYBOARD"));
    stored->box()->addWidget(stored_);
    auto *row = new QHBoxLayout;
    row->addWidget(backup_);
    row->addWidget(restore_);
    row->addStretch(1);
    stored->box()->addLayout(row);
    stored->box()->addWidget(hint(tr("Stored where the stock firmware keeps its own animation, in the same format. "
                                     "A backup reads ~4 s per frame.")));

    auto *fresh = new BracketFrame(tr("NEW ANIMATION"), true);
    auto *body = new QHBoxLayout;
    body->setSpacing(18);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet(QStringLiteral("background: #000; border: 1px solid %1;").arg(ui::theme::LINE2.name()));
    preview_->setFixedSize(322, 174);
    body->addWidget(preview_, 0, Qt::AlignTop);
    auto *side = new QVBoxLayout;
    side->setSpacing(10);
    auto *orow = new QHBoxLayout;
    orow->addWidget(open_);
    orow->addWidget(files_, 1);
    side->addLayout(orow);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->addWidget(key(tr("FIT")), 0, 0);
    grid->addWidget(fit_, 0, 1, Qt::AlignLeft);
    grid->addWidget(key(tr("VIDEO FPS")), 1, 0);
    grid->addWidget(fps_, 1, 1, Qt::AlignLeft);
    grid->addWidget(key(tr("SLIDE")), 2, 0);
    grid->addWidget(slide_, 2, 1, Qt::AlignLeft);
    grid->setColumnStretch(2, 1);
    side->addLayout(grid);
    summary_->setWordWrap(true);
    side->addWidget(summary_);
    auto *urow = new QHBoxLayout;
    upload_->setDefault(true);
    urow->addWidget(upload_);
    urow->addStretch(1);
    side->addLayout(urow);
    side->addStretch(1);
    body->addLayout(side, 1);
    fresh->box()->addLayout(body);
    auto *prow = new QHBoxLayout;
    bar_->setRange(0, 1000);
    bar_->setTextVisible(false);
    prow->addWidget(jobLabel_);
    prow->addWidget(bar_, 1);
    prow->addWidget(eta_);
    prow->addWidget(cancelBtn_);
    fresh->box()->addLayout(prow);
    fresh->box()->addWidget(status_);

    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->setSpacing(18);
    l->addWidget(stored);
    l->addWidget(fresh);
    l->addStretch(1);

    fit_->setCurrent(0);
    fps_->setCurrent(1);
    slide_->setCurrent(2);
    for (Segmented *s : {fit_, fps_, slide_})
        connect(s, &Segmented::changed, this, &AnimationPage::prepare);
    connect(open_, &QPushButton::clicked, this, &AnimationPage::openFiles);
    connect(upload_, &QPushButton::clicked, this, [this] {
        if (!frames_.isEmpty() && !stream_.isEmpty())
            upload(frames_, stream_, tr("UPLOADING"));
    });
    connect(backup_, &QPushButton::clicked, this, &AnimationPage::backup);
    connect(restore_, &QPushButton::clicked, this, &AnimationPage::restore);
    connect(cancelBtn_, &QPushButton::clicked, this, [this] {
        if (cancel_)
            *cancel_ = true;
    });
    connect(&previewTimer_, &QTimer::timeout, this, &AnimationPage::previewTick);
    for (QWidget *w : {static_cast<QWidget *>(jobLabel_), static_cast<QWidget *>(bar_), static_cast<QWidget *>(eta_),
                       static_cast<QWidget *>(cancelBtn_)})
        w->hide();
}

void AnimationPage::keyboardChanged()
{
    refreshInfo();
}

void AnimationPage::activate()
{
    if (!busy_)
        refreshInfo();
    if (previews_.size() > 1)
        previewTick();
}

void AnimationPage::deactivate()
{
    previewTimer_.stop();
}

void AnimationPage::refreshInfo()
{
    if (!ctl->session())
        return;
    ctl->session()->run(hid::Priority::Interactive, this, [](hid::Link &link) { return screenInfo(link); },
                        [this](hid::Result<std::optional<ScreenInfo>> r) {
        if (r.ok())
            info_ = *r.value;
        if (info_)
            preview_->setFixedSize(info_->width + 2, info_->height + 2);
        showInfo();
    });
}

void AnimationPage::showInfo()
{
    if (!info_)
        stored_->setText(QStringLiteral("—"));
    else if (info_->frames)
        stored_->setText(tr("%1 FRAMES  ·  UP TO %2").arg(info_->frames).arg(info_->maxFrames));
    else
        stored_->setText(tr("NO ANIMATION  ·  UP TO %1 FRAMES").arg(info_->maxFrames));
    updateButtons();
}

void AnimationPage::openFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Picture, GIF or video"), {},
        tr("Images and video (*.gif *.png *.jpg *.jpeg *.bmp *.webp *.mp4 *.mov *.mkv *.webm *.avi);;All files (*)"));
    if (paths.isEmpty())
        return;
    paths_ = paths;
    QStringList names;
    for (const QString &p : paths)
        names << QFileInfo(p).fileName();
    files_->setText(names.join(QStringLiteral(", ")));
    prepare();
}

void AnimationPage::prepare()
{
    previewTimer_.stop();
    frames_.clear();
    stream_.clear();
    previews_.clear();
    if (paths_.isEmpty() || !info_) {
        preview_->setPixmap({});
        summary_->clear();
        updateButtons();
        return;
    }
    const auto info = *info_;
    const auto paths = paths_;
    const auto fit = screenmedia::Fit(std::max(0, fit_->current()));
    const int fps = FPS_CHOICES[std::max(0, fps_->current())];
    const int still = SLIDE_MS[std::max(0, slide_->current())];
    const quint64 serial = ++prepareSerial_;
    summary_->setText(tr("PREPARING…"));
    using Prepared = std::pair<QList<screenmedia::Frame>, QByteArray>;
    auto *w = new QFutureWatcher<std::variant<Prepared, QString>>(this);
    connect(w, &QFutureWatcher<std::variant<Prepared, QString>>::finished, this, [this, w, serial, info] {
        w->deleteLater();
        if (serial != prepareSerial_)
            return;
        const auto res = w->result();
        if (const auto *err = std::get_if<QString>(&res)) {
            summary_->clear();
            status_->setText(tr("ERROR: %1").arg(*err));
            return;
        }
        std::tie(frames_, stream_) = std::get<Prepared>(res);
        for (const auto &f : frames_)
            previews_.append(QPixmap::fromImage(screenmedia::frameImage(f, info.width, info.height)));
        previewIdx_ = 0;
        previewTick();
        const qint64 size = screenmedia::storedSize(frames_, info.width, info.height);
        summary_->setText(tr("%1 FRAMES  ·  %2 TO SEND (%3 STORED)  ·  ~%4")
                              .arg(frames_.size()).arg(kib(stream_.size()), kib(size), duration(stream_.size() / UPLOAD_RATE)));
        updateButtons();
    });
    w->setFuture(QtConcurrent::run([paths, info, fit, fps, still]() -> std::variant<Prepared, QString> {
        try {
            auto frames = screenmedia::load(paths, info.width, info.height, fit, info.maxFrames, fps, still);
            if (frames.isEmpty())
                return QStringLiteral("nothing to show in this file");
            return Prepared{frames, screenmedia::encode(frames)};
        } catch (const std::exception &e) {
            return QString::fromUtf8(e.what());
        }
    }));
}

void AnimationPage::previewTick()
{
    if (previews_.isEmpty())
        return;
    preview_->setPixmap(previews_[previewIdx_]);
    const int delay = std::max(20, int(frames_.value(previewIdx_).delayMs));
    previewIdx_ = (previewIdx_ + 1) % int(previews_.size());
    if (previews_.size() > 1 && isVisible())
        previewTimer_.start(delay);
}

void AnimationPage::startJob(const QString &label, double rate)
{
    busy_ = true;
    rate_ = rate;
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    jobLabel_->setText(label);
    eta_->clear();
    status_->clear();
    bar_->setValue(0);
    for (QWidget *w : {static_cast<QWidget *>(jobLabel_), static_cast<QWidget *>(bar_), static_cast<QWidget *>(eta_),
                       static_cast<QWidget *>(cancelBtn_)})
        w->show();
    // megabytes through the keyboard: the rest of the app leaves it alone meanwhile
    ctl->setBusy(true);
    updateButtons();
}

void AnimationPage::progress(qint64 done, qint64 total)
{
    bar_->setValue(int(1000 * done / std::max<qint64>(1, total)));
    if (rate_ > 0)
        eta_->setText(tr("%1 / %2  ·  %3 LEFT").arg(kib(done), kib(total), duration((total - done) / rate_)));
}

void AnimationPage::endJob(const QString &status)
{
    busy_ = false;
    cancel_.reset();
    for (QWidget *w : {static_cast<QWidget *>(jobLabel_), static_cast<QWidget *>(bar_), static_cast<QWidget *>(eta_),
                       static_cast<QWidget *>(cancelBtn_)})
        w->hide();
    status_->setText(status);
    ctl->setBusy(false);
    refreshInfo();
    ctl->armedReload();
}

void AnimationPage::upload(const QList<screenmedia::Frame> &frames, const QByteArray &stream, const QString &label)
{
    if (!info_ || !ctl->session())
        return;
    const quint32 size = quint32(screenmedia::storedSize(frames, info_->width, info_->height));
    startJob(label, UPLOAD_RATE * size / std::max<qint64>(1, stream.size()));
    auto cancel = cancel_;
    QPointer<AnimationPage> self(this);
    ctl->session()->run(hid::Priority::Interactive, this, [size, stream, cancel, self](hid::Link &link) {
        screenUpload(link, size, stream, [self](qint64 d, qint64 t) {
            QMetaObject::invokeMethod(qApp, [self, d, t] {
                if (self)
                    self->progress(d, t);
            });
        }, [cancel] { return cancel->load(); });
        set(link, V_SCREEN, 1);
    }, [this](hid::Result<hid::Unit> r) {
        endJob(r.ok() ? tr("Uploaded. The keyboard now shows the animation.")
                      : r.error == QLatin1String("cancelled") ? tr("Cancelled.") : tr("ERROR: %1").arg(r.error));
    });
}

void AnimationPage::backup()
{
    if (!info_ || !info_->frames || !ctl->session())
        return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save backup"), QStringLiteral("screen-backup.bin"),
                                                      tr("Backup (*.bin)"));
    if (path.isEmpty())
        return;
    const auto info = *info_;
    startJob(tr("READING"), READ_RATE);
    auto cancel = cancel_;
    QPointer<AnimationPage> self(this);
    ctl->session()->run(hid::Priority::Interactive, this, [info, cancel, self](hid::Link &link) {
        auto cancelled = [cancel] { return cancel->load(); };
        QByteArray hdr = screenRead(link, 0, screenmedia::HEADER, {}, cancelled);
        QByteArray body = screenRead(link, screenmedia::HEADER, quint32(info.frames) * quint32(info.frameBytes()),
                                     [self](qint64 d, qint64 t) {
                                         QMetaObject::invokeMethod(qApp, [self, d, t] {
                                             if (self)
                                                 self->progress(d, t);
                                         });
                                     }, cancelled);
        return hdr + body;
    }, [this, path](hid::Result<QByteArray> r) {
        if (!r.ok()) {
            endJob(r.error == QLatin1String("cancelled") ? tr("Cancelled.") : tr("ERROR: %1").arg(r.error));
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(*r.value) < 0)
            endJob(tr("ERROR: %1").arg(f.errorString()));
        else
            endJob(tr("Saved to %1").arg(path));
    });
}

void AnimationPage::restore()
{
    if (!info_)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Restore backup"), {}, tr("Backup (*.bin)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        status_->setText(tr("ERROR: %1").arg(f.errorString()));
        return;
    }
    auto frames = screenmedia::fromBackup(f.readAll(), info_->width, info_->height, info_->maxFrames);
    if (frames.isEmpty()) {
        status_->setText(tr("Not a screen backup."));
        return;
    }
    upload(frames, screenmedia::encode(frames), tr("RESTORING"));
}

void AnimationPage::updateButtons()
{
    const auto cur = ctl->current();
    // megabytes at ~15 ms per 28-byte packet: USB only
    const bool bulk = !busy_ && !(cur && cur->isDongle());
    open_->setEnabled(!busy_);
    upload_->setEnabled(bulk && !frames_.isEmpty() && !stream_.isEmpty());
    backup_->setEnabled(bulk && info_ && info_->frames);
    restore_->setEnabled(bulk && info_.has_value());
    const QString tip = bulk || busy_ ? QString() : tr("Connect the keyboard by USB to transfer animations");
    for (QPushButton *b : {upload_, backup_, restore_})
        b->setToolTip(tip);
    for (Segmented *s : {fit_, fps_, slide_})
        s->setEnabled(!busy_);
}
