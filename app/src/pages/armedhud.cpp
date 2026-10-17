#include "pages/armedhud.h"

#include "app/agenthost.h"
#include "core/paths.h"
#include "model/hudpresets.h"
#include "ui/theme.h"
#include "ui/widgets/armedcontrols.h"
#include "ui/widgets/hudcanvas.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

using namespace hud;
namespace th = ui::theme;

namespace {

constexpr int UNDO_DEPTH = 100;
constexpr int PUSH_DELAY_MS = 120;
constexpr int POLL_MS = 150;
constexpr int CONFIG_SAVE_MS = 600;
const char *const CLIPBOARD_KEY = "vial-armed-hud-widgets";

const QList<int> kTypes = {W_TEXT, W_BAR, W_GRAPH, W_INDICATOR, W_BATTERY, W_FRAME, W_TEXTBOX};

QString roleTip(int r)
{
    switch (r) {
    case C_AUTO: return QObject::tr("AUTO: the colour the source suggests (accent above the base layer, green while charging, link state, ...)");
    case C_BG: return QObject::tr("BG: the screen's black");
    case C_PANEL: return QObject::tr("PANEL: lit tiles");
    case C_LINE: return QObject::tr("LINE: separators, unlit parts");
    case C_DIM: return QObject::tr("DIM: labels");
    case C_TEXT: return QObject::tr("TEXT: values");
    case C_ACCENT: return QObject::tr("ACCENT: follows DISPLAY > ACCENT on the keyboard");
    case C_OK: return QObject::tr("OK: green");
    case C_WARN: return QObject::tr("WARN: red");
    }
    return roleName(r);
}

QString typeTip(int t)
{
    switch (t) {
    case W_TEXT: return QObject::tr("Text with a live value in place of {}, or a fixed label");
    case W_BAR: return QObject::tr("A bar filled by a number between two ends");
    case W_GRAPH: return QObject::tr("A number over time");
    case W_INDICATOR: return QObject::tr("A tile lit while the source is on, or a coloured dot");
    case W_BATTERY: return QObject::tr("The keyboard's battery icon");
    case W_FRAME: return QObject::tr("Corner brackets, a box, a line or a filled panel");
    case W_TEXTBOX: return QObject::tr("Several lines of text, e.g. a command's output");
    }
    return {};
}

// small line icons for the arrange tools
QPixmap iconPixmap(const QString &kind, const QColor &fg, const QColor &acc, int s = 18)
{
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter qp(&pm);
    auto bar = [&](int x, int y, int w, int h, const QColor &c) { qp.fillRect(x, y, w, h, c); };
    if (kind.startsWith("align_")) {
        const QString k = kind.mid(6);
        if (k == "left" || k == "hcenter" || k == "right") {
            const int x = k == "left" ? 2 : k == "hcenter" ? s / 2 : s - 3;
            bar(x, 1, 1, s - 2, acc);
            for (auto [y, w] : {std::pair{4, 11}, std::pair{10, 7}}) {
                const int bx = k == "left" ? 3 : k == "hcenter" ? s / 2 - w / 2 : s - 3 - w;
                bar(bx, y, w, 4, fg);
            }
        } else {
            const int y = k == "top" ? 2 : k == "vcenter" ? s / 2 : s - 3;
            bar(1, y, s - 2, 1, acc);
            for (auto [x, h] : {std::pair{4, 11}, std::pair{10, 7}}) {
                const int by = k == "top" ? 3 : k == "vcenter" ? s / 2 - h / 2 : s - 3 - h;
                bar(x, by, 4, h, fg);
            }
        }
    } else if (kind == "dist_h") {
        bar(1, 1, 1, s - 2, acc);
        bar(s - 2, 1, 1, s - 2, acc);
        for (int x : {3, 8, 13})
            bar(x, 5, 2, 8, fg);
    } else if (kind == "dist_v") {
        bar(1, 1, s - 2, 1, acc);
        bar(1, s - 2, s - 2, 1, acc);
        for (int y : {3, 8, 13})
            bar(5, y, 8, 2, fg);
    } else if (kind == "same_w") {
        bar(3, 3, 12, 4, fg);
        bar(3, 11, 12, 4, fg);
        bar(2, 8, 14, 1, acc);
    } else if (kind == "same_h") {
        bar(3, 3, 4, 12, fg);
        bar(11, 3, 4, 12, fg);
        bar(8, 2, 1, 14, acc);
    }
    qp.end();
    return pm;
}

QIcon makeIcon(const QString &kind)
{
    QIcon icon(iconPixmap(kind, th::TXT, th::ACC));
    icon.addPixmap(iconPixmap(kind, th::LINE2, th::DIM), QIcon::Disabled);
    return icon;
}

QLabel *styled(const QString &text, const char *name)
{
    auto *l = new QLabel(text);
    l->setObjectName(name);
    return l;
}

QString dot(const QColor &c, const QString &text)
{
    return QStringLiteral("<span style=\"color:%1\">■</span> %2").arg(c.name(), text.toHtmlEscaped());
}

QPushButton *tool(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setObjectName("armedTool");
    return b;
}

}

// ---- swatches

RoleSwatches::RoleSwatches(const QList<int> &roles, QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    for (int r : roles) {
        auto *b = new QPushButton(r == C_AUTO ? "A" : "");
        b->setObjectName("armedSwatch");
        b->setCheckable(true);
        b->setFixedSize(26, 22);
        b->setToolTip(roleTip(r));
        connect(b, &QPushButton::clicked, this, [this, r] { emit changed(r); });
        lay->addWidget(b);
        buttons_.insert(r, b);
    }
    lay->addStretch(1);
}

void RoleSwatches::setPalette(const QList<Hsv> &palette)
{
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it) {
        if (it.key() == C_AUTO)
            continue;
        const Rgb c = it.key() < palette.size() ? panelRgb(palette[it.key()]) : Rgb{0, 0, 0};
        it.value()->setStyleSheet(QStringLiteral("background: rgb(%1,%2,%3);").arg(c[0]).arg(c[1]).arg(c[2]));
    }
}

void RoleSwatches::setCurrent(std::optional<int> role)
{
    for (auto it = buttons_.begin(); it != buttons_.end(); ++it)
        it.value()->setChecked(role && it.key() == *role);
}

// ---- preview sources

namespace {

Value hostValue(const std::optional<std::pair<double, int>> &v)
{
    if (!v)
        return Value{.hint = C_LINE, .valid = false};
    return Value{.num = std::llround(v->first * std::pow(10, v->second)), .dec = v->second};
}

}

std::optional<Value> PreviewSources::value(int s) const
{
    if (s >= S_HOST && s < S_HOST + agent::HOST_VALUES)
        return hostValue(agent_.values.value(s - S_HOST));
    if (s >= S_HOST_TXT && s < S_HOST_TXT + agent::HOST_TEXTS) {
        const auto &t = agent_.texts.value(s - S_HOST_TXT);
        if (!t)
            return Value{.hint = C_LINE, .valid = false, .text = "--"};
        return Value{.text = t->section('\n', 0, 0).left(23)};
    }
    auto it = kb.constFind(s);
    if (it == kb.constEnd())
        return std::nullopt;
    return *it;
}

std::optional<QString> PreviewSources::hostText(int slot) const
{
    return agent_.texts.value(slot);
}

QList<Value> PreviewSources::history(int s) const
{
    if (s >= S_HOST && s < S_HOST + agent::HOST_VALUES) {
        QList<Value> out;
        for (const auto &v : agent_.history.value(s - S_HOST))
            out.append(hostValue(v));
        return out;
    }
    return kbHistory.value(s);
}

// ---- page

HudEditorPage::HudEditorPage(Controller *ctl)
    : Page(ctl)
    , host_(AgentHost::instance(ctl))
    , cfg_(agent::Config::load())
    , preview_(host_->agent())
{
    connect(host_, &AgentHost::changed, this, &HudEditorPage::refreshAgent);

    pushTimer_.setSingleShot(true);
    connect(&pushTimer_, &QTimer::timeout, this, &HudEditorPage::push);
    connect(&pollTimer_, &QTimer::timeout, this, &HudEditorPage::poll);
    connect(&paintTimer_, &QTimer::timeout, this, &HudEditorPage::redraw);
    cfgTimer_.setSingleShot(true);
    connect(&cfgTimer_, &QTimer::timeout, this, &HudEditorPage::saveConfig);

    // top bar
    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    view_ = new Segmented({tr("LAYOUT"), tr("AGENT")});
    view_->setCurrent(0);
    top->addWidget(view_);
    top->addSpacing(8);
    state_ = new QLabel;
    top->addWidget(state_);
    error_ = styled({}, "armedHint");
    top->addWidget(error_, 1);
    undoBtn_ = new QPushButton(tr("UNDO"));
    undoBtn_->setToolTip("Ctrl+Z");
    redoBtn_ = new QPushButton(tr("REDO"));
    redoBtn_->setToolTip("Ctrl+Shift+Z");
    presetsBtn_ = new QPushButton(tr("START FROM ▾"));
    auto *menu = new QMenu(presetsBtn_);
    const auto &presets = hud::presets();
    for (int i = 0; i < presets.size(); ++i)
        menu->addAction(presets[i].name, this, [this, i] { onPreset(i); });
    presetsBtn_->setMenu(menu);
    importBtn_ = new QPushButton(tr("IMPORT"));
    exportBtn_ = new QPushButton(tr("EXPORT"));
    revertBtn_ = new QPushButton(tr("REVERT"));
    revertBtn_->setToolTip(tr("Back to the layout stored on the keyboard"));
    saveBtn_ = new QPushButton(tr("SAVE"));
    saveBtn_->setToolTip(tr("Store on the keyboard (Ctrl+S)"));
    saveBtn_->setDefault(true);
    connect(undoBtn_, &QPushButton::clicked, this, &HudEditorPage::onUndo);
    connect(redoBtn_, &QPushButton::clicked, this, &HudEditorPage::onRedo);
    connect(importBtn_, &QPushButton::clicked, this, &HudEditorPage::onImport);
    connect(exportBtn_, &QPushButton::clicked, this, &HudEditorPage::onExport);
    connect(revertBtn_, &QPushButton::clicked, this, &HudEditorPage::onRevert);
    connect(saveBtn_, &QPushButton::clicked, this, &HudEditorPage::onSave);
    top->addWidget(undoBtn_);
    top->addWidget(redoBtn_);
    top->addSpacing(10);
    top->addWidget(presetsBtn_);
    top->addWidget(importBtn_);
    top->addWidget(exportBtn_);
    top->addSpacing(10);
    top->addWidget(revertBtn_);
    top->addWidget(saveBtn_);

    stack_ = new QStackedWidget;
    stack_->addWidget(buildLayoutView());
    stack_->addWidget(buildAgentView());
    connect(view_, &Segmented::changed, this, [this](int i) {
        stack_->setCurrentIndex(i);
        if (i == 0)
            QTimer::singleShot(0, this, &HudEditorPage::fitCanvas);
    });

    // the wheel scrolls the page; it changes a combo box or a number only after a click on it
    for (QWidget *w : stack_->findChildren<QWidget *>()) {
        if (qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(this);
        }
    }

    for (const auto &[seq, fn] : {std::pair{QKeySequence(QKeySequence::Undo), &HudEditorPage::onUndo},
                                  std::pair{QKeySequence(QKeySequence::Redo), &HudEditorPage::onRedo},
                                  std::pair{QKeySequence("Ctrl+Y"), &HudEditorPage::onRedo},
                                  std::pair{QKeySequence(QKeySequence::Save), &HudEditorPage::onSave}}) {
        auto *sc = new QShortcut(seq, this);
        sc->setEnabled(false);
        connect(sc, &QShortcut::activated, this, fn);
        shortcuts_.append(sc);
    }

    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, th::S2, 0, 0);
    l->addLayout(top);
    l->addSpacing(10);
    l->addWidget(stack_, 1);
    loadAgentRows();
    refreshAll();
    refreshAgent();
}

QWidget *HudEditorPage::buildLayoutView()
{
    screenFrame_ = new BracketFrame(tr("SCREEN"), true);
    screenLbl_ = styled({}, "armedHint");
    screenFrame_->header()->insertWidget(1, screenLbl_);
    zoom_ = new Segmented({"FIT", "1×", "2×", "3×", "4×"});
    zoom_->setCurrent(0);
    connect(zoom_, &Segmented::changed, this, &HudEditorPage::fitCanvas);
    screenFrame_->header()->addWidget(zoom_);

    auto *add = new QHBoxLayout;
    add->setSpacing(4);
    auto *lbl = styled(tr("ADD"), "armedKey");
    lbl->setFixedWidth(64);
    add->addWidget(lbl);
    for (int t : kTypes) {
        auto *b = tool(typeName(t));
        b->setToolTip(typeTip(t));
        connect(b, &QPushButton::clicked, this, [this, t] { onAdd(t); });
        add->addWidget(b);
        addButtons_.append(b);
    }
    add->addStretch(1);
    screenFrame_->box()->addLayout(add);

    auto *arrange = new QHBoxLayout;
    arrange->setSpacing(4);
    lbl = styled(tr("ARRANGE"), "armedKey");
    lbl->setFixedWidth(64);
    arrange->addWidget(lbl);
    struct Tool {
        const char *name;
        QString tip;
        std::function<void()> fn;
    };
    const QList<std::optional<Tool>> tools = {
        Tool{"align_left", tr("Align left edges"), [this] { align("left"); }},
        Tool{"align_hcenter", tr("Align centres horizontally"), [this] { align("hcenter"); }},
        Tool{"align_right", tr("Align right edges"), [this] { align("right"); }},
        std::nullopt,
        Tool{"align_top", tr("Align top edges"), [this] { align("top"); }},
        Tool{"align_vcenter", tr("Align middles vertically"), [this] { align("vcenter"); }},
        Tool{"align_bottom", tr("Align bottom edges"), [this] { align("bottom"); }},
        std::nullopt,
        Tool{"dist_h", tr("Distribute horizontally: equal gaps (3 or more)"), [this] { distribute(true); }},
        Tool{"dist_v", tr("Distribute vertically: equal gaps (3 or more)"), [this] { distribute(false); }},
        std::nullopt,
        Tool{"same_w", tr("Same width as the last selected"), [this] { match(true); }},
        Tool{"same_h", tr("Same height as the last selected"), [this] { match(false); }},
    };
    for (const auto &t : tools) {
        if (!t) {
            arrange->addSpacing(10);
            continue;
        }
        auto *b = tool({});
        b->setIcon(makeIcon(t->name));
        b->setToolTip(t->tip + tr(" — one widget: to the screen; several: to their bounds"));
        connect(b, &QPushButton::clicked, this, t->fn);
        arrange->addWidget(b);
        arrangeButtons_.append({t->name, b});
    }
    arrange->addStretch(1);
    snap_ = new QCheckBox(tr("SNAP"));
    snap_->setChecked(true);
    snap_->setToolTip(tr("Snap to the screen and to other widgets while dragging; hold Alt to drag freely"));
    bounds_ = new QCheckBox(tr("BOUNDS"));
    bounds_->setToolTip(tr("Outline every widget"));
    arrange->addWidget(snap_);
    arrange->addWidget(bounds_);
    screenFrame_->box()->addLayout(arrange);

    canvas_ = new HudCanvas;
    connect(snap_, &QCheckBox::toggled, this, [this](bool on) { canvas_->snap = on; });
    connect(bounds_, &QCheckBox::toggled, this, [this](bool on) {
        canvas_->showBounds = on;
        canvas_->update();
    });
    connect(canvas_, &HudCanvas::selectionChanged, this, [this] { setSelection(canvas_->selection(), true); });
    connect(canvas_, &HudCanvas::editStarted, this, &HudEditorPage::snapshot);
    connect(canvas_, &HudCanvas::edited, this, [this] { changed(true); });
    connect(canvas_, &HudCanvas::nudge, this, &HudEditorPage::onNudge);
    connect(canvas_, &HudCanvas::contextRequested, this, &HudEditorPage::onContext);
    connect(canvas_, &HudCanvas::cursorMoved, this, [this](int x, int y) {
        cursor_ = {x, y};
        refreshStatus();
    });
    connect(canvas_, &HudCanvas::activated, this, &HudEditorPage::onActivate);
    canvasScroll_ = new QScrollArea;
    canvasScroll_->setAlignment(Qt::AlignCenter);
    canvasScroll_->setWidget(canvas_);
    canvasScroll_->setMinimumHeight(200);
    canvasScroll_->viewport()->installEventFilter(this);
    screenFrame_->box()->addWidget(canvasScroll_, 1);

    status_ = styled({}, "armedHint");
    status_->setWordWrap(true);
    screenFrame_->box()->addWidget(status_);

    // widget list
    auto *listFrame = new BracketFrame(tr("WIDGETS"));
    count_ = styled({}, "armedHint");
    listFrame->header()->insertWidget(1, count_);
    list_ = new QListWidget;
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setToolTip(tr("The top one is drawn last, over the others. Drag to change the order"));
    connect(list_, &QListWidget::itemSelectionChanged, this, &HudEditorPage::onListSelection);
    auto later = [this] {
        if (!updating_)
            QTimer::singleShot(0, this, &HudEditorPage::onRowsMoved);
    };
    connect(list_->model(), &QAbstractItemModel::rowsMoved, this, later);
    connect(list_->model(), &QAbstractItemModel::rowsInserted, this, later);
    connect(list_->model(), &QAbstractItemModel::rowsRemoved, this, later);
    listFrame->box()->addWidget(list_, 1);
    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    dupBtn_ = new QPushButton(tr("DUPLICATE"));
    dupBtn_->setToolTip("Ctrl+D");
    upBtn_ = new QPushButton("▲");
    upBtn_->setToolTip(tr("Bring forward (Ctrl+])"));
    downBtn_ = new QPushButton("▼");
    downBtn_->setToolTip(tr("Send backward (Ctrl+[)"));
    delBtn_ = new QPushButton(tr("DELETE"));
    delBtn_->setToolTip("Del");
    connect(dupBtn_, &QPushButton::clicked, this, &HudEditorPage::onDuplicate);
    connect(upBtn_, &QPushButton::clicked, this, [this] { onOrder(1); });
    connect(downBtn_, &QPushButton::clicked, this, [this] { onOrder(-1); });
    connect(delBtn_, &QPushButton::clicked, this, &HudEditorPage::onDelete);
    for (auto *b : {dupBtn_, upBtn_, downBtn_, delBtn_})
        row->addWidget(b);
    listFrame->box()->addLayout(row);

    props_ = new BracketFrame(tr("PROPERTIES"));
    buildProps();
    auto *propsScroll = new QScrollArea;
    propsScroll->setWidgetResizable(true);
    propsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *holder = new QWidget;
    auto *hl = new QVBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(props_);
    propsScroll->setWidget(holder);

    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setSpacing(14);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(listFrame, 2);
    rl->addWidget(propsScroll, 3);
    right->setFixedWidth(420);

    auto *view = new QWidget;
    auto *body = new QHBoxLayout(view);
    body->setSpacing(18);
    body->setContentsMargins(0, 0, 0, 0);
    body->addWidget(screenFrame_, 1);
    body->addWidget(right);

    const QList<std::pair<QKeySequence, std::function<void()>>> keys = {
        {QKeySequence("Ctrl+D"), [this] { onDuplicate(); }},
        {QKeySequence(QKeySequence::Copy), [this] { onCopy(); }},
        {QKeySequence(QKeySequence::Paste), [this] { onPaste(); }},
        {QKeySequence(QKeySequence::SelectAll), [this] { selectAll(); }},
        {QKeySequence(QKeySequence::Delete), [this] { onDelete(); }},
        {QKeySequence("Backspace"), [this] { onDelete(); }},
        {QKeySequence("Escape"), [this] { setSelection({}); }},
        {QKeySequence("Ctrl+]"), [this] { onOrder(1); }},
        {QKeySequence("Ctrl+["), [this] { onOrder(-1); }},
    };
    for (const auto &[seq, fn] : keys) {
        for (QWidget *w : {static_cast<QWidget *>(canvas_), static_cast<QWidget *>(list_)}) {
            auto *sc = new QShortcut(seq, w);
            sc->setContext(Qt::WidgetShortcut);
            connect(sc, &QShortcut::activated, this, fn);
        }
    }
    return view;
}

void HudEditorPage::buildProps()
{
    QVBoxLayout *box = props_->box();
    selLbl_ = styled({}, "armedHint");
    selLbl_->setWordWrap(true);
    box->addWidget(selLbl_);

    auto section = [&](const QString &title) {
        auto *lbl = styled(title, "armedSection");
        box->addWidget(lbl);
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(7);
        grid->setColumnStretch(1, 1);
        box->addLayout(grid);
        sections_.append(std::pair{lbl, QStringList()});
        return grid;
    };
    auto row = [&](QGridLayout *grid, const QString &key, const QString &label, QWidget *w) {
        const int r = grid->rowCount();
        auto *lbl = styled(label, "armedKey");
        lbl->setFixedWidth(92);
        grid->addWidget(lbl, r, 0);
        grid->addWidget(w, r, 1);
        rows_.insert(key, {lbl, w});
        sections_.last().second.append(key);
    };
    auto hbox = [](std::initializer_list<QWidget *> ws, int spacing = 6) {
        auto *holder = new QWidget;
        auto *l = new QHBoxLayout(holder);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(spacing);
        for (QWidget *w : ws)
            l->addWidget(w, qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QLineEdit *>(w) ? 1 : 0);
        return holder;
    };

    QGridLayout *g = section(tr("CONTENT"));
    typeCombo_ = new QComboBox;
    for (int t : kTypes)
        typeCombo_->addItem(typeName(t), t);
    connect(typeCombo_, &QComboBox::currentIndexChanged, this, &HudEditorPage::onType);
    row(g, "type", tr("TYPE"), typeCombo_);
    auto *src = new QWidget;
    auto *sl = new QVBoxLayout(src);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(3);
    sourceCombo_ = new QComboBox;
    connect(sourceCombo_, &QComboBox::currentIndexChanged, this, &HudEditorPage::onSource);
    nowLbl_ = styled({}, "armedHint");
    sl->addWidget(sourceCombo_);
    sl->addWidget(nowLbl_);
    row(g, "source", tr("SOURCE"), src);
    labelEdit_ = new QLineEdit;
    labelEdit_->setMaxLength(46);
    connect(labelEdit_, &QLineEdit::textEdited, this, [this](const QString &t) {
        edit([t](Widget &w) { w.label = t; });
    });
    valueBtn_ = tool("{}");
    valueBtn_->setToolTip(tr("Insert the value"));
    connect(valueBtn_, &QPushButton::clicked, this, &HudEditorPage::insertValue);
    row(g, "label", tr("TEXT"), hbox({labelEdit_, valueBtn_}, 4));
    digits_ = new QSpinBox;
    digits_->setRange(0, 9);
    digits_->setSpecialValueText(tr("AS IS"));
    digits_->setSuffix(tr(" DIGITS"));
    digits_->setToolTip(tr("Pad numbers with zeros: 3 shows 7 as 007"));
    connect(digits_, &QSpinBox::valueChanged, this, [this](int v) { edit([v](Widget &w) { w.param = v; }); });
    row(g, "digits", tr("PAD"), digits_);

    g = section(tr("LOOK"));
    fontCombo_ = new QComboBox;
    connect(fontCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0)
            edit([i](Widget &w) { w.font = i; });
    });
    row(g, "font", tr("FONT"), fontCombo_);
    fg_ = new RoleSwatches({C_AUTO, C_TEXT, C_ACCENT, C_DIM, C_LINE, C_OK, C_WARN, C_PANEL});
    connect(fg_, &RoleSwatches::changed, this, [this](int r) { edit([r](Widget &w) { w.fg = r; }); });
    row(g, "fg", tr("COLOUR"), fg_);
    bg_ = new RoleSwatches({C_BG, C_PANEL, C_LINE, C_DIM, C_ACCENT});
    connect(bg_, &RoleSwatches::changed, this, [this](int r) { edit([r](Widget &w) { w.bg = r; }); });
    row(g, "bg", tr("BACKGROUND"), bg_);
    alignSeg_ = new Segmented({alignName(0), alignName(1), alignName(2)});
    connect(alignSeg_, &Segmented::changed, this, [this](int i) { editFlags(F_ALIGN, i); });
    row(g, "align", tr("ALIGN TEXT"), hbox({alignSeg_}));
    dot_ = new QCheckBox(tr("DOT ONLY"));
    connect(dot_, &QCheckBox::toggled, this, [this](bool on) { editFlags(F_DOT, on ? F_DOT : 0); });
    row(g, "dot", tr("STYLE"), dot_);
    frameStyle_ = new QComboBox;
    for (int i = FRAME_BRACKETS; i <= FRAME_PANEL; ++i)
        frameStyle_->addItem(frameName(i), i);
    connect(frameStyle_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0)
            edit([i](Widget &w) { w.param = i; });
    });
    corner_ = new QSpinBox;
    corner_->setRange(0, 60);
    corner_->setSpecialValueText(tr("CORNER 6 PX"));
    corner_->setPrefix(tr("CORNER "));
    corner_->setSuffix(" PX");
    connect(corner_, &QSpinBox::valueChanged, this, [this](int v) { edit([v](Widget &w) { w.lo = v; }); });
    row(g, "frame", tr("SHAPE"), hbox({frameStyle_, corner_}));
    segments_ = new QSpinBox;
    segments_->setRange(0, 40);
    segments_->setSpecialValueText(tr("SOLID"));
    segments_->setSuffix(tr(" SEGMENTS"));
    connect(segments_, &QSpinBox::valueChanged, this, [this](int v) {
        edit([v](Widget &w) { w.param = v != 1 ? v : 2; });
    });
    row(g, "segments", tr("FILL"), segments_);
    period_ = new QDoubleSpinBox;
    period_->setRange(0.1, 25.5);
    period_->setSingleStep(0.5);
    period_->setDecimals(1);
    period_->setPrefix(tr("EVERY "));
    period_->setSuffix(" S");
    connect(period_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit([p = int(std::lround(v * 10))](Widget &w) { w.param = p; });
    });
    fill_ = new QCheckBox(tr("COLUMNS"));
    fill_->setToolTip(tr("Filled columns instead of a line"));
    connect(fill_, &QCheckBox::toggled, this, [this](bool on) { editFlags(F_FILL, on ? F_FILL : 0); });
    row(g, "period", tr("SAMPLE"), hbox({period_, fill_}));
    gap_ = new QSpinBox;
    gap_->setRange(0, 20);
    gap_->setPrefix(tr("GAP "));
    gap_->setSuffix(" PX");
    connect(gap_, &QSpinBox::valueChanged, this, [this](int v) { edit([v](Widget &w) { w.param = v; }); });
    tail_ = new QCheckBox(tr("SHOW THE END"));
    tail_->setToolTip(tr("When the text doesn't fit, show its last lines, like a terminal"));
    connect(tail_, &QCheckBox::toggled, this, [this](bool on) { editFlags(F_TAIL, on ? F_TAIL : 0); });
    row(g, "lines", tr("LINES"), hbox({gap_, tail_}));

    g = section(tr("POSITION AND SIZE"));
    auto *geo = new QWidget;
    auto *gl = new QGridLayout(geo);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setHorizontalSpacing(6);
    gl->setVerticalSpacing(5);
    const std::pair<const char *, int Widget::*> fields[] = {{"x", &Widget::x}, {"y", &Widget::y},
                                                              {"w", &Widget::w}, {"h", &Widget::h}};
    for (int n = 0; n < 4; ++n) {
        auto *sp = new QSpinBox;
        sp->setRange(0, 999);
        sp->setPrefix(QString(fields[n].first).toUpper() + "  ");
        sp->setSuffix(" PX");
        connect(sp, &QSpinBox::valueChanged, this, [this, f = fields[n].second](int v) {
            edit([f, v](Widget &w) { w.*f = v; });
        });
        spin_.insert(fields[n].first, sp);
        gl->addWidget(sp, n / 2, n % 2);
    }
    spin_["w"]->setSpecialValueText(tr("W  TO EDGE"));
    spin_["h"]->setSpecialValueText(tr("H  ONE LINE"));
    row(g, "geo", {}, geo);

    g = section(tr("RANGE AND ALERT"));
    lo_ = new QDoubleSpinBox;
    hi_ = new QDoubleSpinBox;
    for (auto [sp, f] : {std::pair{lo_, &Widget::lo}, std::pair{hi_, &Widget::hi}}) {
        sp->setRange(-32768, 32767);
        connect(sp, &QDoubleSpinBox::valueChanged, this, [this, f](double v) { editScaled(f, v); });
    }
    row(g, "range", tr("FROM … TO"), hbox({lo_, new QLabel("…"), hi_}));
    warnChk_ = new QCheckBox(tr("RED FROM"));
    warnChk_->setToolTip(tr("Switch to the WARN colour at this value"));
    connect(warnChk_, &QCheckBox::toggled, this, [this](bool on) { editFlags(F_WARN, on ? F_WARN : 0); });
    warn_ = new QDoubleSpinBox;
    warn_->setRange(-32768, 32767);
    connect(warn_, &QDoubleSpinBox::valueChanged, this, [this](double v) { editScaled(&Widget::warn, v); });
    below_ = new QComboBox;
    below_->addItem(tr("AND UP"), 0);
    below_->addItem(tr("AND DOWN"), int(F_WARN_BELOW));
    connect(below_, &QComboBox::currentIndexChanged, this, [this](int i) {
        editFlags(F_WARN_BELOW, below_->itemData(i).toInt());
    });
    row(g, "warn", tr("ALERT"), hbox({warnChk_, warn_, below_}));

    hintLbl_ = styled({}, "armedHint");
    hintLbl_->setWordWrap(true);
    box->addSpacing(6);
    box->addWidget(hintLbl_);
    box->addStretch(1);
}

QWidget *HudEditorPage::buildAgentView()
{
    auto *frame = new BracketFrame(tr("AGENT"), true);
    agentLbl_ = new QLabel;
    frame->header()->insertWidget(1, agentLbl_);
    autostart_ = new QCheckBox(tr("RUN AT LOGIN WITHOUT THE WINDOW"));
    autostart_->setToolTip(tr("Adds `vial-armed --agent` to your desktop's autostart"));
    connect(autostart_, &QCheckBox::toggled, this, &HudEditorPage::onAutostart);
    frame->header()->addWidget(autostart_);
#if !defined(Q_OS_LINUX) && !defined(Q_OS_WIN)
    autostart_->setVisible(false);
#endif
    auto *note = styled(tr("What this computer sends to the HUD while Vial Armed runs: pick these as sources "
                           "(H1–H16, T1–T4) in the LAYOUT view. Over USB and the 2.4 GHz dongle; Bluetooth has "
                           "no data channel. Commands run here as you, through the shell, and are kept only on "
                           "this computer. A number takes the first number the command prints, a text its "
                           "whole output."), "armedHint");
    note->setWordWrap(true);
    frame->box()->addWidget(note);

    auto header = [](QGridLayout *grid, const QStringList &titles) {
        for (int c = 0; c < titles.size(); ++c)
            grid->addWidget(styled(titles[c], "armedSection"), 0, c);
    };

    auto *values = new QGridLayout;
    values->setHorizontalSpacing(8);
    values->setVerticalSpacing(5);
    header(values, {"", tr("NUMBER"), tr("COMMAND"), tr("EVERY"), tr("DECIMALS"), tr("NOW"), tr("ON SCREEN")});
    for (int i = 0; i < agent::HOST_VALUES; ++i) {
        ValueRow r;
        r.metric = new QComboBox;
        r.metric->addItem("—", QString());
        for (const auto &m : agent::metrics())
            r.metric->addItem(m.name, m.key);
        r.command = new QLineEdit;
        r.command->setPlaceholderText(tr("pick COMMAND to run your own"));
        r.interval = new QDoubleSpinBox;
        r.interval->setRange(0.5, 3600);
        r.interval->setDecimals(1);
        r.interval->setSuffix(" S");
        r.dec = new QSpinBox;
        r.dec->setRange(0, 4);
        r.live = new QLabel;
        r.live->setMinimumWidth(80);
        r.used = styled({}, "armedHint");
        const QList<QWidget *> ws = {styled(QStringLiteral("H%1").arg(i + 1), "armedKey"), r.metric, r.command,
                                     r.interval, r.dec, r.live, r.used};
        for (int k = 0; k < ws.size(); ++k)
            values->addWidget(ws[k], i + 1, k);
        connect(r.metric, &QComboBox::currentIndexChanged, this, [this, i] { onValueCfg(i, "metric"); });
        connect(r.command, &QLineEdit::textEdited, this, [this, i] { onValueCfg(i, "command"); });
        connect(r.interval, &QDoubleSpinBox::valueChanged, this, [this, i] { onValueCfg(i, "interval"); });
        connect(r.dec, &QSpinBox::valueChanged, this, [this, i] { onValueCfg(i, "dec"); });
        valueRows_.append(r);
    }
    values->setColumnStretch(2, 1);
    frame->box()->addLayout(values);
    frame->box()->addSpacing(10);

    auto *texts = new QGridLayout;
    texts->setHorizontalSpacing(8);
    texts->setVerticalSpacing(5);
    header(texts, {"", tr("TEXT FROM A COMMAND"), "", tr("EVERY"), tr("OUTPUT"), tr("ON SCREEN")});
    for (int i = 0; i < agent::HOST_TEXTS; ++i) {
        TextRow r;
        r.command = new QLineEdit;
        r.command->setPlaceholderText(tr("e.g. uptime -p"));
        auto *ex = new QPushButton(tr("EXAMPLES ▾"));
        auto *m = new QMenu(ex);
        for (const auto &[name, example] : agent::kTextExamples)
            m->addAction(name, this, [this, i, example] {
                textRows_[i].command->setText(example);
                onTextCfg(i);
            });
        ex->setMenu(m);
        r.interval = new QDoubleSpinBox;
        r.interval->setRange(0.5, 3600);
        r.interval->setDecimals(1);
        r.interval->setSuffix(" S");
        r.live = new QLabel;
        r.live->setMinimumWidth(240);
        r.used = styled({}, "armedHint");
        const QList<QWidget *> ws = {styled(QStringLiteral("T%1").arg(i + 1), "armedKey"), r.command, ex,
                                     r.interval, r.live, r.used};
        for (int k = 0; k < ws.size(); ++k)
            texts->addWidget(ws[k], i + 1, k);
        connect(r.command, &QLineEdit::textEdited, this, [this, i] { onTextCfg(i); });
        connect(r.interval, &QDoubleSpinBox::valueChanged, this, [this, i] { onTextCfg(i); });
        textRows_.append(r);
    }
    texts->setColumnStretch(1, 2);
    texts->setColumnStretch(4, 1);
    frame->box()->addLayout(texts);
    frame->box()->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *holder = new QWidget;
    auto *hl = new QVBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(frame);
    scroll->setWidget(holder);
    return scroll;
}

bool HudEditorPage::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == canvasScroll_->viewport()) {
        if (ev->type() == QEvent::Resize)
            QTimer::singleShot(0, this, &HudEditorPage::fitCanvas);
        return false;
    }
    if (ev->type() != QEvent::Wheel)
        return false;
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || w->hasFocus())
        return false;
    for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(p)) {
            QApplication::sendEvent(area->verticalScrollBar(), ev);
            break;
        }
    }
    return true;
}

// ---- device

bool HudEditorPage::valid() const
{
    const KeyboardState *kb = ctl->keyboard();
    return kb && kb->armed && kb->armed->has(armed::F_HUD) && ctl->session();
}

void HudEditorPage::keyboardChanged()
{
    ++serial_;
    loaded_ = loading_ = fontsBusy_ = polling_ = false;
    shown_ = std::make_shared<std::optional<QByteArray>>();
    info_.reset();
    fonts_.clear();
    preview_.kb.clear();
    preview_.kbHistory.clear();
    hud_ = {};
    saved_.reset();
    sel_.clear();
    undo_.clear();
    redo_.clear();
    pushTimer_.stop();
    refreshAll();
    if (active_)
        load();
}

void HudEditorPage::activate()
{
    active_ = true;
    if (!valid())
        return;
    if (!loaded_) {
        load();
    } else if (info_) {
        ctl->session()->run(hid::Priority::Poll, this, [info = *info_](hid::Link &link) {
            return armed::hudPalette(link, info);
        }, [this, serial = serial_](hid::Result<QList<std::array<quint8, 3>>> r) {
            if (serial != serial_ || !r.ok())
                return;
            palette_.clear();
            for (const auto &c : *r.value)
                palette_.append(Hsv{c[0], c[1], c[2]});
            fg_->setPalette(palette_);
            bg_->setPalette(palette_);
        });
    }
    pollTimer_.start(POLL_MS);
    paintTimer_.start(500);
    for (auto *sc : shortcuts_)
        sc->setEnabled(true);
    QTimer::singleShot(0, this, &HudEditorPage::fitCanvas);
}

void HudEditorPage::deactivate()
{
    active_ = false;
    pollTimer_.stop();
    paintTimer_.stop();
    for (auto *sc : shortcuts_)
        sc->setEnabled(false);
    if (loaded_ && pushTimer_.isActive()) {
        pushTimer_.stop();
        push();
    }
}

void HudEditorPage::showError(const QString &e)
{
    error_->setText(e.isEmpty() ? QString() : tr("ERROR: %1").arg(e));
}

void HudEditorPage::load()
{
    if (loading_ || !valid())
        return;
    loading_ = true;
    error_->setText(tr("READING THE HUD…"));
    const KeyboardState *kb = ctl->keyboard();
    armed::ListCache cache = kb->keyboardId ? armed::ListCache(paths::configDir(), *kb->keyboardId,
                                                               kb->armed->versionString(), kb->armed->features)
                                            : armed::ListCache();
    struct Loaded {
        armed::HudInfo info;
        QList<armed::HudSource> sources;
        QList<std::array<quint8, 3>> palette;
        QByteArray blob;
        QList<QByteArray> heads;
    };
    ctl->session()->run(hid::Priority::Interactive, this, [cache, shown = shown_](hid::Link &link) mutable {
        auto info = armed::hudInfo(link);
        if (!info)
            throw std::runtime_error("the keyboard has no HUD");
        Loaded out{*info, armed::hudSources(link, *info, cache), armed::hudPalette(link, *info),
                   armed::hudRead(link, *info), {}};
        *shown = out.blob;
        for (int i = 0; i < info->fonts; ++i)
            out.heads.append(armed::hudFontHead(link, i));
        return out;
    }, [this, serial = serial_](hid::Result<Loaded> r) {
        if (serial != serial_)
            return;
        loading_ = false;
        if (!r.ok()) {
            showError(r.error);
            return;
        }
        info_ = r.value->info;
        sources_ = r.value->sources;
        palette_.clear();
        for (const auto &c : r.value->palette)
            palette_.append(Hsv{c[0], c[1], c[2]});
        auto layout = Layout::unpack(r.value->blob, {info_->maxWidgets, info_->strings});
        if (!layout) {
            showError(tr("the keyboard's layout is not readable"));
            return;
        }
        hud_ = *layout;
        saved_ = hud_;
        undo_.clear();
        redo_.clear();
        sel_.clear();
        loaded_ = true;
        error_->clear();
        canvas_->setScreen(info_->width, info_->height);
        screenLbl_->setText(QStringLiteral("%1 × %2 PX").arg(info_->width).arg(info_->height));
        fg_->setPalette(palette_);
        bg_->setPalette(palette_);
        fontTries_ = 0;
        loadFonts(r.value->heads);
        refreshAll();
        QTimer::singleShot(0, this, &HudEditorPage::fitCanvas);
    });
}

QString HudEditorPage::fontCache(const QByteArray &head) const
{
    const QString version = ctl->keyboard() && ctl->keyboard()->armed ? ctl->keyboard()->armed->versionString() : QString();
    const QByteArray key = QCryptographicHash::hash(head + version.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return paths::configDir() + "/hud-fonts/" + QString::fromLatin1(key) + ".qff";
}

// the keyboard's fonts: from the cache, or read once (a few seconds over USB, longer over the dongle)
void HudEditorPage::loadFonts(const QList<QByteArray> &heads)
{
    if (fontsBusy_ || !valid())
        return;
    QList<QByteArray> fonts(heads.size());
    QList<int> missing;
    QStringList paths;
    for (int i = 0; i < heads.size(); ++i) {
        paths.append(fontCache(heads[i]));
        QFile f(paths[i]);
        if (f.open(QIODevice::ReadOnly))
            fonts[i] = f.readAll();
        else
            missing.append(i);
    }
    if (missing.isEmpty()) {
        setFonts(fonts);
        return;
    }
    fontsBusy_ = true;
    QPointer<HudEditorPage> self(this);
    const quint64 serial = serial_;
    auto report = [self, serial](int pct) {
        QMetaObject::invokeMethod(qApp, [self, serial, pct] {
            if (self && serial == self->serial_)
                self->error_->setText(tr("READING THE KEYBOARD'S FONTS %1%").arg(pct));
        }, Qt::QueuedConnection);
    };
    ctl->session()->run(hid::Priority::Interactive, this, [fonts, missing, paths, report](hid::Link &link) mutable {
        int last = -1;
        for (int k = 0; k < missing.size(); ++k) {
            const int i = missing[k];
            // per piece would flood the GUI thread
            fonts[i] = armed::hudFont(link, i, [&](qint64 d, qint64 t) {
                const int pct = int((k * 100 + d * 100 / std::max<qint64>(1, t)) / missing.size());
                if (pct != last) {
                    last = pct;
                    report(pct);
                }
            });
            QDir().mkpath(QFileInfo(paths[i]).absolutePath());
            QSaveFile f(paths[i]);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(fonts[i]);
                f.commit();
            }
        }
        return fonts;
    }, [this, serial, heads](hid::Result<QList<QByteArray>> r) {
        if (serial != serial_)
            return;
        fontsBusy_ = false;
        if (!r.ok()) {
            // a reply got lost: what was read is cached, try again
            if (++fontTries_ <= 3 && loaded_)
                QTimer::singleShot(0, this, [this, heads] { loadFonts(heads); });
            else
                showError(r.error);
            return;
        }
        error_->clear();
        setFonts(*r.value);
    });
}

void HudEditorPage::setFonts(const QList<QByteArray> &fonts)
{
    fonts_.clear();
    for (const auto &f : fonts)
        fonts_.append(QffFont::load(f));
    updating_ = true;
    fontCombo_->clear();
    for (const auto &f : fonts_)
        fontCombo_->addItem(f ? QStringLiteral("%1 PX").arg(f->lineHeight()) : QStringLiteral("? PX"));
    updating_ = false;
    refreshAll();
}

// ---- rendering

void HudEditorPage::redraw()
{
    if (!loaded_ || !info_)
        return;
    Renderer r(info_->width, info_->height, fonts_, palette_.isEmpty() ? DEFAULT_PALETTE : palette_, preview_);
    for (const auto &s : sources_)
        r.names.insert(s.id, s.name);
    const QImage image = r.render(hud_);
    areas_.clear();
    for (const auto &w : hud_.widgets)
        areas_.append(r.area(w));
    canvas_->setSelection(sel_);
    canvas_->setContent(image, &hud_, areas_);
    refreshLive();
}

void HudEditorPage::refreshLive()
{
    agent::HudAgent &a = host_->agent();
    for (int i = 0; i < valueRows_.size(); ++i) {
        const auto &v = a.values.value(i);
        valueRows_[i].live->setText(v ? QString::number(v->first, 'f', v->second) : QStringLiteral("—"));
    }
    for (int i = 0; i < textRows_.size(); ++i) {
        const auto &t = a.texts.value(i);
        const QString err = a.textRunners.value(i) ? a.textRunners[i]->error : QString();
        QString shown = !err.isEmpty() ? err.toUpper() : !t ? QStringLiteral("—") : t->section('\n', 0, 0).left(48);
        textRows_[i].live->setText(shown.isEmpty() ? QStringLiteral(" ") : shown);
    }
    const Widget *wd = current();
    if (wd && wd->source && sel_.size() == 1) {
        auto v = preview_.value(wd->source);
        nowLbl_->setText(tr("NOW: ") + (v ? valueText(*v, 0) : tr("not read yet")));
    }
    refreshAgent();
}

// keyboard sources the layout uses, one per tick
void HudEditorPage::poll()
{
    if (!loaded_ || polling_ || fontsBusy_ || !valid())
        return;
    QList<int> used;
    bool battery = false;
    for (const auto &w : hud_.widgets) {
        if (w.source > 0 && w.source < S_HOST && !used.contains(w.source))
            used.append(w.source);
        battery |= w.type == W_BATTERY;
    }
    if (battery && !used.contains(S_CHARGING))
        used.append(S_CHARGING);
    if (used.isEmpty())
        return;
    std::sort(used.begin(), used.end());
    pollIdx_ = (pollIdx_ + 1) % int(used.size());
    const int s = used[pollIdx_];
    polling_ = true;
    ctl->session()->run(hid::Priority::Poll, this, [s](hid::Link &link) { return armed::hudValue(link, s); },
                        [this, s, serial = serial_](hid::Result<std::optional<armed::HudValue>> r) {
        if (serial != serial_)
            return;
        polling_ = false;
        if (!r.ok())
            return;
        if (!*r.value) {
            preview_.kb.remove(s);
            return;
        }
        const armed::HudValue &hv = **r.value;
        const Value v{.num = hv.num, .dec = hv.dec, .hint = hv.hint, .valid = hv.valid, .text = hv.text};
        preview_.kb.insert(s, v);
        auto &hist = preview_.kbHistory[s];
        hist.append(v);
        if (hist.size() > GRAPH_POINTS)
            hist.remove(0, hist.size() - GRAPH_POINTS);
    }, QStringLiteral("hud-poll"));
}

void HudEditorPage::fitCanvas()
{
    if (!info_ || info_->width <= 0 || info_->height <= 0)
        return;
    const int z = zoom_->current();
    if (z > 0) {
        canvas_->setScale(z);
        return;
    }
    const QSize vp = canvasScroll_->viewport()->size();
    canvas_->setScale(std::max(1, std::min((vp.width() - 4) / info_->width, (vp.height() - 4) / info_->height)));
}

// ---- selection

const Widget *HudEditorPage::current() const
{
    const int i = sel_.isEmpty() ? -1 : sel_.last();
    return i >= 0 && i < hud_.widgets.size() ? &hud_.widgets[i] : nullptr;
}

QList<int> HudEditorPage::validSel() const
{
    QList<int> out;
    for (int i : sel_)
        if (i >= 0 && i < hud_.widgets.size())
            out.append(i);
    return out;
}

void HudEditorPage::setSelection(const QList<int> &sel, bool fromCanvas)
{
    sel_.clear();
    for (int i : sel)
        if (i >= 0 && i < hud_.widgets.size())
            sel_.append(i);
    if (!fromCanvas) {
        canvas_->setSelection(sel_);
        canvas_->update();
    }
    syncListSelection();
    refreshProps();
    refreshButtons();
    refreshStatus();
}

void HudEditorPage::selectAll()
{
    QList<int> all;
    for (int i = 0; i < hud_.widgets.size(); ++i)
        all.append(i);
    setSelection(all);
}

void HudEditorPage::syncListSelection()
{
    updating_ = true;
    list_->clearSelection();
    for (int i : sel_)
        if (auto *item = list_->item(rowOf(i)))
            item->setSelected(true);
    if (!sel_.isEmpty()) {
        list_->setCurrentRow(rowOf(sel_.last()), QItemSelectionModel::NoUpdate);
        list_->scrollToItem(list_->item(rowOf(sel_.last())));
    }
    updating_ = false;
}

void HudEditorPage::onListSelection()
{
    if (updating_)
        return;
    QList<int> rows;
    for (auto *it : list_->selectedItems())
        rows.append(list_->row(it));
    std::sort(rows.begin(), rows.end(), std::greater<>());
    QList<int> sel;
    for (int r : rows)
        sel.append(rowOf(r));
    const int cur = list_->currentRow();
    if (cur >= 0 && sel.contains(rowOf(cur))) {
        sel.removeAll(rowOf(cur));
        sel.append(rowOf(cur));
    }
    sel_ = sel;
    canvas_->setSelection(sel_);
    canvas_->update();
    refreshProps();
    refreshButtons();
    refreshStatus();
}

void HudEditorPage::reorder(const QList<int> &order)
{
    QList<Widget> ws;
    for (int i : order)
        ws.append(hud_.widgets[i]);
    hud_.widgets = ws;
    for (int &i : sel_)
        i = int(order.indexOf(i));
}

void HudEditorPage::onRowsMoved()
{
    if (updating_ || list_->count() != hud_.widgets.size())
        return; // mid-drop: the item is removed but not inserted yet
    QList<int> order;
    for (int r = list_->count() - 1; r >= 0; --r)
        order.append(list_->item(r)->data(Qt::UserRole).toInt());
    QList<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    bool identity = true;
    for (int i = 0; i < order.size(); ++i) {
        if (sorted[i] != i)
            return;
        identity &= order[i] == i;
    }
    if (identity)
        return;
    snapshot();
    reorder(order);
    changed();
}

void HudEditorPage::onActivate(int i)
{
    setSelection({i});
    if (i < hud_.widgets.size() && (hud_.widgets[i].type == W_TEXT || hud_.widgets[i].type == W_INDICATOR)) {
        labelEdit_->setFocus();
        labelEdit_->selectAll();
    }
}

// ---- editing

void HudEditorPage::snapshot()
{
    undo_.append(hud_);
    if (undo_.size() > UNDO_DEPTH)
        undo_.removeFirst();
    redo_.clear();
}

void HudEditorPage::changed(bool refreshProps)
{
    pushTimer_.start(PUSH_DELAY_MS);
    refreshList();
    if (refreshProps)
        this->refreshProps();
    refreshState();
    refreshButtons();
    redraw();
    refreshStatus();
}

void HudEditorPage::refreshAll()
{
    refreshList();
    refreshProps();
    refreshState();
    refreshButtons();
    redraw();
    refreshStatus();
}

void HudEditorPage::edit(const std::function<void(Widget &)> &fn, bool refreshProps)
{
    const QList<int> targets = validSel();
    if (targets.isEmpty() || updating_)
        return;
    const Layout before = hud_;
    for (int i : targets)
        fn(hud_.widgets[i]);
    if (hud_ == before)
        return;
    undo_.append(before);
    if (undo_.size() > UNDO_DEPTH)
        undo_.removeFirst();
    redo_.clear();
    changed(refreshProps);
}

void HudEditorPage::editFlags(int mask, int value)
{
    edit([mask, value](Widget &w) { w.flags = (w.flags & ~mask) | value; });
}

int HudEditorPage::sourceDec(const Widget &wd) const
{
    if (wd.source >= S_HOST && wd.source < S_HOST + agent::HOST_VALUES)
        return cfg_.values.value(wd.source - S_HOST).dec;
    for (const auto &s : sources_)
        if (s.id == wd.source)
            return s.dec.value_or(0);
    return 0;
}

void HudEditorPage::editScaled(int Widget::*field, double v)
{
    if (const Widget *wd = current()) {
        const int scaled = int(std::lround(v * std::pow(10, sourceDec(*wd))));
        edit([field, scaled](Widget &w) { w.*field = scaled; });
    }
}

void HudEditorPage::onType()
{
    const Widget *wd = current();
    const QVariant t = typeCombo_->currentData();
    if (updating_ || !wd || !t.isValid() || t.toInt() == wd->type)
        return;
    const int type = t.toInt();
    snapshot();
    Widget &w = hud_.widgets[sel_.last()];
    const Widget m = Widget::make(type, 0, 0);
    // a fresh widget's look, keeping the place, size, source and text
    w.type = type;
    w.fg = m.fg;
    if (type == W_TEXT)
        w.font = m.font;
    if (type == W_GRAPH || type == W_FRAME)
        w.param = m.param;
    if (type == W_TEXTBOX)
        w.flags = m.flags;
    changed();
}

void HudEditorPage::onSource()
{
    if (updating_)
        return;
    const QVariant d = sourceCombo_->currentData();
    const Widget *wd = current();
    if (!wd || !d.isValid() || d.toInt() == wd->source)
        return;
    const int s = d.toInt();
    std::optional<std::pair<int, int>> range;
    if (s >= S_HOST && s < S_HOST + agent::HOST_VALUES && (wd->type == W_BAR || wd->type == W_GRAPH) && wd->hi <= wd->lo) {
        const auto &slot = cfg_.values.value(s - S_HOST);
        if (const agent::Metric *m = agent::metric(slot.metric)) {
            const double k = std::pow(10, slot.dec);
            range = {int(m->lo * k), int(m->hi * k)};
        }
    }
    edit([s, range](Widget &w) {
        w.source = s;
        if (range) {
            w.lo = range->first;
            w.hi = range->second;
        }
    }, true);
}

void HudEditorPage::insertValue()
{
    if (!labelEdit_->text().contains("{}")) {
        labelEdit_->insert("{}");
        edit([t = labelEdit_->text()](Widget &w) { w.label = t; });
    }
    labelEdit_->setFocus();
}

QPoint HudEditorPage::freeSpot(int w, int h) const
{
    int x = (info_->width - w) / 2, y = (info_->height - h) / 2;
    QSet<std::pair<int, int>> taken;
    for (const auto &wd : hud_.widgets)
        taken.insert({wd.x, wd.y});
    while (taken.contains({x, y}) && x + 8 < info_->width && y + 8 < info_->height) {
        x += 8;
        y += 8;
    }
    return {std::max(0, x), std::max(0, y)};
}

void HudEditorPage::onAdd(int t)
{
    if (!info_ || hud_.widgets.size() >= info_->maxWidgets)
        return;
    snapshot();
    Widget wd = Widget::make(t, 0, 0);
    if (t == W_TEXT) {
        wd.source = S_LAYER;
    } else if (t == W_INDICATOR) {
        wd.source = sources_.isEmpty() ? S_NONE : sources_.first().id;
        wd.label.reset();
    } else if (t == W_BAR || t == W_GRAPH) {
        wd.source = S_HOST;
        const auto &slot = cfg_.values.value(0);
        if (const agent::Metric *m = agent::metric(slot.metric)) {
            const double k = std::pow(10, slot.dec);
            wd.lo = int(m->lo * k);
            wd.hi = int(m->hi * k);
        }
    } else if (t == W_TEXTBOX) {
        wd.source = S_HOST_TXT;
    }
    const auto f = wd.font < fonts_.size() ? fonts_[wd.font] : nullptr;
    const QPoint p = freeSpot(wd.w ? wd.w : 80, wd.h ? wd.h : (f ? f->lineHeight() : 14));
    wd.x = p.x();
    wd.y = p.y();
    hud_.widgets.append(wd);
    sel_ = {int(hud_.widgets.size()) - 1};
    changed();
    syncListSelection();
    canvas_->setFocus();
}

void HudEditorPage::onDuplicate()
{
    if (!info_ || sel_.isEmpty() || hud_.widgets.size() + sel_.size() > info_->maxWidgets)
        return;
    snapshot();
    QList<int> sorted = validSel(), added;
    std::sort(sorted.begin(), sorted.end());
    for (int i : sorted) {
        Widget c = hud_.widgets[i];
        c.x = std::min(info_->width - 1, c.x + 6);
        c.y = std::min(info_->height - 1, c.y + 6);
        hud_.widgets.append(c);
        added.append(int(hud_.widgets.size()) - 1);
    }
    sel_ = added;
    changed();
    syncListSelection();
}

void HudEditorPage::onCopy()
{
    QList<int> sorted = validSel();
    if (sorted.isEmpty())
        return;
    std::sort(sorted.begin(), sorted.end());
    Layout part;
    for (int i : sorted)
        part.widgets.append(hud_.widgets[i]);
    const QJsonArray ws = QJsonDocument::fromJson(part.toJson()).object().value("widgets").toArray();
    QApplication::clipboard()->setText(QString::fromUtf8(
        QJsonDocument(QJsonObject{{CLIPBOARD_KEY, ws}}).toJson(QJsonDocument::Compact)));
}

void HudEditorPage::onPaste()
{
    if (!info_)
        return;
    const QJsonObject d = QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object();
    if (!d.value(CLIPBOARD_KEY).isArray())
        return;
    const QJsonObject wrapped{{"vial-armed-hud", FORMAT}, {"widgets", d.value(CLIPBOARD_KEY)}};
    auto parsed = Layout::fromJson(QJsonDocument(wrapped).toJson());
    auto *part = std::get_if<Layout>(&parsed);
    if (!part || part->widgets.isEmpty() || hud_.widgets.size() + part->widgets.size() > info_->maxWidgets)
        return;
    snapshot();
    const int start = int(hud_.widgets.size());
    for (Widget w : part->widgets) {
        w.x = std::min(info_->width - 1, w.x + 8);
        w.y = std::min(info_->height - 1, w.y + 8);
        hud_.widgets.append(w);
    }
    sel_.clear();
    for (int i = start; i < hud_.widgets.size(); ++i)
        sel_.append(i);
    changed();
    syncListSelection();
}

// +1: one step towards the front (drawn later), -1: towards the back; ±99: all the way
void HudEditorPage::onOrder(int d)
{
    const QList<int> sel = validSel();
    if (sel.isEmpty())
        return;
    const int n = int(hud_.widgets.size());
    QList<int> order;
    for (int i = 0; i < n; ++i)
        order.append(i);
    QList<int> moving = sel;
    std::sort(moving.begin(), moving.end());
    if (d > 0)
        std::reverse(moving.begin(), moving.end());
    bool any = false;
    for (int i : moving) {
        int pos = int(order.indexOf(i));
        const int target = std::clamp(pos + d, 0, n - 1);
        const int step = target > pos ? 1 : -1;
        while (pos != target && !sel.contains(order[pos + step])) {
            std::swap(order[pos], order[pos + step]);
            pos += step;
            any = true;
        }
    }
    if (!any)
        return;
    snapshot();
    reorder(order);
    changed();
    syncListSelection();
}

void HudEditorPage::onDelete()
{
    QList<int> sel = validSel();
    if (sel.isEmpty())
        return;
    snapshot();
    std::sort(sel.begin(), sel.end(), std::greater<>());
    for (int i : sel)
        hud_.widgets.removeAt(i);
    sel_.clear();
    changed();
}

void HudEditorPage::onNudge(int dx, int dy)
{
    if (sel_.isEmpty() || !info_)
        return;
    snapshot();
    for (int i : validSel()) {
        Widget &w = hud_.widgets[i];
        w.x = std::clamp(w.x + dx, 0, info_->width - 1);
        w.y = std::clamp(w.y + dy, 0, info_->height - 1);
    }
    changed();
}

// ---- arrange

QList<std::pair<int, Box>> HudEditorPage::boxes() const
{
    QList<std::pair<int, Box>> out;
    for (int i : validSel())
        if (i < areas_.size() && areas_[i])
            out.append({i, *areas_[i]});
    return out;
}

void HudEditorPage::align(const QString &how)
{
    const auto items = boxes();
    if (items.isEmpty() || !info_)
        return;
    Box ref{0, 0, info_->width - 1, info_->height - 1};
    if (items.size() > 1) {
        ref = items.first().second;
        for (const auto &[i, a] : items) {
            ref.x0 = std::min(ref.x0, a.x0);
            ref.y0 = std::min(ref.y0, a.y0);
            ref.x1 = std::max(ref.x1, a.x1);
            ref.y1 = std::max(ref.y1, a.y1);
        }
    }
    snapshot();
    for (const auto &[i, a] : items) {
        Widget &w = hud_.widgets[i];
        const int bw = a.x1 - a.x0, bh = a.y1 - a.y0;
        if (how == "left")
            w.x = ref.x0;
        else if (how == "hcenter")
            w.x = (ref.x0 + ref.x1 - bw + 1) / 2;
        else if (how == "right")
            w.x = ref.x1 - bw;
        else if (how == "top")
            w.y = ref.y0;
        else if (how == "vcenter")
            w.y = (ref.y0 + ref.y1 - bh + 1) / 2;
        else if (how == "bottom")
            w.y = ref.y1 - bh;
        w.x = std::max(0, w.x);
        w.y = std::max(0, w.y);
    }
    changed();
}

void HudEditorPage::distribute(bool x)
{
    auto items = boxes();
    if (items.size() < 3)
        return;
    auto lo = [x](const Box &b) { return x ? b.x0 : b.y0; };
    auto hi = [x](const Box &b) { return x ? b.x1 : b.y1; };
    std::stable_sort(items.begin(), items.end(), [&](const auto &a, const auto &b) { return lo(a.second) < lo(b.second); });
    const int start = lo(items.first().second);
    int end = 0, total = 0;
    for (const auto &[i, a] : items) {
        end = std::max(end, hi(a) + 1);
        total += hi(a) - lo(a) + 1;
    }
    const double gap = double(end - start - total) / (items.size() - 1);
    snapshot();
    double pos = start;
    for (const auto &[i, a] : items) {
        // Python's round(): halves to even
        const int v = int(std::nearbyint(pos));
        (x ? hud_.widgets[i].x : hud_.widgets[i].y) = v;
        pos += hi(a) - lo(a) + 1 + gap;
    }
    changed();
}

void HudEditorPage::match(bool width)
{
    const auto items = boxes();
    if (items.size() < 2)
        return;
    const Box ref = items.last().second; // the primary selection is last
    const int size = width ? ref.x1 - ref.x0 + 1 : ref.y1 - ref.y0 + 1;
    edit([width, size](Widget &w) { (width ? w.w : w.h) = size; });
}

void HudEditorPage::onContext(const QPoint &global)
{
    QMenu m;
    const bool has = !sel_.isEmpty();
    m.addAction(tr("Duplicate\tCtrl+D"), this, &HudEditorPage::onDuplicate)->setEnabled(has);
    m.addAction(tr("Copy\tCtrl+C"), this, &HudEditorPage::onCopy)->setEnabled(has);
    m.addAction(tr("Paste\tCtrl+V"), this, &HudEditorPage::onPaste);
    m.addAction(tr("Delete\tDel"), this, &HudEditorPage::onDelete)->setEnabled(has);
    m.addSeparator();
    for (const auto &[text, d] : {std::pair{tr("Bring to front"), 99}, std::pair{tr("Bring forward\tCtrl+]"), 1},
                                  std::pair{tr("Send backward\tCtrl+["), -1}, std::pair{tr("Send to back"), -99}})
        m.addAction(text, this, [this, d] { onOrder(d); })->setEnabled(has);
    m.addSeparator();
    QMenu *al = m.addMenu(tr("Align"));
    al->setEnabled(has);
    for (const auto &[text, how] : {std::pair{tr("Left"), "left"}, std::pair{tr("Centre"), "hcenter"},
                                    std::pair{tr("Right"), "right"}, std::pair{tr("Top"), "top"},
                                    std::pair{tr("Middle"), "vcenter"}, std::pair{tr("Bottom"), "bottom"}})
        al->addAction(text, this, [this, h = QString(how)] { align(h); });
    m.addAction(tr("Select all\tCtrl+A"), this, &HudEditorPage::selectAll);
    m.exec(global);
}

void HudEditorPage::onUndo()
{
    if (undo_.isEmpty())
        return;
    redo_.append(hud_);
    hud_ = undo_.takeLast();
    sel_ = validSel();
    changed();
}

void HudEditorPage::onRedo()
{
    if (redo_.isEmpty())
        return;
    undo_.append(hud_);
    hud_ = redo_.takeLast();
    sel_ = validSel();
    changed();
}

void HudEditorPage::onPreset(int index)
{
    if (!loaded_ || !info_)
        return;
    const auto &p = hud::presets().value(index);
    pushTimer_.stop();
    if (p.make) {
        snapshot();
        hud_ = p.make();
        sel_.clear();
        changed();
        return;
    }
    ctl->session()->run(hid::Priority::Interactive, this, [info = *info_, shown = shown_](hid::Link &link) {
        armed::hudRevert(link, true);
        QByteArray blob = armed::hudRead(link, info);
        *shown = blob;
        return blob;
    }, [this, serial = serial_](hid::Result<QByteArray> r) {
        if (serial != serial_)
            return;
        if (!r.ok()) {
            showError(r.error);
            return;
        }
        auto layout = Layout::unpack(*r.value, {info_->maxWidgets, info_->strings});
        if (!layout) {
            showError(tr("the keyboard's layout is not readable"));
            return;
        }
        snapshot();
        hud_ = *layout;
        sel_.clear();
        changed();
    });
}

void HudEditorPage::onImport()
{
    if (!info_)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Import HUD layout"), {}, tr("HUD layout (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    QString why;
    std::optional<Layout> layout;
    if (!f.open(QIODevice::ReadOnly)) {
        why = f.errorString();
    } else {
        auto parsed = Layout::fromJson(f.readAll());
        if (auto *e = std::get_if<QString>(&parsed)) {
            why = *e;
        } else {
            auto packed = std::get<Layout>(parsed).pack({info_->maxWidgets, info_->strings});
            if (auto *e2 = std::get_if<QString>(&packed))
                why = *e2;
            else
                layout = std::get<Layout>(parsed);
        }
    }
    if (!layout) {
        QMessageBox::warning(this, {}, tr("Not a HUD layout this keyboard can show: %1").arg(why));
        return;
    }
    snapshot();
    hud_ = *layout;
    sel_.clear();
    changed();
}

void HudEditorPage::onExport()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export HUD layout"), "hud.json", tr("HUD layout (*.json)"));
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(hud_.toJson()) < 0 || !f.commit())
        QMessageBox::warning(this, {}, f.errorString());
}

// sends what differs from the shown layout; queued pushes replace each other, the diff is taken when one runs
void HudEditorPage::push()
{
    if (!loaded_ || !info_ || !valid())
        return;
    auto packed = hud_.pack({info_->maxWidgets, info_->strings});
    if (auto *e = std::get_if<QString>(&packed)) {
        showError(e->toUpper());
        return;
    }
    ctl->session()->run(hid::Priority::Interactive, this, [blob = std::get<QByteArray>(packed), shown = shown_](hid::Link &link) {
        if (*shown == blob)
            return;
        try {
            armed::hudWrite(link, blob, *shown);
            *shown = blob;
        } catch (...) {
            shown->reset(); // unknown now: send everything next time
            throw;
        }
    }, [this, serial = serial_](hid::Result<hid::Unit> r) {
        if (serial == serial_)
            showError(r.ok() ? QString() : r.error);
    }, QStringLiteral("hud-push"));
}

void HudEditorPage::onSave()
{
    if (!loaded_ || !info_ || !valid())
        return;
    pushTimer_.stop();
    auto packed = hud_.pack({info_->maxWidgets, info_->strings});
    if (auto *e = std::get_if<QString>(&packed)) {
        showError(e->toUpper());
        return;
    }
    ctl->session()->run(hid::Priority::Interactive, this, [blob = std::get<QByteArray>(packed), shown = shown_](hid::Link &link) {
        if (*shown != blob) {
            try {
                armed::hudWrite(link, blob, *shown);
                *shown = blob;
            } catch (...) {
                shown->reset();
                throw;
            }
        }
        armed::hudStore(link);
    }, [this, serial = serial_, layout = hud_](hid::Result<hid::Unit> r) {
        if (serial != serial_)
            return;
        if (r.ok()) {
            saved_ = layout;
            showError({});
            ctl->toast(tr("HUD layout saved on the keyboard"), Controller::ToastKind::Ok);
        } else {
            showError(r.error);
        }
        refreshState();
    });
}

void HudEditorPage::onRevert()
{
    if (!loaded_ || !info_ || !valid())
        return;
    pushTimer_.stop();
    ctl->session()->run(hid::Priority::Interactive, this, [info = *info_, shown = shown_](hid::Link &link) {
        armed::hudRevert(link, false);
        QByteArray blob = armed::hudRead(link, info);
        *shown = blob;
        return blob;
    }, [this, serial = serial_](hid::Result<QByteArray> r) {
        if (serial != serial_)
            return;
        if (!r.ok()) {
            showError(r.error);
            return;
        }
        auto layout = Layout::unpack(*r.value, {info_->maxWidgets, info_->strings});
        if (!layout) {
            showError(tr("the keyboard's layout is not readable"));
            return;
        }
        hud_ = *layout;
        saved_ = hud_;
        undo_.clear();
        redo_.clear();
        sel_ = validSel();
        showError({});
        refreshAll();
    });
}

// ---- panels

QString HudEditorPage::summary(const Widget &wd) const
{
    QString what;
    if (wd.type == W_FRAME) {
        what = frameName(wd.param);
    } else {
        what = wd.source ? sourceName(wd.source) : QString();
        if ((wd.type == W_TEXT || wd.type == W_INDICATOR) && wd.label && *wd.label != "{}")
            what = QStringLiteral("\"%1\"").arg(*wd.label) + (what.isEmpty() ? QString() : "  · " + what);
    }
    const QString name = typeName(wd.type);
    return name.leftJustified(10) + " " + what;
}

QString HudEditorPage::sourceName(int s) const
{
    if (s >= S_HOST && s < S_HOST + agent::HOST_VALUES)
        return QStringLiteral("H%1 · %2").arg(s - S_HOST + 1).arg(agent::slotName(cfg_, s - S_HOST));
    if (s >= S_HOST_TXT && s < S_HOST_TXT + agent::HOST_TEXTS)
        return QStringLiteral("T%1 · %2").arg(s - S_HOST_TXT + 1).arg(agent::textName(cfg_, s - S_HOST_TXT));
    for (const auto &src : sources_)
        if (src.id == s)
            return src.name;
    return QStringLiteral("#%1").arg(s);
}

void HudEditorPage::refreshList()
{
    updating_ = true;
    list_->clear();
    const int n = int(hud_.widgets.size());
    for (int i = n - 1; i >= 0; --i) {
        auto *item = new QListWidgetItem(summary(hud_.widgets[i]));
        item->setData(Qt::UserRole, i);
        if (i < areas_.size() && !areas_[i]) {
            item->setForeground(th::WARN);
            item->setToolTip(tr("Off the screen"));
        }
        list_->addItem(item);
    }
    updating_ = false;
    syncListSelection();
    count_->setText(QStringLiteral("%1 / %2").arg(n).arg(info_ ? info_->maxWidgets : 0));
}

void HudEditorPage::refreshButtons()
{
    const bool full = !info_ || hud_.widgets.size() >= info_->maxWidgets;
    for (auto *b : addButtons_)
        b->setEnabled(loaded_ && !full);
    const int k = int(validSel().size());
    for (auto *b : {dupBtn_, upBtn_, downBtn_, delBtn_})
        b->setEnabled(k > 0);
    for (const auto &[name, b] : arrangeButtons_)
        b->setEnabled(name.startsWith("dist") ? k >= 3 : name.startsWith("same") ? k >= 2 : k >= 1);
    for (auto *b : {presetsBtn_, importBtn_, exportBtn_})
        b->setEnabled(loaded_);
}

void HudEditorPage::refreshStatus()
{
    QStringList parts;
    if (cursor_.x() >= 0)
        parts << QStringLiteral("%1, %2").arg(cursor_.x()).arg(cursor_.y());
    const Widget *wd = current();
    if (sel_.size() == 1 && wd) {
        const int i = sel_.first();
        const auto a = i < areas_.size() ? areas_[i] : std::nullopt;
        const QString size = a ? QStringLiteral("%1 × %2").arg(a->x1 - a->x0 + 1).arg(a->y1 - a->y0 + 1) : tr("OFF SCREEN");
        parts << QStringLiteral("%1 · X %2 Y %3 · %4").arg(typeName(wd->type)).arg(wd->x).arg(wd->y).arg(size);
    } else if (!sel_.isEmpty()) {
        parts << tr("%1 SELECTED").arg(sel_.size());
    }
    parts << tr("Shift / Ctrl + click or drag a box: several · arrows nudge, Shift: 8 px · Alt: no snapping · right click: more");
    status_->setText(parts.join("   ·   "));
}

void HudEditorPage::refreshState()
{
    const bool dirty = saved_ && hud_ != *saved_;
    state_->setVisible(loaded_);
    state_->setText(dot(dirty ? th::ACC : th::OK, dirty ? tr("ON THE KEYBOARD, NOT SAVED") : tr("SAVED")));
    state_->setToolTip(dirty ? tr("The keyboard already shows your changes; SAVE keeps them after it is unplugged") : QString());
    saveBtn_->setEnabled(dirty);
    revertBtn_->setEnabled(dirty);
    undoBtn_->setEnabled(!undo_.isEmpty());
    redoBtn_->setEnabled(!redo_.isEmpty());
}

void HudEditorPage::showRows(const QSet<QString> &keys)
{
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
        it->label->setVisible(keys.contains(it.key()));
        it->widget->setVisible(keys.contains(it.key()));
    }
    for (const auto &[title, ks] : sections_) {
        bool any = false;
        for (const auto &k : ks)
            any |= keys.contains(k);
        title->setVisible(any);
    }
}

void HudEditorPage::refreshProps()
{
    const QList<int> sel = validSel();
    const Widget *wd = current();
    props_->setEnabled(!sel.isEmpty());
    if (sel.isEmpty()) {
        showRows({});
        selLbl_->show();
        selLbl_->setText(loaded_ ? tr("Nothing selected. Click a widget on the screen or in the list, or add one with "
                                      "the buttons above the screen.")
                                 : QString());
        hintLbl_->clear();
        return;
    }
    updating_ = true;
    QSet<int> types;
    for (int i : sel)
        types.insert(hud_.widgets[i].type);
    QSet<QString> keys;
    auto subset = [&](const QSet<int> &of) {
        for (int t : types)
            if (!of.contains(t))
                return false;
        return true;
    };
    if (sel.size() > 1) {
        selLbl_->setText(tr("%1 widgets: changes apply to all of them.").arg(sel.size()));
        selLbl_->show();
        keys = {"fg", "bg"};
        if (subset({W_TEXT, W_INDICATOR, W_TEXTBOX}))
            keys << "font";
        if (subset({W_TEXT, W_TEXTBOX}))
            keys << "align";
    } else {
        selLbl_->hide();
        const int t = wd->type;
        keys = {"type", "geo", "fg"};
        if (t != W_FRAME)
            keys << "source";
        if (t == W_TEXT || t == W_INDICATOR)
            keys << "label";
        if (t == W_TEXT || t == W_INDICATOR || t == W_TEXTBOX)
            keys << "font";
        if (t == W_TEXT || t == W_TEXTBOX || t == W_BAR || t == W_GRAPH || t == W_INDICATOR)
            keys << "bg";
        if (t == W_TEXT || t == W_TEXTBOX)
            keys << "align";
        if (t == W_TEXT)
            keys << "digits" << "warn";
        if (t == W_BAR || t == W_GRAPH)
            keys << "range" << "warn";
        if (t == W_BAR)
            keys << "segments";
        if (t == W_GRAPH)
            keys << "period";
        if (t == W_INDICATOR)
            keys << "dot";
        if (t == W_FRAME)
            keys << "frame";
        if (t == W_TEXTBOX)
            keys << "lines";
    }
    showRows(keys);

    auto common = [&](int Widget::*f) -> std::optional<int> {
        const int v = hud_.widgets[sel.first()].*f;
        for (int i : sel)
            if (hud_.widgets[i].*f != v)
                return std::nullopt;
        return v;
    };
    fg_->setCurrent(common(&Widget::fg));
    bg_->setCurrent(common(&Widget::bg));
    const auto font = common(&Widget::font);
    fontCombo_->setCurrentIndex(font && *font < fontCombo_->count() ? *font : -1);
    std::optional<int> al = hud_.widgets[sel.first()].flags & F_ALIGN;
    for (int i : sel)
        if ((hud_.widgets[i].flags & F_ALIGN) != *al)
            al.reset();
    alignSeg_->setCurrent(al.value_or(-1));
    if (sel.size() == 1)
        fillSingle(*wd);
    updating_ = false;
}

void HudEditorPage::fillSingle(const Widget &wd)
{
    const int t = wd.type;
    typeCombo_->setCurrentIndex(typeCombo_->findData(t));
    sourceCombo_->clear();
    sourceCombo_->addItem("—", int(S_NONE));
    auto *model = qobject_cast<QStandardItemModel *>(sourceCombo_->model());
    QList<armed::HudSource> kb, host, txt;
    for (const auto &s : sources_) {
        if (!fits(t, s))
            continue;
        (s.id < S_HOST ? kb : s.id < S_HOST_TXT ? host : txt).append(s);
    }
    for (const auto &[title, items] : {std::pair{tr("— KEYBOARD"), kb}, std::pair{tr("— THIS COMPUTER: NUMBERS"), host},
                                       std::pair{tr("— THIS COMPUTER: TEXT"), txt}}) {
        if (items.isEmpty())
            continue;
        sourceCombo_->addItem(title);
        if (model)
            model->item(sourceCombo_->count() - 1)->setEnabled(false);
        for (const auto &s : items)
            sourceCombo_->addItem("   " + sourceName(s.id), s.id);
    }
    int idx = sourceCombo_->findData(wd.source);
    if (idx < 0) {
        sourceCombo_->addItem(sourceName(wd.source), wd.source);
        idx = sourceCombo_->count() - 1;
    }
    sourceCombo_->setCurrentIndex(idx);
    nowLbl_->setVisible(wd.source != S_NONE);
    labelEdit_->setText(wd.label.value_or(QString()));
    labelEdit_->setPlaceholderText(t == W_TEXT ? tr("e.g. CPU {}%  ({} is the value)") : tr("the source's name"));
    valueBtn_->setVisible(t == W_TEXT);
    spin_["x"]->setValue(wd.x);
    spin_["y"]->setValue(wd.y);
    spin_["w"]->setValue(wd.w);
    spin_["h"]->setValue(wd.h);
    const int dec = sourceDec(wd);
    const double k = std::pow(10, dec);
    for (auto [sp, v] : {std::pair{lo_, wd.lo}, std::pair{hi_, wd.hi}, std::pair{warn_, wd.warn}}) {
        sp->setDecimals(dec);
        sp->setValue(v / k);
    }
    const bool warn = wd.flags & F_WARN;
    warnChk_->setChecked(warn);
    warn_->setEnabled(warn);
    below_->setEnabled(warn);
    below_->setCurrentIndex(wd.flags & F_WARN_BELOW ? 1 : 0);
    digits_->setValue(wd.param & 0x0F);
    segments_->setValue(wd.param);
    period_->setValue((wd.param ? wd.param : 10) / 10.0);
    fill_->setChecked(wd.flags & F_FILL);
    dot_->setChecked(wd.flags & F_DOT);
    frameStyle_->setCurrentIndex(std::min<int>(wd.param, FRAME_PANEL));
    corner_->setValue(wd.lo);
    corner_->setEnabled(wd.param == FRAME_BRACKETS);
    gap_->setValue(wd.param);
    tail_->setChecked(wd.flags & F_TAIL);
    QString hint;
    switch (t) {
    case W_TEXT:
        hint = tr("No source: a fixed label. AUTO colour follows the source: accent above the base layer, green while "
                  "charging, the link state's colour.");
        break;
    case W_BAR: hint = tr("Fills from the left between FROM and TO."); break;
    case W_GRAPH:
        hint = tr("Newest sample on the right; the keyboard keeps 64. %1 graphs at most.").arg(info_ ? info_->graphs : 4);
        break;
    case W_INDICATOR:
        hint = tr("Lit while the source is on. DOT ONLY with AUTO colour shows the source's colour, like the link dot.");
        break;
    case W_BATTERY: hint = tr("Always 25 × 11 px."); break;
    case W_FRAME: hint = tr("Redrawn whenever a widget over it changes."); break;
    case W_TEXTBOX:
        hint = tr("Wrapped between words. The keyboard's fonts have no Cyrillic: the agent spells it in Latin letters.");
        break;
    }
    hintLbl_->setText(hint);
}

bool HudEditorPage::fits(int t, const armed::HudSource &s)
{
    if (t == W_BAR || t == W_GRAPH || t == W_BATTERY)
        return s.kind == K_NUMBER;
    if (t == W_TEXTBOX)
        return s.kind == K_LINES || s.kind == K_TEXT;
    return true;
}

// ---- agent

void HudEditorPage::loadAgentRows()
{
    updating_ = true;
    for (int i = 0; i < valueRows_.size(); ++i) {
        const ValueRow &r = valueRows_[i];
        const agent::ValueSlot s = cfg_.values.value(i);
        r.metric->setCurrentIndex(std::max(0, r.metric->findData(s.metric)));
        r.command->setText(s.command);
        r.interval->setValue(s.interval);
        r.dec->setValue(s.dec);
        const bool isCmd = s.metric == "command";
        r.command->setEnabled(isCmd);
        r.interval->setEnabled(isCmd);
        r.dec->setEnabled(!s.metric.isEmpty());
    }
    for (int i = 0; i < textRows_.size(); ++i) {
        const agent::TextSlot s = cfg_.texts.value(i);
        textRows_[i].command->setText(s.command);
        textRows_[i].interval->setValue(s.interval);
    }
    updating_ = false;
}

void HudEditorPage::onValueCfg(int i, const QString &what)
{
    if (updating_ || i >= cfg_.values.size())
        return;
    const ValueRow &r = valueRows_[i];
    agent::ValueSlot &s = cfg_.values[i];
    const QString old = s.metric;
    s.metric = r.metric->currentData().toString();
    s.command = r.command->text();
    s.interval = r.interval->value();
    const agent::Metric *m = agent::metric(s.metric);
    if (what == "metric" && m && s.metric != old)
        s.dec = m->dec;
    else
        s.dec = r.dec->value();
    loadAgentRows();
    if (what == "metric" && s.metric == "command")
        r.command->setFocus();
    cfgTimer_.start(CONFIG_SAVE_MS);
}

void HudEditorPage::onTextCfg(int i)
{
    if (updating_ || i >= cfg_.texts.size())
        return;
    cfg_.texts[i].command = textRows_[i].command->text();
    cfg_.texts[i].interval = textRows_[i].interval->value();
    cfgTimer_.start(CONFIG_SAVE_MS);
}

void HudEditorPage::saveConfig()
{
    cfg_.save();
    host_->agent().reload();
    if (loaded_) {
        refreshList();
        refreshProps();
    }
}

void HudEditorPage::refreshAgent()
{
    const auto [color, text] = host_->status();
    agentLbl_->setText(dot(color, text));
    const bool was = updating_;
    updating_ = true;
    autostart_->setChecked(agent::autostartOn());
    updating_ = was;
    QHash<int, int> uses;
    for (const auto &w : hud_.widgets)
        ++uses[w.source];
    for (int i = 0; i < valueRows_.size(); ++i) {
        const int n = uses.value(S_HOST + i);
        valueRows_[i].used->setText(n ? tr("%1 WIDGETS").arg(n) : QStringLiteral("—"));
    }
    for (int i = 0; i < textRows_.size(); ++i) {
        const int n = uses.value(S_HOST_TXT + i);
        textRows_[i].used->setText(n ? tr("%1 WIDGETS").arg(n) : QStringLiteral("—"));
    }
}

void HudEditorPage::onAutostart(bool on)
{
    if (updating_)
        return;
    QString error;
    if (!agent::setAutostart(on, &error))
        QMessageBox::warning(this, {}, error);
    refreshAgent();
}
