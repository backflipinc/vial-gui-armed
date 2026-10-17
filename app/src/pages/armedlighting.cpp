#include "pages/armedlighting.h"

#include "ui/theme.h"
#include "ui/widgets/armedcontrols.h"
#include "ui/widgets/keyboardview.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

using namespace armed;
using namespace ui::theme;

namespace {

constexpr int FPS = 30;
constexpr int DEMO_PRESS_MS = 420;
enum Role { IdRole = Qt::UserRole, Color2Role, ThemeBaseRole };

class EffectDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *qp, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        qp->save();
        const bool selected = option.state & QStyle::State_Selected;
        const QRect r = option.rect.adjusted(6, 1, -10, -1);
        if (selected) {
            qp->setPen(ACC);
            qp->drawRect(r.adjusted(0, 0, -1, -1));
            qp->fillRect(QRect(r.left(), r.top(), 4, r.height()), ACC);
        } else if (option.state & (QStyle::State_MouseOver | QStyle::State_HasFocus)) {
            qp->setPen(LINE2);
            qp->drawRect(r.adjusted(0, 0, -1, -1));
        }
        qp->setFont(mono(12));
        qp->setPen(selected ? ACC : DIM);
        qp->drawText(r.adjusted(14, 0, 0, 0), Qt::AlignVCenter, QStringLiteral("%1").arg(index.row(), 2, 10, QLatin1Char('0')));
        qp->setFont(head(15));
        qp->setPen(selected ? ACC : TXT);
        const QString text = qp->fontMetrics().elidedText(index.data().toString().toUpper(), Qt::ElideRight, r.width() - 84);
        qp->drawText(r.adjusted(44, 0, -40, 0), Qt::AlignVCenter, text);
        if (index.data(Color2Role).toBool()) {
            const QRect badge(r.right() - 34, r.center().y() - 8, 26, 16);
            qp->setFont(mono(11));
            qp->setPen(selected ? ACC : ACC_D);
            qp->drawRect(badge);
            qp->setPen(ACC);
            qp->drawText(badge, Qt::AlignCenter, QStringLiteral("2C"));
        }
        qp->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {260, 32}; }
};

}

ArmedLightingPage::ArmedLightingPage(Controller *ctl)
    : Page(ctl)
    , effects_(new QListWidget)
    , enabled_(new QPushButton(QStringLiteral("ON")))
    , hint_(new QLabel)
    , colorFrame_(new BracketFrame(tr("COLOR"), true))
    , color2Frame_(new BracketFrame(tr("COLOR 2")))
    , levelFrame_(new BracketFrame)
    , fnFrame_(new BracketFrame(tr("FN COLOR")))
    , stripFrame_(new BracketFrame(tr("LIGHT BAR")))
    , chip_(new QLabel)
    , color2Name_(new QLabel)
    , fnName_(new QLabel)
    , hue_(armedSlider(255))
    , sat_(armedSlider(255))
    , val_(armedSlider(255))
    , speed_(armedSlider(255))
    , color2_(new Swatches)
    , fnColor_(new Swatches)
    , stripColor_(new Swatches)
    , holdFn_(new QPushButton(tr("HOLD FN")))
    , stripMode_(new Segmented)
    , stripCaps_(new QCheckBox(tr("CAPS ALERT")))
    , stripView_(new StripView)
    , kb_(new KeyboardView)
{
    clock_.start();
    auto *top = new QHBoxLayout;
    top->addStretch(1);
    hint_->setObjectName("armedHint");
    top->addWidget(hint_);
    enabled_->setCheckable(true);
    enabled_->setToolTip(tr("Turn the keyboard's lighting on or off"));
    top->addWidget(enabled_);

    effects_->setItemDelegate(new EffectDelegate(effects_));
    effects_->setMouseTracking(true);
    effects_->setFixedWidth(300);
    effects_->setAccessibleName(tr("Effects"));

    chip_->setFixedSize(22, 16);
    colorFrame_->header()->insertWidget(1, chip_);
    hue_->setStyleSheet(QStringLiteral("QSlider::groove:horizontal { border: 0; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
                                       "stop:0 #f00, stop:0.17 #ff0, stop:0.33 #0f0, stop:0.5 #0ff, stop:0.67 #00f, stop:0.83 #f0f, stop:1 #f00); }"
                                       "QSlider::sub-page:horizontal { background: transparent; border: 0; }"));
    colorFrame_->box()->addLayout(sliderRow(tr("HUE"), hue_));
    colorFrame_->box()->addLayout(sliderRow(tr("SAT"), sat_));
    color2Name_->setObjectName("armedHint");
    color2Frame_->header()->insertWidget(1, color2Name_);
    color2Frame_->box()->addWidget(color2_);
    color2Frame_->box()->addStretch(1);
    levelFrame_->box()->addLayout(sliderRow(tr("BRIGHT"), val_, true));
    levelFrame_->box()->addLayout(sliderRow(tr("SPEED"), speed_, true));
    fnName_->setObjectName("armedHint");
    fnFrame_->header()->insertWidget(1, fnName_);
    holdFn_->setCheckable(true);
    holdFn_->setToolTip(tr("Preview what the keyboard shows while Fn is held"));
    fnFrame_->header()->addWidget(holdFn_);
    fnFrame_->box()->addWidget(fnColor_);
    fnFrame_->box()->addStretch(1);
    stripFrame_->header()->insertWidget(1, stripView_, 1);
    auto *stripRow = new QHBoxLayout;
    stripRow->setSpacing(18);
    stripRow->addWidget(stripMode_);
    stripRow->addWidget(stripColor_);
    stripRow->addStretch(1);
    stripRow->addWidget(stripCaps_);
    stripFrame_->box()->addLayout(stripRow);

    auto *kbFrame = new BracketFrame;
    kbFrame->box()->setContentsMargins(16, 16, 16, 16);
    kb_->setSelectable(false);
    kb_->setCursor(Qt::PointingHandCursor);
    auto *fitted = new FittedKeyboard(kb_);
    fitted->setPreferredScale(1.1);
    kbFrame->box()->addWidget(fitted, 1);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(14);
    grid->addWidget(colorFrame_, 0, 0);
    grid->addWidget(color2Frame_, 0, 1);
    grid->addWidget(levelFrame_, 1, 0);
    grid->addWidget(fnFrame_, 1, 1);
    grid->addWidget(stripFrame_, 2, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    auto *right = new QVBoxLayout;
    right->setSpacing(14);
    right->addLayout(grid);
    right->addWidget(kbFrame, 1);
    auto *body = new QHBoxLayout;
    body->setSpacing(18);
    body->addWidget(effects_);
    body->addLayout(right, 1);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addLayout(top);
    l->addLayout(body, 1);

    connect(effects_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (updating_ || row < 0)
            return;
        set(V_EFFECT, effects_->item(row)->data(IdRole).toInt());
        if (!this->ctl->armedValue(V_ENABLED))
            set(V_ENABLED, 1);
        sync();
    });
    connect(enabled_, &QPushButton::clicked, this, [this](bool on) {
        set(V_ENABLED, on);
        sync();
    });
    for (auto [s, vid] : {std::pair{hue_, V_HUE}, std::pair{sat_, V_SAT}, std::pair{val_, V_VAL}, std::pair{speed_, V_SPEED}}) {
        connect(s, &QSlider::valueChanged, this, [this, v = vid](int x) {
            if (!updating_) {
                set(v, x);
                sync();
            }
        });
        // HSV and speed stay in RAM on the keyboard until saved
        connect(s, &QSlider::sliderReleased, this, [this] { this->ctl->armedSave(); });
    }
    for (auto [sw, vid] : {std::pair{color2_, V_COLOR2}, std::pair{fnColor_, V_FN_COLOR}, std::pair{stripColor_, V_STRIP_COLOR}})
        connect(sw, &Swatches::changed, this, [this, v = vid](int i) {
            set(v, i);
            sync();
        });
    connect(stripMode_, &Segmented::changed, this, [this](int i) {
        set(V_STRIP_MODE, i);
        sync();
    });
    connect(stripCaps_, &QCheckBox::toggled, this, [this](bool on) {
        if (!updating_)
            set(V_STRIP_CAPS, on);
    });
    connect(kb_, &KeyboardView::keyPressed, this, [this](int key) {
        for (int i = 0; i < ledKeys_.size(); ++i) {
            if (ledKeys_[i] == key) {
                preview_->hit(i, now());
                themePreview_->hit(i, now());
                return;
            }
        }
    });
    connect(ctl, &Controller::armedValuesChanged, this, &ArmedLightingPage::load);
    timer_.setInterval(1000 / FPS);
    connect(&timer_, &QTimer::timeout, this, &ArmedLightingPage::tick);
}

void ArmedLightingPage::set(quint8 vid, int value)
{
    ctl->armedSet(vid, value);
}

void ArmedLightingPage::keyboardChanged()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed)
        return;
    const Info &io = *kb->armed;
    preview_ = std::make_unique<lighting::EffectPreview>(io.leds);
    themePreview_ = std::make_unique<lighting::ThemeRenderer>(io.leds);
    kb_->setKeys(kb->def.keys, kb->def.encoders);
    ledKeys_.clear();
    for (const Led &led : io.leds) {
        int found = -1;
        for (int k = 0; k < kb_->keys().size(); ++k)
            if (!kb_->keys()[k].encoder && kb_->keys()[k].def.row == led.row && kb_->keys()[k].def.col == led.col)
                found = k;
        ledKeys_.append(found);
    }
    for (auto &k : kb_->keys()) {
        k.text.clear();
        k.glow = QColor(0, 0, 0);
    }
    fnKeys_.clear();
    for (const auto &[row, col] : kb->def.matrix) {
        const QString fn = kb->key(1, row, col);
        if ((!fn.isNull() && fn != "KC_TRNS" && fn != "KC_NO") || kb->key(0, row, col) == "MO(1)")
            fnKeys_.insert({row, col});
    }
    const bool has2 = io.has(F_COLOR2), hasFn = io.has(F_FN_COLOR);
    color2Frame_->setVisible(has2);
    fnFrame_->setVisible(hasFn);
    stripFrame_->setVisible(io.has(F_STRIP));
    hint_->setText(has2 ? tr("2C = uses COLOR 2 · click the keyboard to press keys")
                        : tr("click the keyboard to press keys"));
    val_->setMaximum(io.maxBrightness);
    updating_ = true;
    effects_->clear();
    for (const Effect &e : io.effects) {
        if (e.name.trimmed().toUpper().replace(' ', '_') == "VIALRGB_DIRECT")
            continue;
        auto *item = new QListWidgetItem(e.name);
        item->setData(IdRole, e.qmkId);
        item->setData(Color2Role, e.usesColor2());
        item->setData(ThemeBaseRole, e.theme() ? e.name : QString());
        effects_->addItem(item);
    }
    for (Swatches *sw : {color2_, fnColor_, stripColor_})
        sw->setColors(io.colors);
    stripMode_->setLabels(io.stripModes);
    updating_ = false;
    load();
}

void ArmedLightingPage::load()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed)
        return;
    updating_ = true;
    const int effect = ctl->armedValue(V_EFFECT);
    for (int row = 0; row < effects_->count(); ++row) {
        if (effects_->item(row)->data(IdRole).toInt() == effect) {
            effects_->setCurrentRow(row);
            effects_->scrollToItem(effects_->item(row), QAbstractItemView::PositionAtCenter);
        }
    }
    hue_->setValue(ctl->armedValue(V_HUE));
    sat_->setValue(ctl->armedValue(V_SAT));
    val_->setValue(ctl->armedValue(V_VAL));
    speed_->setValue(ctl->armedValue(V_SPEED));
    color2_->setCurrent(ctl->armedValue(V_COLOR2));
    fnColor_->setCurrent(ctl->armedValue(V_FN_COLOR));
    stripColor_->setCurrent(ctl->armedValue(V_STRIP_COLOR));
    stripMode_->setCurrent(ctl->armedValue(V_STRIP_MODE));
    stripCaps_->setChecked(ctl->armedValue(V_STRIP_CAPS));
    enabled_->setChecked(ctl->armedValue(V_ENABLED));
    updating_ = false;
    sync();
}

std::optional<int> ArmedLightingPage::themeSlot() const
{
    const auto &themes = ctl->themes();
    const auto *item = effects_->currentItem();
    if (!themes || !item)
        return std::nullopt;
    const int slot = int(themes->info.modes.indexOf(item->data(IdRole).toInt()));
    if (slot < 0 || slot >= themes->list.size())
        return std::nullopt;
    return slot;
}

void ArmedLightingPage::sync()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed || !preview_)
        return;
    const Info &io = *kb->armed;
    auto &p = *preview_;
    const auto *item = effects_->currentItem();
    p.setEffect(item ? item->text().section(QStringLiteral(" · "), 0, 0) : QStringLiteral("SOLID_COLOR"));
    p.h = ctl->armedValue(V_HUE);
    p.s = ctl->armedValue(V_SAT);
    p.v = std::min(255, ctl->armedValue(V_VAL) * 255 / std::max(1, io.maxBrightness));
    p.speed = ctl->armedValue(V_SPEED);
    p.color1Black = p.h == 0 && p.s == 1;
    const auto c2 = io.colors.value(ctl->armedValue(V_COLOR2));
    p.color2 = {c2.h, c2.s};
    p.color2Black = c2.black();
    const auto sc = io.colors.value(ctl->armedValue(V_STRIP_COLOR));
    strip_.color = {sc.h, sc.s};
    strip_.colorBlack = sc.black();
    strip_.mode = io.stripModes.value(ctl->armedValue(V_STRIP_MODE), QStringLiteral("OFF"));

    const QColor chip = p.color1Black ? QColor(Qt::black) : QColor::fromHsv(p.h * 359 / 255, p.s, 255);
    chip_->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2;").arg(chip.name(), LINE2.name()));
    color2Name_->setText(io.colors.value(ctl->armedValue(V_COLOR2)).name);
    fnName_->setText(io.colors.value(ctl->armedValue(V_FN_COLOR)).name);
    enabled_->setText(ctl->armedValue(V_ENABLED) ? QStringLiteral("ON") : QStringLiteral("OFF"));

    const bool themed = themeSlot().has_value();
    colorFrame_->setVisible(!themed);
    color2Frame_->setVisible(io.has(F_COLOR2) && !themed);
    const bool uses2 = item && item->data(Color2Role).toBool();
    color2Frame_->setAccent(uses2);
    color2Name_->setStyleSheet(QStringLiteral("color: %1;").arg((uses2 ? TXT : DIM).name()));
}

void ArmedLightingPage::labelThemes()
{
    const auto &themes = ctl->themes();
    if (!themes)
        return;
    for (int row = 0; row < effects_->count(); ++row) {
        auto *item = effects_->item(row);
        const QString base = item->data(ThemeBaseRole).toString();
        const int slot = int(themes->info.modes.indexOf(item->data(IdRole).toInt()));
        if (!base.isEmpty() && slot >= 0 && slot < themes->list.size()) {
            const QString name = themes->list[slot].name;
            item->setText(name.isEmpty() ? base : QStringLiteral("%1 · %2").arg(base, name));
        }
    }
    sync();
}

void ArmedLightingPage::activate()
{
    ctl->armedReload();
    ctl->loadThemes([this](bool) { labelThemes(); });
    timer_.start();
}

void ArmedLightingPage::deactivate()
{
    timer_.stop();
}

void ArmedLightingPage::tick()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed || !preview_ || !isVisible())
        return;
    const Info &io = *kb->armed;
    const qint64 t = now();
    auto slot = themeSlot();
    const theme::Theme *th = slot ? &ctl->themes()->list[*slot] : nullptr;
    bool reactive = preview_->reactive();
    if (th) {
        reactive = false;
        for (const auto &l : th->layers)
            reactive = reactive || l.type == theme::REACTIVE || l.type == theme::RIPPLE;
    }
    if (reactive && t - lastDemo_ > DEMO_PRESS_MS && !io.leds.isEmpty()) {
        lastDemo_ = t;
        const int i = int(QRandomGenerator::global()->bounded(io.leds.size()));
        preview_->hit(i, t);
        themePreview_->hit(i, t);
    }
    QList<lighting::Rgb> colors;
    if (!ctl->armedValue(V_ENABLED)) {
        colors = QList<lighting::Rgb>(io.leds.size(), {0, 0, 0});
    } else if (th) {
        themePreview_->layersOn = holdFn_->isChecked() ? QSet<int>{1} : QSet<int>{};
        colors = themePreview_->frame(*th, t, preview_->v);
    } else {
        colors = preview_->frame(t);
    }
    bool themeFn = false;
    if (th)
        for (const auto &l : th->layers)
            themeFn = themeFn || (l.type && !l.hidden() && (l.cond & (theme::C_LAYER | theme::C_NOT_LAYER)));
    if (holdFn_->isChecked() && !themeFn) {
        const int fi = ctl->armedValue(V_FN_COLOR);
        const auto fc = io.colors.value(fi);
        lighting::Rgb on{0, 0, 0};
        if (fi < io.colors.size() && !fc.black()) {
            const QColor c = QColor::fromHsv(fc.h * 359 / 255, fc.s, std::max(preview_->v, 160));
            on = {quint8(c.red()), quint8(c.green()), quint8(c.blue())};
        }
        for (int i = 0; i < io.leds.size(); ++i)
            colors[i] = fnKeys_.contains({io.leds[i].row, io.leds[i].col}) ? on : lighting::Rgb{0, 0, 0};
    }
    auto &keys = kb_->keys();
    for (int i = 0; i < ledKeys_.size() && i < colors.size(); ++i)
        if (ledKeys_[i] >= 0)
            keys[ledKeys_[i]].glow = QColor(colors[i][0], colors[i][1], colors[i][2]);
    kb_->update();
    if (io.has(F_STRIP)) {
        strip_.caps = false;
        stripView_->setColors(strip_.frame(t));
    }
}
