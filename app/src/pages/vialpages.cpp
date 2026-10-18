#include "pages/vialpages.h"

#include "app/firmwareflasher.h"
#include "core/bytes.h"
#include "model/qmksettings.h"
#include "ui/theme.h"
#include "ui/unlocker.h"
#include "ui/widgets/keyboardview.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

// ---- QMK settings

QmkSettingsPage::QmkSettingsPage(Controller *ctl)
    : Page(ctl)
    , tabs_(new QTabWidget)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->addWidget(tabs_, 1);
    auto *bottom = new QHBoxLayout;
    auto *hint = new QLabel(tr("Changes apply at once."));
    hint->setObjectName("armedHint");
    auto *reset = new QPushButton(tr("Reset to defaults"));
    connect(reset, &QPushButton::clicked, this, [this] {
        this->ctl->write(false, [](hid::Link &link) { vial::qmkSettingsReset(link); }, {}, [this] {
            this->ctl->toast(tr("QMK settings reset; reading them again"));
            this->ctl->reload();
        });
    });
    bottom->addWidget(hint);
    bottom->addStretch(1);
    bottom->addWidget(reset);
    l->addLayout(bottom);
}

void QmkSettingsPage::setValue(quint16 qsid, quint32 value)
{
    KeyboardState *kb = ctl->keyboard();
    if (!kb || kb->settings.value(qsid) == value)
        return;
    const quint32 old = kb->settings.value(qsid);
    kb->settings[qsid] = value;
    const QByteArray bytes = qmksettings::Catalog::instance().serialize(qsid, value);
    ctl->write(false, [qsid, bytes](hid::Link &link) { vial::qmkSettingSet(link, qsid, bytes); },
               [this, qsid, old] {
                   if (KeyboardState *k = ctl->keyboard())
                       k->settings[qsid] = old;
                   keyboardChanged();
               },
               {}, QStringLiteral("qs-%1").arg(qsid));
}

void QmkSettingsPage::keyboardChanged()
{
    while (tabs_->count()) {
        QWidget *w = tabs_->widget(0);
        tabs_->removeTab(0);
        w->deleteLater();
    }
    const KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    for (const auto &tab : qmksettings::Catalog::instance().tabs()) {
        auto *form = new QFormLayout;
        form->setSpacing(ui::theme::S2);
        int shown = 0;
        for (const auto &f : tab.fields) {
            if (!kb->settings.contains(f.qsid))
                continue;
            const quint32 v = kb->settings.value(f.qsid);
            if (f.type == qmksettings::Field::Type::Boolean) {
                auto *c = new QCheckBox(f.title);
                c->setChecked(v & (1u << f.bit));
                connect(c, &QCheckBox::toggled, this, [this, f](bool on) {
                    const quint32 cur = ctl->keyboard()->settings.value(f.qsid);
                    setValue(f.qsid, on ? (cur | (1u << f.bit)) : (cur & ~(1u << f.bit)));
                });
                form->addRow(c);
            } else {
                auto *s = new QSpinBox;
                s->setRange(f.min, f.max);
                s->setValue(int(v));
                connect(s, &QSpinBox::valueChanged, this, [this, f](int value) { setValue(f.qsid, quint32(value)); });
                form->addRow(f.title, s);
            }
            ++shown;
        }
        if (!shown) {
            delete form;
            continue;
        }
        auto *inner = new QWidget;
        inner->setLayout(form);
        auto *area = new QScrollArea;
        area->setWidget(inner);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        tabs_->addTab(area, tab.name);
    }
}

// ---- matrix tester

MatrixPage::MatrixPage(Controller *ctl)
    : Page(ctl)
    , view_(new KeyboardView)
    , lockHint_(new QLabel(tr("Unlock the keyboard before testing:")))
    , unlock_(new QPushButton(tr("Unlock")))
    , grabber_(new QWidget(this))
{
    view_->setSelectable(false);
    auto *l = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Press keys on the keyboard: each lights up while held, and stays marked once seen."));
    hint->setObjectName("armedHint");
    l->addWidget(hint);
    l->addStretch(1);
    l->addWidget(new FittedKeyboard(view_));
    l->addStretch(1);
    auto *bottom = new QHBoxLayout;
    bottom->addStretch(1);
    bottom->addWidget(lockHint_);
    bottom->addWidget(unlock_);
    auto *reset = new QPushButton(tr("Reset"));
    bottom->addWidget(reset);
    l->addLayout(bottom);
    connect(reset, &QPushButton::clicked, this, &MatrixPage::reset);
    connect(unlock_, &QPushButton::clicked, this, [this] {
        Unlocker::ensure(this->ctl, this, [](bool) {});
    });
    grabber_->hide();
    connect(&timer_, &QTimer::timeout, this, &MatrixPage::poll);
}

void MatrixPage::keyboardChanged()
{
    last_.clear();
    if (const KeyboardState *kb = ctl->keyboard())
        view_->setKeys(kb->def.keys, kb->def.encoders);
}

void MatrixPage::activate()
{
    // keys pressed for testing shouldn't type into the app
    grabber_->grabKeyboard();
    // every 2.4 GHz request costs ~20 ms of radio time, so the dongle is asked less often
    const auto cur = ctl->current();
    timer_.setInterval(cur && cur->isDongle() ? 50 : 20);
    timer_.start();
}

void MatrixPage::deactivate()
{
    grabber_->releaseKeyboard();
    timer_.stop();
}

void MatrixPage::reset()
{
    last_.clear();
    for (auto &k : view_->keys())
        k.pressed = k.on = false;
    view_->update();
}

void MatrixPage::poll()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !ctl->session() || inFlight_ || !isVisible() || window()->isMinimized())
        return;
    inFlight_ = true;
    const bool askLock = ticks_++ % (1000 / timer_.interval()) == 0;
    const int proto = int(kb->vialProtocol);
    ctl->session()->run(hid::Priority::Poll, this, [askLock, proto](hid::Link &link) {
        std::pair<std::optional<bool>, QByteArray> out;
        if (askLock) {
            out.first = vial::unlockStatus(link, proto, 3).unlocked;
            if (!*out.first)
                return out;
        }
        out.second = vial::matrixState(link);
        return out;
    }, [this](hid::Result<std::pair<std::optional<bool>, QByteArray>> r) {
        inFlight_ = false;
        if (!r.ok())
            return;
        auto [unlocked, data] = *r.value;
        if (unlocked) {
            lockHint_->setVisible(!*unlocked);
            unlock_->setVisible(!*unlocked);
        }
        if (data.isEmpty() || data == last_)
            return;
        last_ = data;
        const KeyboardState *kb = ctl->keyboard();
        if (!kb)
            return;
        const int rowSize = (kb->def.cols + 7) / 8;
        for (auto &k : view_->keys()) {
            if (k.encoder || k.def.row < 0 || k.def.row >= kb->def.rows || k.def.col >= kb->def.cols)
                continue;
            const int start = 2 + k.def.row * rowSize;
            const int byte = start + rowSize - 1 - k.def.col / 8;
            const bool down = byte < data.size() && (quint8(data[byte]) >> (k.def.col % 8)) & 1;
            k.pressed = down;
            k.on = k.on || down;
        }
        view_->update();
    }, QStringLiteral("matrix"));
}

// ---- firmware

FirmwarePage::FirmwarePage(Controller *ctl)
    : Page(ctl)
    , file_(new QLineEdit)
    , log_(new QPlainTextEdit)
    , progress_(new QProgressBar)
    , flash_(new QPushButton(tr("Flash")))
    , select_(new QPushButton(tr("Select file...")))
    , restore_(new QCheckBox(tr("Restore the current layout after flashing")))
{
    file_->setReadOnly(true);
    file_->setPlaceholderText(tr("A Vial firmware package (.vfw)"));
    log_->setReadOnly(true);
    progress_->setRange(0, 1000);
    restore_->setChecked(true);
    auto *row = new QHBoxLayout;
    row->addWidget(file_, 1);
    row->addWidget(select_);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(progress_, 1);
    bottom->addWidget(flash_);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->addLayout(row);
    l->addWidget(log_, 1);
    l->addWidget(restore_);
    l->addLayout(bottom);
    connect(select_, &QPushButton::clicked, this, &FirmwarePage::selectFile);
    connect(flash_, &QPushButton::clicked, this, &FirmwarePage::flash);

    auto *f = FirmwareFlasher::instance(ctl);
    for (const QString &line : f->logLines())
        log_->appendPlainText(line);
    progress_->setValue(int(f->progress() * 1000));
    connect(f, &FirmwareFlasher::logged, log_, &QPlainTextEdit::appendPlainText);
    connect(f, &FirmwareFlasher::progressed, this, [this](double p) { progress_->setValue(int(p * 1000)); });
    connect(f, &FirmwareFlasher::runningChanged, this, [this](bool running) {
        flash_->setEnabled(!running);
        select_->setEnabled(!running);
    });
    flash_->setEnabled(!f->running());
    select_->setEnabled(!f->running());
}

void FirmwarePage::keyboardChanged()
{
    const auto cur = ctl->current();
    restore_->setVisible(cur && cur->kind != hid::DeviceKind::Bootloader);
}

void FirmwarePage::selectFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select firmware"), {}, tr("Vial Firmware (*.vfw)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 10 * 1024 * 1024) {
        ctl->toast(tr("Not a firmware package: %1").arg(path), Controller::ToastKind::Error);
        return;
    }
    firmware_ = f.readAll();
    file_->setText(path);
    log_->appendPlainText(tr("Firmware update package: %1").arg(path));
}

void FirmwarePage::flash()
{
    if (firmware_.isEmpty()) {
        ctl->toast(tr("Select a firmware package first"), Controller::ToastKind::Error);
        return;
    }
    FirmwareFlasher::instance(ctl)->start(firmware_, restore_->isVisible() && restore_->isChecked());
}

// ---- VialRGB / QMK lighting

namespace {

const char *const kRgblightEffects[] = {
    "All Off", "Solid Color", "Breathing 1", "Breathing 2", "Breathing 3", "Breathing 4", "Rainbow Mood 1",
    "Rainbow Mood 2", "Rainbow Mood 3", "Rainbow Swirl 1", "Rainbow Swirl 2", "Rainbow Swirl 3", "Rainbow Swirl 4",
    "Rainbow Swirl 5", "Rainbow Swirl 6", "Snake 1", "Snake 2", "Snake 3", "Snake 4", "Snake 5", "Snake 6", "Knight 1",
    "Knight 2", "Knight 3", "Christmas", "Gradient 1", "Gradient 2", "Gradient 3", "Gradient 4", "Gradient 5",
    "Gradient 6", "Gradient 7", "Gradient 8", "Gradient 9", "Gradient 10", "RGB Test", "Alternating",
};
// effects without a colour to pick
const QSet<int> kNoColor = {0, 6, 7, 8, 9, 10, 11, 12, 13, 14};

const char *const kVialRgbEffects[] = {
    "Disable", "Direct Control", "Solid Color", "Alphas Mods", "Gradient Up Down", "Gradient Left Right", "Breathing",
    "Band Sat", "Band Val", "Band Pinwheel Sat", "Band Pinwheel Val", "Band Spiral Sat", "Band Spiral Val", "Cycle All",
    "Cycle Left Right", "Cycle Up Down", "Rainbow Moving Chevron", "Cycle Out In", "Cycle Out In Dual",
    "Cycle Pinwheel", "Cycle Spiral", "Dual Beacon", "Rainbow Beacon", "Rainbow Pinwheels", "Raindrops",
    "Jellybean Raindrops", "Hue Breathing", "Hue Pendulum", "Hue Wave", "Typing Heatmap", "Digital Rain",
    "Solid Reactive Simple", "Solid Reactive", "Solid Reactive Wide", "Solid Reactive Multiwide",
    "Solid Reactive Cross", "Solid Reactive Multicross", "Solid Reactive Nexus", "Solid Reactive Multinexus",
    "Splash", "Multisplash", "Solid Splash", "Solid Multisplash", "Pixel Rain", "Pixel Fractal",
};

QPushButton *swatch(const QColor &c)
{
    auto *b = new QPushButton;
    b->setFixedSize(60, 26);
    b->setStyleSheet(QStringLiteral("QPushButton { background: %1; }").arg(c.name()));
    b->setToolTip(QObject::tr("Pick a colour"));
    return b;
}

QSlider *slider(int max, int value)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(0, max);
    s->setValue(value);
    s->setMinimumWidth(260);
    return s;
}

}

RgbPage::RgbPage(Controller *ctl)
    : Page(ctl)
{
    auto *l = new QVBoxLayout(this);
    l->addStretch(1);
    auto *bottom = new QHBoxLayout;
    bottom->addStretch(1);
    auto *save = new QPushButton(tr("Save"));
    save->setToolTip(tr("Keep these settings after the keyboard restarts"));
    connect(save, &QPushButton::clicked, this, [this] {
        this->ctl->write(false, [](hid::Link &link) { vial::lightingSave(link); }, {},
                         [this] { this->ctl->toast(tr("Lighting saved"), Controller::ToastKind::Ok); });
    });
    bottom->addWidget(save);
    l->addLayout(bottom);
}

void RgbPage::keyboardChanged()
{
    delete body_;
    body_ = nullptr;
    KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    body_ = new QWidget;
    auto *form = new QFormLayout(body_);
    form->setSpacing(ui::theme::S3);
    auto set = [this](const QByteArray &args, const QString &key) {
        ctl->write(false, [args](hid::Link &link) { vial::lightingSet(link, args); }, {}, {}, key);
    };
    using namespace vial;
    const bool armedMatrix = kb->armed && kb->armed->has(armed::F_RGB_MATRIX);

    if (kb->lighting == "qmk_backlight" || kb->lighting == "qmk_backlight_rgblight") {
        auto *b = slider(255, kb->qmkLighting.value(QMK_BACKLIGHT_BRIGHTNESS));
        connect(b, &QSlider::valueChanged, this, [set, kb](int v) {
            kb->qmkLighting[QMK_BACKLIGHT_BRIGHTNESS] = v;
            set(bytes::of({QMK_BACKLIGHT_BRIGHTNESS, v}), "bl-b");
        });
        form->addRow(tr("Backlight brightness"), b);
        auto *breathing = new QCheckBox;
        breathing->setChecked(kb->qmkLighting.value(QMK_BACKLIGHT_EFFECT) == 1);
        connect(breathing, &QCheckBox::toggled, this, [set, kb](bool on) {
            kb->qmkLighting[QMK_BACKLIGHT_EFFECT] = on;
            set(bytes::of({QMK_BACKLIGHT_EFFECT, on ? 1 : 0}), "bl-e");
        });
        form->addRow(tr("Backlight breathing"), breathing);
    }
    if (kb->lighting == "qmk_rgblight" || kb->lighting == "qmk_backlight_rgblight") {
        auto *effect = new QComboBox;
        for (const char *e : kRgblightEffects)
            effect->addItem(tr(e));
        effect->setCurrentIndex(kb->qmkLighting.value(QMK_RGBLIGHT_EFFECT));
        auto current = [kb] {
            const int c = kb->qmkLighting.value(QMK_RGBLIGHT_COLOR);
            return QColor::fromHsvF((c & 0xFF) / 255.0, (c >> 8) / 255.0, kb->qmkLighting.value(QMK_RGBLIGHT_BRIGHTNESS) / 255.0);
        };
        auto *color = swatch(current());
        color->setVisible(!kNoColor.contains(effect->currentIndex()));
        connect(effect, &QComboBox::currentIndexChanged, this, [set, kb, color](int i) {
            kb->qmkLighting[QMK_RGBLIGHT_EFFECT] = i;
            color->setVisible(!kNoColor.contains(i));
            set(bytes::of({QMK_RGBLIGHT_EFFECT, i}), "ug-e");
        });
        form->addRow(tr("Underglow effect"), effect);
        auto *b = slider(255, kb->qmkLighting.value(QMK_RGBLIGHT_BRIGHTNESS));
        connect(b, &QSlider::valueChanged, this, [set, kb](int v) {
            kb->qmkLighting[QMK_RGBLIGHT_BRIGHTNESS] = v;
            set(bytes::of({QMK_RGBLIGHT_BRIGHTNESS, v}), "ug-b");
        });
        form->addRow(tr("Underglow brightness"), b);
        connect(color, &QPushButton::clicked, this, [this, set, kb, color, current] {
            QColor c = QColorDialog::getColor(current(), this, tr("Underglow colour"));
            if (!c.isValid())
                return;
            color->setStyleSheet(QStringLiteral("QPushButton { background: %1; }").arg(c.name()));
            const int h = std::max(0, int(255 * c.hsvHueF())), s = int(255 * c.hsvSaturationF());
            kb->qmkLighting[QMK_RGBLIGHT_COLOR] = h | s << 8;
            set(bytes::of({QMK_RGBLIGHT_COLOR, h, s}), "ug-c");
        });
        form->addRow(tr("Underglow colour"), color);
    }
    if (kb->lighting == "vialrgb" && !armedMatrix) {
        auto *effect = new QComboBox;
        for (int i = 0; i < int(std::size(kVialRgbEffects)); ++i)
            if (kb->vialRgbEffects.contains(quint16(i)))
                effect->addItem(tr(kVialRgbEffects[i]), i);
        effect->setCurrentIndex(effect->findData(int(kb->vialRgbMode.mode)));
        auto send = [this, kb] {
            const auto m = kb->vialRgbMode;
            ctl->write(false, [m](hid::Link &link) { setVialRgbMode(link, m); }, {}, {}, QStringLiteral("vialrgb"));
        };
        connect(effect, &QComboBox::currentIndexChanged, this, [kb, effect, send] {
            kb->vialRgbMode.mode = quint16(effect->currentData().toInt());
            send();
        });
        form->addRow(tr("RGB effect"), effect);
        auto *color = swatch(QColor::fromHsvF(kb->vialRgbMode.h / 255.0, kb->vialRgbMode.s / 255.0, 1.0));
        connect(color, &QPushButton::clicked, this, [this, kb, color, send] {
            QColor c = QColorDialog::getColor(QColor::fromHsvF(kb->vialRgbMode.h / 255.0, kb->vialRgbMode.s / 255.0, 1.0),
                                              this, tr("RGB colour"));
            if (!c.isValid())
                return;
            color->setStyleSheet(QStringLiteral("QPushButton { background: %1; }").arg(c.name()));
            kb->vialRgbMode.h = quint8(std::max(0, int(255 * c.hsvHueF())));
            kb->vialRgbMode.s = quint8(255 * c.hsvSaturationF());
            send();
        });
        form->addRow(tr("RGB colour"), color);
        auto *b = slider(std::max(1, kb->vialRgb.maxBrightness), kb->vialRgbMode.v);
        connect(b, &QSlider::valueChanged, this, [kb, send](int v) {
            kb->vialRgbMode.v = quint8(v);
            send();
        });
        form->addRow(tr("RGB brightness"), b);
        auto *speed = slider(255, kb->vialRgbMode.speed);
        connect(speed, &QSlider::valueChanged, this, [kb, send](int v) {
            kb->vialRgbMode.speed = quint8(v);
            send();
        });
        form->addRow(tr("RGB speed"), speed);
    }
    static_cast<QVBoxLayout *>(layout())->insertWidget(0, body_, 0, Qt::AlignHCenter);
}
