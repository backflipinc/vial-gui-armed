#include "pages/armedthemes.h"

#include "ui/theme.h"
#include "ui/widgets/armedcontrols.h"
#include "ui/widgets/keyboardview.h"
#include "ui/widgets/themecontrols.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QVBoxLayout>

using namespace theme;
using namespace ui::theme;

namespace {

constexpr int FPS = 30;
constexpr int DEMO_PRESS_MS = 420;
constexpr int UNDO_GROUP_MS = 600;
constexpr int PUSH_MS = 30;
constexpr int UNDO_DEPTH = 100;

QString seconds(double ms)
{
    return ms < 10000 ? QStringLiteral("%1 S").arg(ms / 1000, 0, 'f', 1) : QStringLiteral("%1 S").arg(int(ms / 1000));
}

QString pct(int v, int full = 255)
{
    return QStringLiteral("%1%").arg(int(std::lround(v * 100.0 / full)));
}

QString sizeLabel(int t)
{
    switch (t) {
    case GRADIENT:
    case RAINBOW: return QStringLiteral("LENGTH");
    case PULSE: return QStringLiteral("SPREAD");
    case WAVE:
    case RIPPLE: return QStringLiteral("WIDTH");
    case SPARKLE:
    case REACTIVE: return QStringLiteral("FADE");
    default: return QStringLiteral("SIZE");
    }
}

QSlider *slider(int max = 255)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(0, max);
    s->setMinimumWidth(120);
    return s;
}

QLabel *keyLabel(const QString &t = {})
{
    auto *l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("color: %1;").arg(MID.name()));
    l->setFixedWidth(96);
    return l;
}

}

// The keyboard of the theme editor: lit by the preview, marks the keys the layer covers, paints by dragging
class ThemeKeyboard : public KeyboardView {
public:
    explicit ThemeKeyboard(ThemeEditorPage *page)
        : page_(page)
    {
        setSelectable(false);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        const int key = hit(e->position()).first;
        dragged_ = {key};
        if (key >= 0)
            page_->keyDown(key, e->button(), e->modifiers());
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton)))
            return;
        const int key = hit(e->position()).first;
        if (key >= 0 && !dragged_.contains(key)) {
            dragged_.insert(key);
            page_->keyDrag(key);
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        dragged_.clear();
        page_->keyUp();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override {}
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ToolTip)
            return true;
        return KeyboardView::event(e);
    }
    void paintEvent(QPaintEvent *e) override
    {
        KeyboardView::paintEvent(e);
        QPainter qp(this);
        qp.setRenderHint(QPainter::Antialiasing);
        qp.setPen(Qt::NoPen);
        qp.setBrush(ACC);
        const QSet<int> covered = page_->coveredKeys();
        for (int i = 0; i < keys().size(); ++i) {
            if (!covered.contains(i))
                continue;
            const Key &k = keys()[i];
            qp.save();
            transformTo(qp, k);
            const QRectF r = k.rect;
            const double n = std::min(r.width(), r.height()) * 0.22;
            qp.drawPolygon(QPolygonF{r.topLeft() + QPointF(2, 2), r.topLeft() + QPointF(2 + n, 2), r.topLeft() + QPointF(2, 2 + n)});
            qp.restore();
        }
        if (auto c = page_->centerPoint()) {
            qp.setPen(QPen(ACC, 2));
            qp.setBrush(Qt::NoBrush);
            qp.drawEllipse(*c, 9, 9);
            qp.drawLine(*c - QPointF(15, 0), *c + QPointF(15, 0));
            qp.drawLine(*c - QPointF(0, 15), *c + QPointF(0, 15));
        }
    }

private:
    ThemeEditorPage *page_;
    QSet<int> dragged_;
};

ThemeEditorPage::ThemeEditorPage(Controller *ctl)
    : Page(ctl)
    , slots_(new Segmented)
    , name_(new QLineEdit)
    , state_(new QLabel)
    , keysHint_(new QLabel)
    , tools_(keyLabel(tr("KEYS")))
    , count_(new QLabel)
    , hint_(new QLabel)
    , empty_(new QLabel(tr("No layers yet. Press + ADD, or START FROM a preset.")))
    , undoBtn_(new QPushButton(tr("UNDO")))
    , redoBtn_(new QPushButton(tr("REDO")))
    , saveBtn_(new QPushButton(tr("SAVE")))
    , revertBtn_(new QPushButton(tr("REVERT")))
    , addBtn_(new QPushButton(tr("+ ADD ▾")))
    , dupBtn_(new QPushButton(tr("COPY")))
    , upBtn_(new QPushButton(QStringLiteral("▲")))
    , downBtn_(new QPushButton(QStringLiteral("▼")))
    , delBtn_(new QPushButton(tr("DELETE")))
    , swapBtn_(new QPushButton(QStringLiteral("⇄")))
    , centerBtn_(new QPushButton(tr("PICK THE POINT")))
    , solo_(new QCheckBox(tr("SOLO LAYER")))
    , caps_(new QCheckBox(tr("CAPS")))
    , previewLayer_(new QComboBox)
    , keysMode_(new Segmented)
    , radial_(new QCheckBox(tr("FROM A POINT")))
    , mirror_(new QCheckBox)
    , kb_(new ThemeKeyboard(this))
    , layers_(new LayerList)
    , props_(new BracketFrame(tr("LAYER")))
    , type_(new QComboBox)
    , cond_(new QComboBox)
    , colorA_(new ColorButton(QStringLiteral("A")))
    , colorB_(new ColorButton(QStringLiteral("B")))
    , colorsLabel_(keyLabel(tr("COLOURS")))
    , dial_(new DirectionDial)
    , radialWay_(new Segmented({"OUTWARD", "INWARD"}))
    , blend_(new Segmented(kBlends))
    , speed_(slider())
    , size_(slider())
    , param_(slider())
    , opacity_(slider())
    , speedLabel_(keyLabel())
    , sizeLabel_(keyLabel())
    , paramLabel_(keyLabel())
    , speedVal_(new QLabel)
    , sizeVal_(new QLabel)
    , paramVal_(new QLabel)
    , opacityVal_(new QLabel)
{
    clock_.start();
    // top bar
    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    top->addWidget(slots_);
    name_->setMaxLength(NAME_LEN - 1);
    name_->setFixedWidth(200);
    name_->setPlaceholderText(tr("NAME"));
    top->addWidget(name_);
    state_->setObjectName("armedHint");
    state_->setTextFormat(Qt::RichText);
    top->addWidget(state_);
    top->addStretch(1);
    auto *presets = new QPushButton(tr("START FROM ▾"));
    auto *presetsMenu = new QMenu(presets);
    const QStringList names = presetNames();
    for (int i = 0; i < names.size(); ++i)
        presetsMenu->addAction(names[i], this, [this, i] { onPreset(i); });
    presets->setMenu(presetsMenu);
    auto *importBtn = new QPushButton(tr("IMPORT"));
    auto *exportBtn = new QPushButton(tr("EXPORT"));
    saveBtn_->setDefault(true);
    undoBtn_->setToolTip(tr("Ctrl+Z"));
    redoBtn_->setToolTip(tr("Ctrl+Shift+Z / Ctrl+Y"));
    saveBtn_->setToolTip(tr("Keep the theme after the keyboard is unplugged (Ctrl+S)"));
    for (QPushButton *b : {undoBtn_, redoBtn_, presets, importBtn, exportBtn, revertBtn_, saveBtn_})
        top->addWidget(b);

    // keyboard
    auto *kbFrame = new BracketFrame;
    auto *kbHead = new QHBoxLayout;
    keysHint_->setObjectName("armedHint");
    kbHead->addWidget(keysHint_, 1);
    solo_->setToolTip(tr("Preview only the selected layer"));
    caps_->setToolTip(tr("Preview with Caps Lock on"));
    previewLayer_->setToolTip(tr("Preview with this keymap layer on"));
    for (QCheckBox *c : {solo_, caps_})
        kbHead->addWidget(c);
    kbHead->addWidget(previewLayer_);
    kbFrame->box()->addLayout(kbHead);
    auto *fitted = new FittedKeyboard(kb_);
    fitted->setPreferredScale(1.25);
    kbFrame->box()->addWidget(fitted, 1);
    auto *keysRow = new QHBoxLayout;
    keysRow->setSpacing(6);
    auto *keysLabel = keyLabel(tr("KEYMAP LAYER"));
    keysLabel->setFixedWidth(110);
    keysRow->addWidget(keysLabel);
    keysRow->addWidget(keysMode_);
    keysRow->addStretch(1);
    kbFrame->box()->addLayout(keysRow);
    rowKeys_ = Row{{keysLabel, keysMode_}};
    auto *tools = new QHBoxLayout;
    tools->setSpacing(6);
    tools_->setFixedWidth(110);
    tools->addWidget(tools_);
    for (const QString &g : kGroups.mid(1) + QStringList{"NONE", "INVERT"}) {
        auto *b = new QPushButton(g);
        connect(b, &QPushButton::clicked, this, [this, g] { onGroup(g); });
        tools->addWidget(b);
        groupButtons_.append(b);
    }
    tools->addStretch(1);
    kbFrame->box()->addLayout(tools);

    // layers
    auto *layersFrame = new BracketFrame(tr("LAYERS"), true);
    count_->setObjectName("armedHint");
    layersFrame->header()->insertWidget(1, count_);
    layers_->setMinimumHeight(150);
    layersFrame->box()->addWidget(layers_, 1);
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    auto *addMenu = new QMenu(addBtn_);
    for (const auto &t : types())
        addMenu->addAction(QStringLiteral("%1   %2").arg(t.name, -10).arg(t.hint), this, [this, id = t.id] { onAdd(id); });
    addBtn_->setMenu(addMenu);
    for (QPushButton *b : {addBtn_, dupBtn_, upBtn_, downBtn_, delBtn_})
        row->addWidget(b);
    layersFrame->box()->addLayout(row);

    // layer properties
    hint_->setObjectName("armedHint");
    hint_->setWordWrap(true);
    props_->box()->addWidget(hint_);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    for (const auto &t : types())
        type_->addItem(t.name, t.id);
    keysMode_->setToolTip(tr("The keys this layer lights. An FN layer's keys are what is bound on that keymap layer "
                             "plus the keys that bring it up; the keyboard follows remaps by itself"));
    rowType_ = addRow(grid, 0, keyLabel(tr("TYPE")), type_);
    auto *colors = new QHBoxLayout;
    swapBtn_->setToolTip(tr("Swap the colours"));
    colors->addWidget(colorA_);
    colors->addWidget(colorB_);
    colors->addWidget(swapBtn_);
    colors->addStretch(1);
    rowColors_ = addRow(grid, 1, colorsLabel_, colors);
    auto *dirBox = new QHBoxLayout;
    dirBox->addWidget(dial_);
    auto *dirOpts = new QVBoxLayout;
    dirOpts->setSpacing(6);
    radial_->setToolTip(tr("Spread out from a point instead of across the keyboard"));
    centerBtn_->setCheckable(true);
    for (QWidget *w : {static_cast<QWidget *>(radial_), static_cast<QWidget *>(radialWay_),
                       static_cast<QWidget *>(centerBtn_), static_cast<QWidget *>(mirror_)})
        dirOpts->addWidget(w);
    dirOpts->addStretch(1);
    dirBox->addLayout(dirOpts);
    dirBox->addStretch(1);
    rowDir_ = addRow(grid, 2, keyLabel(tr("DIRECTION")), dirBox);
    rowSpeed_ = addRow(grid, 3, speedLabel_, speed_, speedVal_);
    rowSize_ = addRow(grid, 4, sizeLabel_, size_, sizeVal_);
    rowParam_ = addRow(grid, 5, paramLabel_, param_, paramVal_);
    rowOpacity_ = addRow(grid, 6, keyLabel(tr("OPACITY")), opacity_, opacityVal_);
    blend_->setToolTip(tr("NORMAL covers the layers below, ADD brightens them, MULTIPLY tints them"));
    rowBlend_ = addRow(grid, 7, keyLabel(tr("MIX")), blend_);
    mixHint_ = new QLabel;
    mixHint_->setObjectName("armedHint");
    mixHint_->setWordWrap(true);
    mixHint_->hide();
    grid->addWidget(mixHint_, 9, 1, 1, 2);
    rowCond_ = addRow(grid, 8, keyLabel(tr("SHOW")), cond_);
    grid->setColumnStretch(1, 1);
    props_->box()->addLayout(grid);
    props_->box()->addStretch(1);
    empty_->setObjectName("armedHint");
    empty_->setAlignment(Qt::AlignCenter);
    props_->box()->addWidget(empty_);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(props_);

    auto *rightW = new QWidget;
    auto *right = new QVBoxLayout(rightW);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(14);
    right->addWidget(layersFrame, 2);
    right->addWidget(scroll, 3);
    rightW->setFixedWidth(470);
    auto *body = new QHBoxLayout;
    body->setSpacing(18);
    body->addWidget(kbFrame, 1);
    body->addWidget(rightW);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->addLayout(top);
    l->addLayout(body, 1);

    // events
    connect(slots_, &Segmented::changed, this, &ThemeEditorPage::onSlot);
    connect(name_, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (!loaded())
            return;
        QString n;
        for (QChar c : text.toUpper())
            n += c.unicode() >= 32 && c.unicode() < 127 ? c : QChar('?');
        theme().name = n;
        slots_->setLabels(slotLabels());
        slots_->setCurrent(slot_);
        changed();
    });
    connect(undoBtn_, &QPushButton::clicked, this, &ThemeEditorPage::onUndo);
    connect(redoBtn_, &QPushButton::clicked, this, &ThemeEditorPage::onRedo);
    connect(importBtn, &QPushButton::clicked, this, &ThemeEditorPage::onImport);
    connect(exportBtn, &QPushButton::clicked, this, &ThemeEditorPage::onExport);
    connect(revertBtn_, &QPushButton::clicked, this, &ThemeEditorPage::onRevert);
    connect(saveBtn_, &QPushButton::clicked, this, &ThemeEditorPage::onSave);
    connect(dupBtn_, &QPushButton::clicked, this, &ThemeEditorPage::onDuplicate);
    connect(upBtn_, &QPushButton::clicked, this, [this] { onMove(1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { onMove(-1); });
    connect(delBtn_, &QPushButton::clicked, this, &ThemeEditorPage::onDelete);
    connect(layers_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (updating_ || row < 0 || !loaded())
            return;
        cur_ = int(theme().layers.size()) - 1 - row;
        centerBtn_->setChecked(false);
        refreshControls();
        refreshKeys();
    });
    connect(layers_, &LayerList::toggled, this, [this](int row) {
        Layer &l = theme().layers[theme().layers.size() - 1 - row];
        l.setFlag(F_HIDDEN, !l.hidden());
        changed(true);
    });
    connect(type_, &QComboBox::currentIndexChanged, this, &ThemeEditorPage::onType);
    connect(colorA_, &ColorButton::changed, this, [this](const Rgb &c) { edit([c](Layer &l) { l.a = c; }); });
    connect(colorB_, &ColorButton::changed, this, [this](const Rgb &c) { edit([c](Layer &l) { l.b = c; }); });
    connect(swapBtn_, &QPushButton::clicked, this, [this] {
        if (Layer *l = layer()) {
            std::swap(l->a, l->b);
            changed(false, true);
        }
    });
    connect(dial_, &DirectionDial::changed, this, [this](int v) {
        if (Layer *l = layer()) {
            l->dir = v;
            l->setFlag(F_RADIAL, false);
            l->setFlag(F_REVERSE, false);
            changed(false, true);
        }
    });
    connect(radial_, &QCheckBox::toggled, this, [this](bool on) { editFlag(F_RADIAL, on); });
    connect(radialWay_, &Segmented::changed, this, [this](int i) { editFlag(F_REVERSE, i == 1); });
    connect(centerBtn_, &QPushButton::toggled, this, [this](bool on) {
        pickingCenter_ = on;
        refreshKeys();
    });
    connect(mirror_, &QCheckBox::toggled, this, [this](bool on) { editFlag(F_MIRROR, on); });
    connect(speed_, &QSlider::valueChanged, this, [this](int v) { edit([v](Layer &l) { l.speed = v; }); });
    connect(size_, &QSlider::valueChanged, this, [this](int v) { edit([v](Layer &l) { l.size = v; }); });
    connect(param_, &QSlider::valueChanged, this, [this](int v) { edit([v](Layer &l) { l.param = v; }); });
    connect(opacity_, &QSlider::valueChanged, this, [this](int v) { edit([v](Layer &l) { l.opacity = v; }); });
    connect(blend_, &Segmented::changed, this, [this](int i) { edit([i](Layer &l) { l.blend = i; }); });
    connect(keysMode_, &Segmented::changed, this, [this](int i) {
        Layer *l = layer();
        if (updating_ || !l || i < 0 || i >= keysModes_.size() || keysModes_[i] == l->keys)
            return;
        // the mask keeps what the layer shows: firmware before 1.9.0 and CUSTOM start from it
        l->mask = covered(*l);
        l->keys = keysModes_[i];
        l->mask = covered(*l);
        changed(true, true);
    });
    connect(previewLayer_, &QComboBox::currentIndexChanged, this, [this] { kb_->update(); });
    connect(ctl, &Controller::keymapChanged, this, [this] {
        layerKeys_.clear();
        refreshKeys();
    });
    connect(cond_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!updating_ && i >= 0)
            edit([c = cond_->itemData(i).toInt()](Layer &l) { l.cond = c; });
    });

    frameTimer_.setInterval(1000 / FPS);
    connect(&frameTimer_, &QTimer::timeout, this, &ThemeEditorPage::tick);
    pushTimer_.setSingleShot(true);
    pushTimer_.setInterval(PUSH_MS);
    connect(&pushTimer_, &QTimer::timeout, this, &ThemeEditorPage::push);
    groupTimer_.setSingleShot(true);
    groupTimer_.setInterval(UNDO_GROUP_MS);
    connect(&groupTimer_, &QTimer::timeout, this, &ThemeEditorPage::closeGroup);

    for (auto [seq, fn] : {std::pair{QKeySequence(QKeySequence::Undo), &ThemeEditorPage::onUndo},
                           std::pair{QKeySequence(QKeySequence::Redo), &ThemeEditorPage::onRedo},
                           std::pair{QKeySequence(Qt::CTRL | Qt::Key_Y), &ThemeEditorPage::onRedo},
                           std::pair{QKeySequence(QKeySequence::Save), &ThemeEditorPage::onSave}}) {
        auto *sc = new QShortcut(seq, this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    }
}

ThemeEditorPage::Row ThemeEditorPage::addRow(QGridLayout *grid, int r, QWidget *label, QWidget *widget, QWidget *value)
{
    grid->addWidget(label, r, 0, qobject_cast<QSlider *>(widget) ? Qt::AlignVCenter : Qt::AlignTop);
    grid->addWidget(widget, r, 1, 1, value ? 1 : 2);
    Row row{{label, widget}};
    if (value) {
        value->setFixedWidth(84);
        static_cast<QLabel *>(value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(value, r, 2);
        row.widgets.append(value);
    }
    return row;
}

ThemeEditorPage::Row ThemeEditorPage::addRow(QGridLayout *grid, int r, QWidget *label, QLayout *layout)
{
    auto *holder = new QWidget;
    layout->setContentsMargins(0, 0, 0, 0);
    holder->setLayout(layout);
    return addRow(grid, r, label, holder);
}

void ThemeEditorPage::showRow(const Row &row, bool on)
{
    for (QWidget *w : row.widgets)
        w->setVisible(on);
}

bool ThemeEditorPage::loaded() const
{
    return ctl->themes().has_value() && slot_ < ctl->themes()->list.size() && !saved_.isEmpty();
}

theme::Theme &ThemeEditorPage::theme()
{
    return ctl->themes()->list[slot_];
}

theme::Layer *ThemeEditorPage::layer()
{
    if (!loaded())
        return nullptr;
    auto &layers = theme().layers;
    return cur_ >= 0 && cur_ < layers.size() ? &layers[cur_] : nullptr;
}

int ThemeEditorPage::nLeds() const
{
    return ctl->themes() ? ctl->themes()->info.leds : 0;
}

QByteArray ThemeEditorPage::pack(int slot) const
{
    const auto &t = *ctl->themes();
    return t.list[slot].pack(t.info.leds, t.info.layers);
}

void ThemeEditorPage::keyboardChanged()
{
    saved_.clear();
    sent_.clear();
    const KeyboardState *kb = ctl->keyboard();
    if (!kb || !kb->armed)
        return;
    const auto &leds = kb->armed->leds;
    renderer_ = std::make_unique<lighting::ThemeRenderer>(leds);
    kb_->setKeys(kb->def.keys, kb->def.encoders);
    ledKeys_.clear();
    keyLed_.clear();
    for (int i = 0; i < leds.size(); ++i) {
        int found = -1;
        for (int k = 0; k < kb_->keys().size(); ++k)
            if (!kb_->keys()[k].encoder && kb_->keys()[k].def.row == leds[i].row && kb_->keys()[k].def.col == leds[i].col)
                found = k;
        ledKeys_.append(found);
        if (found >= 0)
            keyLed_.insert(found, i);
    }
    for (auto &k : kb_->keys()) {
        k.text.clear();
        k.glow = QColor(0, 0, 0);
    }
    updating_ = true;
    cond_->clear();
    cond_->addItem(tr("ALWAYS"), int(C_ALWAYS));
    cond_->addItem(tr("CAPS LOCK ON"), int(C_CAPS));
    cond_->addItem(tr("CAPS LOCK OFF"), int(C_NO_CAPS));
    cond_->addItem(tr("BASE LAYER ONLY"), int(C_LAYER));
    for (int n = 1; n < std::min(16, kb->layers); ++n)
        cond_->addItem(tr("FN%1 LAYER").arg(n), C_LAYER | n);
    QStringList modes{tr("ALL KEYS"), tr("CUSTOM")};
    keysModes_ = {K_ALL, K_CUSTOM};
    previewLayer_->clear();
    previewLayer_->addItem(tr("BASE"), 0);
    for (int n = 1; n < std::min(16, kb->layers); ++n) {
        modes << tr("FN%1").arg(n);
        keysModes_ << n;
        previewLayer_->addItem(tr("FN%1").arg(n), n);
    }
    keysMode_->setLabels(modes);
    layerKeys_.clear();
    updating_ = false;
    refreshAll();
}

void ThemeEditorPage::loadThemes()
{
    ctl->loadThemes([this](bool ok) {
        if (!ok || !ctl->themes()) {
            state_->setText(tr("CAN'T READ THEMES"));
            return;
        }
        const auto &t = *ctl->themes();
        const KeyboardState *kb = ctl->keyboard();
        if (!kb || !kb->armed || t.info.leds != kb->armed->leds.size()) {
            state_->setText(tr("CAN'T READ THEMES: unexpected theme layout"));
            return;
        }
        saved_.clear();
        for (int s = 0; s < t.list.size(); ++s)
            saved_.append(pack(s));
        sent_ = saved_;
        undo_ = QList<QList<Theme>>(t.list.size());
        redo_ = QList<QList<Theme>>(t.list.size());
        lastState_ = t.list;
        const int effect = ctl->armedValue(armed::V_EFFECT);
        const int idx = int(t.info.modes.indexOf(effect));
        slot_ = idx >= 0 ? idx : std::min(slot_, int(t.list.size()) - 1);
        cur_ = int(theme().layers.size()) - 1;
        groups_ = keyGroups(kb->armed->leds, [kb](int l, int r, int c) { return kb->key(l, r, c); });
        refreshAll();
    });
}

// ---- editing

void ThemeEditorPage::changed(bool layersChanged, bool controls)
{
    // every edit ends here: undo grouping, preview, keyboard
    if (!groupTimer_.isActive()) {
        undo_[slot_].append(lastState_[slot_]);
        if (undo_[slot_].size() > UNDO_DEPTH)
            undo_[slot_].removeFirst();
        redo_[slot_].clear();
    }
    groupTimer_.start();
    if (layersChanged)
        refreshLayers();
    else
        refreshLayerRow();
    if (controls)
        refreshControls();
    refreshKeys();
    pushTimer_.start();
}

void ThemeEditorPage::closeGroup()
{
    if (loaded())
        lastState_[slot_] = theme();
}

void ThemeEditorPage::flushGroup()
{
    if (groupTimer_.isActive()) {
        groupTimer_.stop();
        closeGroup();
    }
}

void ThemeEditorPage::edit(const std::function<void(theme::Layer &)> &f)
{
    Layer *l = layer();
    if (updating_ || !l)
        return;
    f(*l);
    refreshValues();
    changed();
}

void ThemeEditorPage::editFlag(int flag, bool on)
{
    Layer *l = layer();
    if (updating_ || !l)
        return;
    l->setFlag(flag, on);
    if (flag == F_RADIAL && !on)
        l->setFlag(F_REVERSE, false);
    changed(false, true);
}

void ThemeEditorPage::applyToKeyboard()
{
    const int mode = ctl->themes()->info.modes.value(slot_, -1);
    if (mode >= 0 && ctl->armedValue(armed::V_EFFECT) != mode)
        ctl->armedSet(armed::V_EFFECT, mode);
    if (!ctl->armedValue(armed::V_ENABLED))
        ctl->armedSet(armed::V_ENABLED, 1);
}

void ThemeEditorPage::push()
{
    if (!loaded())
        return;
    const QByteArray now = pack(slot_), old = sent_.value(slot_);
    // the bytes that changed, merging gaps shorter than 6 bytes into one write
    QList<QPair<int, int>> ranges;
    for (int i = 0; i < now.size();) {
        if (i < old.size() && now[i] == old[i]) {
            ++i;
            continue;
        }
        int j = i;
        auto differs = [&](int k) { return k >= old.size() || now[k] != old[k]; };
        while (j < now.size()) {
            bool near = false;
            for (int k = j; k < std::min<qsizetype>(now.size(), j + 6); ++k)
                near = near || differs(k);
            if (!differs(j) && !near)
                break;
            ++j;
        }
        ranges.append({i, j});
        i = j;
    }
    if (ranges.isEmpty())
        return;
    const int slot = slot_;
    ctl->write(false, [slot, ranges, now](hid::Link &link) {
        for (const auto &[a, b] : ranges)
            armed::themeWrite(link, slot, a, now.mid(a, b - a));
    }, [this] { state_->setText(tr("KEYBOARD ERROR")); }, [this, slot, now] {
        if (slot < sent_.size())
            sent_[slot] = now;
        refreshState();
    }, QStringLiteral("theme-push-%1").arg(slot));
    applyToKeyboard();
}

void ThemeEditorPage::onType(int index)
{
    Layer *l = layer();
    if (updating_ || !l)
        return;
    const int tid = type_->itemData(index).toInt();
    if (tid == l->type)
        return;
    Layer fresh = Layer::make(tid, tid == PAINT ? QSet<int>{} : l->mask);
    fresh.blend = l->blend;
    fresh.opacity = l->opacity;
    fresh.cond = l->cond;
    fresh.keys = tid == PAINT ? int(K_CUSTOM) : l->keys;
    fresh.dir = l->dir;
    fresh.flags |= l->flags & F_HIDDEN;
    *l = fresh;
    changed(true, true);
}

void ThemeEditorPage::onAdd(int type)
{
    if (!loaded() || theme().layers.size() >= ctl->themes()->info.layers)
        return;
    QSet<int> mask;
    if (type != PAINT)
        for (int i = 0; i < nLeds(); ++i)
            mask.insert(i);
    Layer fresh = Layer::make(type, mask);
    if (type != PAINT)
        fresh.keys = K_ALL;
    theme().layers.insert(cur_ + 1, fresh);
    ++cur_;
    changed(true, true);
}

void ThemeEditorPage::onDuplicate()
{
    Layer *l = layer();
    if (!l || theme().layers.size() >= ctl->themes()->info.layers)
        return;
    const Layer copy = *l;
    theme().layers.insert(cur_ + 1, copy);
    ++cur_;
    changed(true, true);
}

void ThemeEditorPage::onMove(int d)
{
    auto &ls = theme().layers;
    const int j = cur_ + d;
    if (!layer() || j < 0 || j >= ls.size())
        return;
    ls.swapItemsAt(cur_, j);
    cur_ = j;
    changed(true);
}

void ThemeEditorPage::onDelete()
{
    if (!layer())
        return;
    theme().layers.removeAt(cur_);
    cur_ = std::min(cur_, int(theme().layers.size()) - 1);
    changed(true, true);
}

void ThemeEditorPage::onGroup(const QString &g)
{
    Layer *l = layer();
    if (!l)
        return;
    makeCustom(*l);
    QSet<int> everything;
    for (int i = 0; i < nLeds(); ++i)
        everything.insert(i);
    if (g == "INVERT") {
        const QSet<int> fresh = everything - l->mask;
        l->mask = fresh;
        if (l->type == PAINT)
            for (int i : fresh)
                theme().paint[i] = l->a;
        changed();
        return;
    }
    QSet<int> keys = g == "NONE" ? everything : groups_.value(g);
    const bool add = g != "NONE" && (l->type == PAINT || !l->mask.contains(keys));
    if (add) {
        l->mask |= keys;
        if (l->type == PAINT)
            for (int i : keys)
                theme().paint[i] = l->a;
    } else {
        l->mask -= keys;
    }
    changed();
}

void ThemeEditorPage::onPreset(int index)
{
    if (!loaded())
        return;
    const KeyboardState *kb = ctl->keyboard();
    const QString name = theme().name;
    Theme t = buildPreset(index, kb->armed->leds, [kb](int l, int r, int c) { return kb->key(l, r, c); }, kb->layers);
    if (presetNames().value(index) == "BLANK")
        t.name = name;
    theme() = t;
    cur_ = int(t.layers.size()) - 1;
    changed(true, true);
    refreshAll();
}

void ThemeEditorPage::onUndo()
{
    if (!loaded())
        return;
    flushGroup();
    if (undo_[slot_].isEmpty())
        return;
    redo_[slot_].append(theme());
    theme() = undo_[slot_].takeLast();
    lastState_[slot_] = theme();
    cur_ = std::clamp(cur_, 0, int(theme().layers.size()) - 1);
    refreshAll();
    pushTimer_.start();
}

void ThemeEditorPage::onRedo()
{
    if (!loaded())
        return;
    flushGroup();
    if (redo_[slot_].isEmpty())
        return;
    undo_[slot_].append(theme());
    theme() = redo_[slot_].takeLast();
    lastState_[slot_] = theme();
    cur_ = std::clamp(cur_, 0, int(theme().layers.size()) - 1);
    refreshAll();
    pushTimer_.start();
}

void ThemeEditorPage::onSlot(int slot)
{
    if (!loaded())
        return;
    flushGroup();
    pushTimer_.stop();
    push();
    slot_ = slot;
    cur_ = int(theme().layers.size()) - 1;
    centerBtn_->setChecked(false);
    applyToKeyboard();
    refreshAll();
}

void ThemeEditorPage::onSave()
{
    if (!loaded())
        return;
    flushGroup();
    pushTimer_.stop();
    push();
    const int slot = slot_;
    const QByteArray packed = pack(slot);
    ctl->write(false, [slot](hid::Link &link) { armed::themeSave(link, slot); }, {}, [this, slot, packed] {
        saved_[slot] = packed;
        refreshState();
        ctl->toast(tr("Theme saved"), Controller::ToastKind::Ok);
    });
    applyToKeyboard();
}

void ThemeEditorPage::onRevert()
{
    if (!loaded() || !ctl->session())
        return;
    flushGroup();
    pushTimer_.stop();
    const int slot = slot_, size = ctl->themes()->info.size;
    ctl->session()->run(hid::Priority::Interactive, this, [slot, size](hid::Link &link) {
        armed::themeRevert(link, slot);
        return armed::themeRead(link, slot, size);
    }, [this, slot](hid::Result<QByteArray> r) {
        if (!r.ok() || !loaded()) {
            state_->setText(tr("KEYBOARD ERROR: %1").arg(r.error));
            return;
        }
        const auto &info = ctl->themes()->info;
        undo_[slot].append(ctl->themes()->list[slot]);
        ctl->themes()->list[slot] = Theme::unpack(*r.value, info.leds, info.layers);
        saved_[slot] = sent_[slot] = *r.value;
        lastState_[slot] = ctl->themes()->list[slot];
        if (slot == slot_)
            cur_ = int(theme().layers.size()) - 1;
        refreshAll();
    });
}

void ThemeEditorPage::onExport()
{
    if (!loaded())
        return;
    const QString name = (theme().name.isEmpty() ? QStringLiteral("theme") : theme().name).toLower().replace(' ', '-');
    QString path = QFileDialog::getSaveFileName(this, tr("Export theme"), name + ".json",
                                                tr("Vial Armed theme (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(theme().toJson(ctl->keyboard()->armed->leds));
    else
        ctl->toast(tr("Could not save %1").arg(path), Controller::ToastKind::Error);
}

void ThemeEditorPage::onImport()
{
    if (!loaded())
        return;
    QString path = QFileDialog::getOpenFileName(this, tr("Import theme"), {}, tr("Vial Armed theme (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ctl->toast(tr("Could not open %1").arg(path), Controller::ToastKind::Error);
        return;
    }
    auto t = Theme::fromJson(f.readAll(), ctl->keyboard()->armed->leds, ctl->themes()->info.layers);
    if (auto *err = std::get_if<QString>(&t)) {
        ctl->toast(tr("Not a theme file: %1").arg(*err), Controller::ToastKind::Error);
        return;
    }
    theme() = std::get<Theme>(t);
    cur_ = int(theme().layers.size()) - 1;
    changed(true, true);
    refreshAll();
}

// ---- keyboard interaction

void ThemeEditorPage::keyDown(int key, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
    Layer *l = layer();
    if (!l || !keyLed_.contains(key))
        return;
    const int i = keyLed_.value(key);
    if (pickingCenter_) {
        const auto &led = ctl->keyboard()->armed->leds[i];
        l->cx = led.x;
        l->cy = led.y;
        l->setFlag(F_RADIAL, true);
        centerBtn_->setChecked(false);
        changed(false, true);
        return;
    }
    if (l->type == PAINT && button == Qt::LeftButton && (mods & (Qt::ControlModifier | Qt::AltModifier))) {
        if (l->mask.contains(i)) {
            l->a = theme().paint[i];
            refreshControls();
        }
        return;
    }
    makeCustom(*l);
    if (button == Qt::RightButton)
        dragAction_ = Drag::Remove;
    else if (l->type == PAINT)
        dragAction_ = Drag::Paint;
    else
        dragAction_ = l->mask.contains(i) ? Drag::Remove : Drag::Add;
    keyDrag(key);
}

void ThemeEditorPage::keyDrag(int key)
{
    Layer *l = layer();
    if (!l || !keyLed_.contains(key) || dragAction_ == Drag::None)
        return;
    const int i = keyLed_.value(key);
    if (dragAction_ == Drag::Remove) {
        l->mask.remove(i);
    } else {
        l->mask.insert(i);
        if (dragAction_ == Drag::Paint)
            theme().paint[i] = l->a;
    }
    changed();
}

QSet<int> ThemeEditorPage::coveredKeys() const
{
    QSet<int> out;
    auto *self = const_cast<ThemeEditorPage *>(this);
    const Layer *l = self->layer();
    if (!l || l->type == PAINT)
        return out;
    for (int i : covered(*l))
        if (i < ledKeys_.size() && ledKeys_[i] >= 0)
            out.insert(ledKeys_[i]);
    return out;
}

QSet<int> ThemeEditorPage::layerKeys(int n) const
{
    auto it = layerKeys_.constFind(n);
    if (it != layerKeys_.constEnd())
        return *it;
    const KeyboardState *kb = ctl->keyboard();
    QSet<int> keys;
    if (kb && kb->armed)
        keys = theme::layerKeys(kb->armed->leds, [kb](int l, int r, int c) { return kb->key(l, r, c); }, kb->layers, n);
    layerKeys_.insert(n, keys);
    return keys;
}

QSet<int> ThemeEditorPage::covered(const Layer &l) const
{
    return theme::coverage(l, nLeds(), [this](int n) { return layerKeys(n); });
}

// editing single keys turns ALL KEYS or an FN layer into CUSTOM, starting from the keys it showed
void ThemeEditorPage::makeCustom(Layer &l) const
{
    if (l.keys == K_CUSTOM || l.type == PAINT)
        return;
    l.mask = covered(l);
    l.keys = K_CUSTOM;
}

std::optional<QPointF> ThemeEditorPage::centerPoint() const
{
    auto *self = const_cast<ThemeEditorPage *>(this);
    const Layer *l = self->layer();
    const KeyboardState *kb = ctl->keyboard();
    if (!l || !kb || !kb->armed)
        return std::nullopt;
    const auto *t = typeById(l->type);
    if (!t || !t->controls.contains("dir") || !l->flag(F_RADIAL))
        return std::nullopt;
    // a linear map from LED coordinates (0..224, 0..64) to the drawn keys
    QList<std::array<double, 4>> pts;
    const auto &keys = kb_->keys();
    for (int i = 0; i < ledKeys_.size(); ++i) {
        if (ledKeys_[i] < 0)
            continue;
        const QPointF c = keys[ledKeys_[i]].polygon.boundingRect().center();
        pts.append({double(kb->armed->leds[i].x), double(kb->armed->leds[i].y), c.x(), c.y()});
    }
    if (pts.size() < 2)
        return std::nullopt;
    auto fit = [&pts](int v, int p) {
        double mv = 0, mp = 0;
        for (const auto &q : pts) {
            mv += q[v];
            mp += q[p];
        }
        mv /= pts.size();
        mp /= pts.size();
        double var = 0, cov = 0;
        for (const auto &q : pts) {
            var += (q[v] - mv) * (q[v] - mv);
            cov += (q[v] - mv) * (q[p] - mp);
        }
        const double k = cov / (var ? var : 1);
        return std::pair{k, mp - k * mv};
    };
    const auto [kx, bx] = fit(0, 2);
    const auto [ky, by] = fit(1, 3);
    return QPointF((kx * l->cx + bx) * kb_->scale(), (ky * l->cy + by) * kb_->scale());
}

// ---- display

QStringList ThemeEditorPage::slotLabels() const
{
    QStringList out;
    for (int i = 0; i < ctl->themes()->list.size(); ++i) {
        const QString n = ctl->themes()->list[i].name;
        out << QStringLiteral("%1 · %2").arg(i + 1).arg(n.isEmpty() ? QStringLiteral("THEME") : n);
    }
    return out;
}

void ThemeEditorPage::refreshAll()
{
    if (!loaded())
        return;
    updating_ = true;
    slots_->setLabels(slotLabels());
    slots_->setCurrent(slot_);
    name_->setText(theme().name);
    updating_ = false;
    refreshLayers();
    refreshControls();
    refreshKeys();
    refreshState();
}

void ThemeEditorPage::refreshState()
{
    if (!loaded())
        return;
    const bool dirty = pack(slot_) != saved_.value(slot_);
    state_->setText(QStringLiteral("<span style=\"color:%1\">■</span> %2")
                        .arg((dirty ? ACC : OK).name(), dirty ? tr("UNSAVED") : tr("SAVED")));
    state_->setToolTip(dirty ? tr("The keyboard already shows your changes; SAVE keeps them after it is unplugged") : QString());
    saveBtn_->setEnabled(dirty);
    revertBtn_->setEnabled(dirty);
    undoBtn_->setEnabled(!undo_[slot_].isEmpty() || groupTimer_.isActive());
    redoBtn_->setEnabled(!redo_[slot_].isEmpty());
}

void ThemeEditorPage::refreshLayers()
{
    if (!loaded())
        return;
    updating_ = true;
    const auto &ls = theme().layers;
    layers_->clear();
    for (int idx = int(ls.size()) - 1; idx >= 0; --idx) {
        auto *item = new QListWidgetItem(layerSummary(ls[idx]));
        item->setData(Qt::UserRole, QVariant::fromValue(ls[idx]));
        item->setData(Qt::UserRole + 1, idx + 1);
        layers_->addItem(item);
    }
    if (layer())
        layers_->setCurrentRow(int(ls.size()) - 1 - cur_);
    const int max = ctl->themes()->info.layers;
    count_->setText(tr("%1 / %2 · TOP DRAWS OVER BOTTOM").arg(ls.size()).arg(max));
    const bool full = ls.size() >= max;
    addBtn_->setEnabled(!full);
    dupBtn_->setEnabled(!full && layer());
    for (QPushButton *b : {delBtn_, upBtn_, downBtn_})
        b->setEnabled(layer() != nullptr);
    updating_ = false;
    refreshState();
}

void ThemeEditorPage::refreshLayerRow()
{
    if (Layer *l = layer()) {
        if (auto *item = layers_->item(int(theme().layers.size()) - 1 - cur_)) {
            item->setText(layerSummary(*l));
            item->setData(Qt::UserRole, QVariant::fromValue(*l));
        }
    }
    slots_->setLabels(slotLabels());
    slots_->setCurrent(slot_);
    refreshState();
}

void ThemeEditorPage::refreshControls()
{
    Layer *l = layer();
    empty_->setVisible(!l);
    const QList<Row *> rows = {&rowKeys_, &rowType_, &rowColors_, &rowDir_, &rowSpeed_, &rowSize_, &rowParam_, &rowOpacity_,
                               &rowBlend_, &rowCond_};
    if (!l) {
        hint_->clear();
        props_->title()->setText(tr("LAYER"));
        for (Row *r : rows)
            showRow(*r, false);
        return;
    }
    const LayerType *t = typeById(l->type);
    if (!t)
        t = &types().first();
    const auto &c = t->controls;
    updating_ = true;
    props_->title()->setText(tr("LAYER %1 · %2").arg(cur_ + 1, 2, 10, QLatin1Char('0')).arg(t->name));
    hint_->setText(t->hint);
    type_->setCurrentIndex(type_->findData(l->type));
    for (Row *r : {&rowType_, &rowOpacity_, &rowBlend_, &rowCond_})
        showRow(*r, true);
    showRow(rowColors_, c.contains("a") || c.contains("b") || c.contains("paint"));
    colorsLabel_->setText(l->type == PAINT ? tr("BRUSH") : c.contains("b") ? tr("COLOURS") : tr("COLOUR"));
    colorB_->setVisible(c.contains("b"));
    swapBtn_->setVisible(c.contains("b"));
    colorA_->setLabel(l->type == PAINT || !c.contains("b") ? QString() : QStringLiteral("A"));
    colorA_->setColor(l->a);
    colorB_->setColor(l->b);
    showRow(rowDir_, c.contains("dir"));
    if (c.contains("dir")) {
        const bool radial = l->flag(F_RADIAL);
        dial_->setValue(l->dir, radial);
        radial_->setChecked(radial);
        radialWay_->setVisible(radial);
        radialWay_->setCurrent(l->flag(F_REVERSE) ? 1 : 0);
        mirror_->setVisible(c.contains("mirror"));
        mirror_->setText(l->type == GRADIENT ? tr("MIRROR  A → B → A") : l->type == WAVE ? tr("BOUNCE") : tr("MIRROR"));
        mirror_->setChecked(l->flag(F_MIRROR));
    }
    showRow(rowSpeed_, c.contains("speed"));
    speedLabel_->setText(l->type == SPARKLE ? tr("DENSITY") : tr("SPEED"));
    speed_->setValue(l->speed);
    showRow(rowSize_, c.contains("size"));
    sizeLabel_->setText(sizeLabel(l->type));
    size_->setValue(l->size);
    showRow(rowParam_, c.contains("param"));
    paramLabel_->setText(l->type == RAINBOW ? tr("SATURATION") : l->type == REACTIVE ? tr("SPREAD") : QString());
    param_->setMaximum(l->type == REACTIVE ? 90 : 255);
    param_->setValue(l->type == REACTIVE && l->param == 255 ? 0 : l->param);
    opacity_->setValue(l->opacity);
    blend_->setCurrent(l->blend);
    showRow(rowKeys_, l->type != PAINT);
    keysMode_->setCurrent(int(keysModes_.indexOf(l->keys)));
    int idx = cond_->findData(l->cond);
    if (idx < 0) {
        const int n = l->cond & 0x0F;
        cond_->addItem((l->cond & 0xF0) == C_NOT_LAYER ? (n ? tr("NOT FN%1 LAYER").arg(n) : tr("ANY FN LAYER"))
                                                       : tr("CONDITION 0x%1").arg(l->cond, 2, 16, QLatin1Char('0')).toUpper(),
                       l->cond);
        idx = cond_->count() - 1;
    }
    cond_->setCurrentIndex(idx);
    updating_ = false;
    refreshValues();
}

void ThemeEditorPage::refreshValues()
{
    Layer *l = layer();
    if (!l)
        return;
    const int tp = l->type;
    if (tp == SPARKLE || tp == RIPPLE)
        speedVal_->setText(pct(l->speed));
    else
        speedVal_->setText(l->speed ? seconds(65536.0 / l->speed) : tr("STILL"));
    if (tp == SPARKLE)
        sizeVal_->setText(seconds(255.0 / (1 + (255 - l->size) / 16) * 16));
    else if (tp == REACTIVE)
        sizeVal_->setText(seconds(100 + l->size * 8));
    else if (tp == GRADIENT || tp == RAINBOW)
        sizeVal_->setText(l->size ? pct(l->size, 128) : tr("FLAT"));
    else if (tp == PULSE)
        sizeVal_->setText(l->size ? pct(l->size, 128) : tr("TOGETHER"));
    else
        sizeVal_->setText(pct(l->size));
    if (tp == REACTIVE)
        paramVal_->setText(l->param == 0 || l->param == 255 ? tr("KEY ONLY") : tr("%1 KEYS").arg(l->param / 15.0, 0, 'f', 1));
    else
        paramVal_->setText(pct(l->param));
    opacityVal_->setText(pct(l->opacity));
    const bool alone = l->blend != NORMAL && mixesWithNothing();
    mixHint_->setVisible(alone);
    if (alone)
        mixHint_->setText(l->blend == ADD ? tr("No layer below shows together with this one (see SHOW), so ADD has nothing "
                                               "to brighten and looks like NORMAL")
                                          : tr("No layer below shows together with this one (see SHOW), so MULTIPLY tints "
                                               "black and the keys stay dark"));
}

namespace {

// whether two SHOW conditions can hold at once; a layer condition is about the active keymap layer
bool together(int a, int b)
{
    auto caps = [](int c) { return c == C_CAPS ? 1 : c == C_NO_CAPS ? 0 : -1; };
    if (caps(a) >= 0 && caps(b) >= 0 && caps(a) != caps(b))
        return false;
    const int ka = a & 0xF0, kb = b & 0xF0, na = a & 0x0F, nb = b & 0x0F;
    if (ka == C_LAYER && kb == C_LAYER)
        return na == nb;
    if ((ka == C_LAYER && kb == C_NOT_LAYER) || (ka == C_NOT_LAYER && kb == C_LAYER))
        return na != nb;
    return true;
}

}

// MIX blends with the layers below that are on at the same time; with none, ADD and MULTIPLY see black
bool ThemeEditorPage::mixesWithNothing() const
{
    auto *self = const_cast<ThemeEditorPage *>(this);
    const Layer *l = self->layer();
    if (!l)
        return false;
    const QSet<int> mine = covered(*l);
    const auto &ls = self->theme().layers;
    for (int j = 0; j < cur_ && j < ls.size(); ++j) {
        const Layer &b = ls[j];
        if (b.type != NONE && !b.hidden() && together(l->cond, b.cond) && covered(b).intersects(mine))
            return false;
    }
    return true;
}

void ThemeEditorPage::refreshKeys()
{
    Layer *l = layer();
    if (pickingCenter_)
        keysHint_->setText(tr("CLICK A KEY TO PUT THE POINT THERE"));
    else if (!l)
        keysHint_->clear();
    else if (l->type == PAINT)
        keysHint_->setText(tr("CLICK / DRAG TO PAINT · RIGHT-CLICK ERASES · CTRL+CLICK PICKS A KEY'S COLOUR"));
    else if (l->keys == K_ALL)
        keysHint_->setText(tr("◤ = THIS LAYER SHOWS THERE · ALL KEYS · CLICK / DRAG TO PICK KEYS BY HAND"));
    else if (l->keys)
        keysHint_->setText(tr("◤ = THIS LAYER SHOWS THERE · FN%1 LAYER, FOLLOWS REMAPS · CLICK / DRAG TO PICK KEYS BY HAND").arg(l->keys));
    else
        keysHint_->setText(tr("◤ = THIS LAYER SHOWS THERE · CLICK / DRAG TO CHANGE · RIGHT-DRAG REMOVES"));
    tools_->setText(l && l->type == PAINT ? tr("PAINT") : tr("KEYS"));
    for (QPushButton *b : groupButtons_)
        b->setEnabled(l != nullptr);
    kb_->update();
}

void ThemeEditorPage::activate()
{
    if (!loaded()) {
        loadThemes();
    } else {
        // open the slot lit now, if another page switched to a theme
        ctl->armedReload([this] {
            if (!loaded())
                return;
            const int idx = int(ctl->themes()->info.modes.indexOf(ctl->armedValue(armed::V_EFFECT)));
            if (idx >= 0 && idx != slot_) {
                flushGroup();
                slot_ = idx;
                cur_ = int(theme().layers.size()) - 1;
                centerBtn_->setChecked(false);
                refreshAll();
            }
        });
    }
    frameTimer_.start();
}

void ThemeEditorPage::deactivate()
{
    frameTimer_.stop();
    if (loaded()) {
        flushGroup();
        pushTimer_.stop();
        push();
    }
}

void ThemeEditorPage::tick()
{
    if (!loaded() || !renderer_ || !isVisible())
        return;
    const qint64 t = clock_.elapsed();
    Theme shown = theme();
    if (solo_->isChecked() && layer()) {
        Layer only = *layer();
        only.setFlag(F_HIDDEN, false);
        shown.layers = {only};
    }
    bool reactive = false;
    for (const auto &l : shown.layers)
        reactive = reactive || ((l.type == REACTIVE || l.type == RIPPLE) && !l.hidden());
    if (reactive && t - lastDemo_ > DEMO_PRESS_MS && nLeds()) {
        lastDemo_ = t;
        renderer_->hit(int(QRandomGenerator::global()->bounded(nLeds())), t);
    }
    renderer_->caps = caps_->isChecked();
    const int on = previewLayer_->currentData().toInt();
    renderer_->layersOn = on ? QSet<int>{on} : QSet<int>{};
    renderer_->layerKeys = [this](int n) { return layerKeys(n); };
    const auto &io = *ctl->keyboard()->armed;
    const int val = ctl->armedValue(armed::V_VAL);
    const int v = val ? std::min(255, val * 255 / std::max(1, io.maxBrightness)) : 255;
    const auto colors = renderer_->frame(shown, t, std::max(v, 90));
    auto &keys = kb_->keys();
    for (int i = 0; i < ledKeys_.size() && i < colors.size(); ++i)
        if (ledKeys_[i] >= 0)
            keys[ledKeys_[i]].glow = QColor(colors[i][0], colors[i][1], colors[i][2]);
    kb_->update();
}
