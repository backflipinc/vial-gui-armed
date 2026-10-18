#include "ui/widgets/keycodepalette.h"

#include "keycodes/tables.h"
#include "ui/keycodedisplay.h"
#include "ui/theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolTip>
#include <QVBoxLayout>

using keycodes::Group;
using keycodes::Registry;

namespace {

constexpr int GAP = 3;

QList<kle::Key> picture(const char *name)
{
    return kle::deserialize(QJsonDocument::fromJson(keycodes::tables::displayKeyboards().value(name)).array());
}

}

// ---- grid

KeycodeGrid::KeycodeGrid(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    filter_ = [](const QString &) { return true; };
    connect(&KeycodeDisplay::instance(), &KeycodeDisplay::changed, this, &KeycodeGrid::relabel);
}

int KeycodeGrid::unit() const
{
    return int(std::round(QFontMetricsF(font()).height() * 3));
}

void KeycodeGrid::setAlternatives(QList<Alternative> alts, QStringList prefix)
{
    alts_ = std::move(alts);
    prefix_ = std::move(prefix);
    hover_ = focus_ = -1;
    relabel();
}

void KeycodeGrid::setFilter(std::function<bool(const QString &)> filter)
{
    filter_ = std::move(filter);
    hover_ = focus_ = -1;
    relabel();
}

bool KeycodeGrid::hasButtons() const
{
    for (const auto &a : alts_)
        for (const auto *k : a.buttons)
            if (!k->hidden && filter_(k->qmkId))
                return true;
    return false;
}

int KeycodeGrid::layoutItems(int width, QList<Item> *out) const
{
    const int u = unit();
    const KeycodeDisplay &disp = KeycodeDisplay::instance();
    auto item = [&](const QString &id, const QRect &r) {
        Item it;
        it.rect = r;
        it.id = id;
        it.overridden = disp.overridden(id);
        it.label = disp.label(id);
        it.tooltip = Registry::instance().tooltip(id);
        return it;
    };
    for (const Alternative &alt : alts_) {
        int picW = 0;
        for (const auto &k : alt.picture)
            picW = std::max(picW, int(std::ceil((k.x + k.width) * u)));
        if (!alt.picture.isEmpty() && picW > width && &alt != &alts_.last())
            continue;
        QList<Item> items;
        int y = 0;
        if (!alt.picture.isEmpty()) {
            int picH = 0;
            const int left = std::max(0, (width - picW) / 2);
            for (const auto &k : alt.picture) {
                const auto *kc = Registry::instance().findByQmkId(k.labels[0]);
                if (!kc)
                    continue;
                QRect r(left + int(std::round(k.x * u)), int(std::round(k.y * u)),
                        int(std::round(k.width * u)) - GAP, int(std::round(k.height * u)) - GAP);
                items.append(item(kc->qmkId, r));
                picH = std::max(picH, r.bottom() + 1);
            }
            y = picH + 2 * GAP + u / 3;
        }
        int x = 0;
        auto place = [&](Item it) {
            if (x + u > width && x > 0) {
                x = 0;
                y += u + GAP;
            }
            it.rect = QRect(x, y, u, u);
            items.append(it);
            x += u + GAP;
        };
        for (const QString &p : prefix_) {
            Item it;
            it.id = p;
            it.label = p;
            it.prefix = true;
            it.tooltip = tr("Type any keycode or expression, like LCTL(KC_A) or LT(2, KC_SPC)");
            place(it);
        }
        int flowButtons = 0;
        for (const auto *k : alt.buttons) {
            if (k->hidden || !filter_(k->qmkId))
                continue;
            place(item(k->qmkId, {}));
            ++flowButtons;
        }
        const int h = (x > 0 || flowButtons || !prefix_.isEmpty()) ? y + u : y;
        if (out)
            *out = items;
        return h;
    }
    if (out)
        out->clear();
    return 0;
}

int KeycodeGrid::heightForWidth(int w) const
{
    auto it = heights_.constFind(w);
    if (it == heights_.constEnd())
        it = heights_.insert(w, layoutItems(w, nullptr));
    return *it;
}

void KeycodeGrid::updateItem(int i)
{
    if (i >= 0 && i < items_.size())
        update(items_[i].rect.adjusted(-3, -3, 3, 3));
}

QSize KeycodeGrid::sizeHint() const
{
    return {600, heightForWidth(std::max(width(), 600))};
}

void KeycodeGrid::relabel()
{
    heights_.clear();
    layoutItems(width(), &items_);
    updateGeometry();
    update();
}

void KeycodeGrid::resizeEvent(QResizeEvent *)
{
    layoutItems(width(), &items_);
}

int KeycodeGrid::itemAt(const QPoint &p) const
{
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].rect.contains(p))
            return i;
    return -1;
}

void KeycodeGrid::activate(int i)
{
    if (i < 0 || i >= items_.size())
        return;
    if (items_[i].prefix)
        emit prefixClicked(items_[i].id);
    else
        emit keycodeClicked(items_[i].id);
}

void KeycodeGrid::mouseMoveEvent(QMouseEvent *e)
{
    int i = itemAt(e->position().toPoint());
    if (i != hover_) {
        updateItem(hover_);
        updateItem(i);
        hover_ = i;
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
}

void KeycodeGrid::leaveEvent(QEvent *)
{
    updateItem(hover_);
    hover_ = -1;
}

void KeycodeGrid::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    int i = itemAt(e->position().toPoint());
    if (i >= 0) {
        focus_ = i;
        activate(i);
        update();
    }
}

void KeycodeGrid::focusInEvent(QFocusEvent *e)
{
    if (focus_ < 0 && !items_.isEmpty())
        focus_ = 0;
    update();
    QWidget::focusInEvent(e);
}

void KeycodeGrid::keyPressEvent(QKeyEvent *e)
{
    if (items_.isEmpty())
        return QWidget::keyPressEvent(e);
    auto nearest = [this](const QPoint &from, int dx, int dy) {
        int best = -1;
        double bestD = 1e18;
        for (int i = 0; i < items_.size(); ++i) {
            QPoint c = items_[i].rect.center() - from;
            if ((dx && c.x() * dx <= 0) || (dy && c.y() * dy <= 0))
                continue;
            double d = dx ? std::abs(c.x()) + 3.0 * std::abs(c.y()) : std::abs(c.y()) + 3.0 * std::abs(c.x());
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return best;
    };
    const QPoint from = items_.value(focus_, items_.first()).rect.center();
    int next = -1;
    switch (e->key()) {
    case Qt::Key_Left: next = nearest(from, -1, 0); break;
    case Qt::Key_Right: next = nearest(from, 1, 0); break;
    case Qt::Key_Up: next = nearest(from, 0, -1); break;
    case Qt::Key_Down: next = nearest(from, 0, 1); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        activate(focus_);
        return;
    default:
        return QWidget::keyPressEvent(e);
    }
    if (next >= 0) {
        focus_ = next;
        if (auto *area = qobject_cast<QScrollArea *>(parentWidget() ? parentWidget()->parentWidget() : nullptr))
            area->ensureVisible(items_[next].rect.center().x(), items_[next].rect.center().y(), 0, items_[next].rect.height());
        update();
    }
}

bool KeycodeGrid::event(QEvent *e)
{
    if (e->type() == QEvent::FontChange)
        heights_.clear();
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        int i = itemAt(he->pos());
        if (i >= 0 && !items_[i].tooltip.isEmpty())
            QToolTip::showText(he->globalPos(), items_[i].tooltip, this, items_[i].rect);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void KeycodeGrid::paintEvent(QPaintEvent *ev)
{
    using namespace ui::theme;
    QPainter qp(this);
    QFont base = mono(12);
    for (int i = 0; i < items_.size(); ++i) {
        const Item &it = items_[i];
        if (!it.rect.intersects(ev->rect()))
            continue;
        const bool hot = i == hover_;
        const bool focused = hasFocus() && i == focus_;
        qp.setPen(hot ? ACC : focused ? ACC_D : LINE2);
        qp.setBrush(it.prefix ? ACC_BG : PANEL);
        qp.drawRect(it.rect.adjusted(0, 0, -1, -1));
        QRect r = it.rect.adjusted(3, 3, -3, -3);
        QFont f(base);
        for (int k = 0; k < 4; ++k) {
            QFontMetrics fm(f);
            int widest = 0;
            for (const QString &w : it.label.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
                widest = std::max(widest, fm.horizontalAdvance(w));
            if (widest <= r.width())
                break;
            f.setPixelSize(std::max(7, int(f.pixelSize() * 0.86)));
        }
        qp.setFont(f);
        qp.setPen(hot || it.overridden || it.prefix ? ACC : TXT);
        qp.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, it.label);
    }
}

// ---- palette

KeycodePalette::KeycodePalette(QWidget *parent)
    : QWidget(parent)
    , tabs_(new QTabBar)
    , search_(new QLineEdit)
    , pages_(new QStackedWidget)
    , results_(new KeycodeGrid)
    , resultsArea_(new QScrollArea)
{
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(ui::theme::S2);
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(true);
    top->addWidget(tabs_, 1);
    search_->setPlaceholderText(tr("Search keycodes"));
    search_->setClearButtonEnabled(true);
    search_->setFixedWidth(220);
    search_->setToolTip(tr("Find a keycode by name, alias or legend (Ctrl+F)"));
    top->addWidget(search_);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    l->addLayout(top);
    l->addWidget(pages_, 1);

    auto addTab = [this](const QString &title) {
        auto *grid = new KeycodeGrid;
        auto *area = new QScrollArea;
        area->setWidget(grid);
        area->setWidgetResizable(true);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        area->setFrameShape(QFrame::NoFrame);
        connect(grid, &KeycodeGrid::keycodeClicked, this, &KeycodePalette::onPicked);
        connect(grid, &KeycodeGrid::prefixClicked, this, &KeycodePalette::anyKeycodeRequested);
        grids_.append(grid);
        areas_.append(area);
        titles_.append(title);
        pages_->addWidget(area);
    };
    for (const char *t : {"Basic", "ISO/JIS", "Layers", "Quantum", "Backlight", "App, Media and Mouse", "MIDI",
                          "Tap Dance", "User", "Macro"})
        addTab(tr(t));
    resultsArea_->setWidget(results_);
    resultsArea_->setWidgetResizable(true);
    resultsArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultsArea_->setFrameShape(QFrame::NoFrame);
    pages_->addWidget(resultsArea_);
    connect(results_, &KeycodeGrid::keycodeClicked, this, &KeycodePalette::onPicked);
    connect(results_, &KeycodeGrid::prefixClicked, this, &KeycodePalette::anyKeycodeRequested);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (i >= 0 && search_->text().isEmpty())
            pages_->setCurrentWidget(areas_[tabs_->tabData(i).toInt()]);
    });
    connect(search_, &QLineEdit::textChanged, this, &KeycodePalette::applySearch);
    auto *find = new QAction(this);
    find->setShortcut(QKeySequence::Find);
    find->setShortcutContext(Qt::WindowShortcut);
    connect(find, &QAction::triggered, search_, [this] {
        search_->setFocus();
        search_->selectAll();
    });
    addAction(find);
    rebuild();
}

void KeycodePalette::onPicked(const QString &qmkId)
{
    emit keycodeSelected(Registry::instance().normalize(qmkId));
}

void KeycodePalette::setBasicOnly(bool on)
{
    if (basicOnly_ == on)
        return;
    basicOnly_ = on;
    rebuild();
}

void KeycodePalette::rebuild()
{
    const Registry &reg = Registry::instance();
    auto g = [&reg](Group grp) { return reg.group(grp); };
    using Alt = KeycodeGrid::Alternative;
    const auto special = g(Group::Special), shifted = g(Group::Shifted), numpad = g(Group::BasicNumpad),
               nav = g(Group::BasicNav), kr = g(Group::IsoKr);
    const QList<QList<Alt>> alts = {
        {{picture("ansi_100"), special + shifted},
         {picture("ansi_80"), special + numpad + shifted},
         {picture("ansi_70"), special + numpad + nav + shifted},
         {{}, special + g(Group::Basic) + shifted}},
        {{picture("iso_100"), special + shifted + kr},
         {picture("iso_80"), special + numpad + shifted + kr},
         {picture("iso_70"), special + numpad + nav + shifted + kr},
         {{}, g(Group::Iso)}},
        {{{}, reg.layers()}},
        {{picture("mods"), g(Group::Boot) + g(Group::Quantum)},
         {picture("mods_narrow"), g(Group::Boot) + g(Group::Quantum)},
         {{}, g(Group::Boot) + g(Group::Modifiers) + g(Group::Quantum)}},
        {{{}, g(Group::Backlight)}},
        {{{}, g(Group::Media)}},
        {{{}, reg.midi()}},
        {{{}, reg.tapDances()}},
        {{{}, reg.user()}},
        {{{}, reg.macros()}},
    };
    auto filter = [this, &reg](const QString &id) { return !basicOnly_ || reg.isBasic(id); };
    const QString was = tabs_->currentIndex() >= 0 ? tabs_->tabText(tabs_->currentIndex()) : QString();
    {
        QSignalBlocker block(tabs_);
        while (tabs_->count())
            tabs_->removeTab(0);
        for (int i = 0; i < grids_.size(); ++i) {
            grids_[i]->setFilter(filter);
            grids_[i]->setAlternatives(alts[i], i < 2 ? QStringList{tr("Any")} : QStringList{});
            if (grids_[i]->hasButtons()) {
                int t = tabs_->addTab(titles_[i]);
                tabs_->setTabData(t, i);
                if (titles_[i] == was)
                    tabs_->setCurrentIndex(t);
            }
        }
    }
    results_->setFilter(filter);
    if (tabs_->currentIndex() >= 0 && search_->text().isEmpty())
        pages_->setCurrentWidget(areas_[tabs_->tabData(tabs_->currentIndex()).toInt()]);
    applySearch();
}

void KeycodePalette::applySearch()
{
    const QString q = search_->text().trimmed();
    if (q.isEmpty()) {
        if (tabs_->currentIndex() >= 0)
            pages_->setCurrentWidget(areas_[tabs_->tabData(tabs_->currentIndex()).toInt()]);
        tabs_->setEnabled(true);
        return;
    }
    const Registry &reg = Registry::instance();
    QList<const keycodes::Keycode *> all;
    for (Group grp : {Group::Special, Group::Basic, Group::Shifted, Group::Iso, Group::Boot, Group::Modifiers,
                      Group::Quantum, Group::Backlight, Group::Media})
        all += reg.group(grp);
    all += reg.layers() + reg.midi() + reg.tapDances() + reg.user() + reg.macros();
    QList<const keycodes::Keycode *> hits;
    QSet<QString> seen;
    const QString legendMatch = q.toUpper();
    for (const auto *k : all) {
        if (seen.contains(k->qmkId))
            continue;
        bool match = k->tooltip.contains(q, Qt::CaseInsensitive)
            || KeycodeDisplay::instance().label(k->qmkId).simplified().toUpper() == legendMatch;
        for (const QString &a : k->alias)
            match = match || a.contains(q, Qt::CaseInsensitive);
        if (match) {
            seen.insert(k->qmkId);
            hits.append(k);
        }
    }
    results_->setAlternatives({{{}, hits}});
    pages_->setCurrentWidget(resultsArea_);
    tabs_->setEnabled(false);
}
