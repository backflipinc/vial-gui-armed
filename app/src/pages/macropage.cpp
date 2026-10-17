#include "pages/macropage.h"

#include "keycodes/keycodes.h"
#include "protocol/vial.h"
#include "recorder/macrorecorder.h"
#include "ui/theme.h"
#include "ui/widgets/keybutton.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

using macro::Action;
using macro::Kind;

namespace {

const QList<QPair<QString, Kind>> kKinds = {
    {"Text", Kind::Text}, {"Down", Kind::Down}, {"Up", Kind::Up}, {"Tap", Kind::Tap}, {"Delay (ms)", Kind::Delay},
};

// a plain-text editor for one macro's JSON, like the Python app's "Open Text Editor..."
class TextEditor : public QDialog {
public:
    TextEditor(const QString &text, QWidget *parent)
        : QDialog(parent)
        , edit_(new QPlainTextEdit)
    {
        setWindowTitle(tr("Macro as text"));
        resize(640, 420);
        edit_->setPlainText(text);
        edit_->selectAll();
        auto *l = new QVBoxLayout(this);
        auto *hint = new QLabel(tr("JSON: [[\"text\", \"hi\"], [\"tap\", \"KC_ENTER\"], [\"delay\", 100], ...] · "
                                   "Ctrl+O / Ctrl+S load and save a .vim file"));
        hint->setObjectName("armedHint");
        hint->setWordWrap(true);
        l->addWidget(edit_, 1);
        l->addWidget(hint);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
        connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        l->addWidget(buttons);
        auto *open = new QAction(this);
        open->setShortcut(QKeySequence::Open);
        connect(open, &QAction::triggered, this, [this] {
            QString p = QFileDialog::getOpenFileName(this, tr("Load macro"), {}, tr("Vial macro (*.vim)"));
            QFile f(p);
            if (!p.isEmpty() && f.open(QIODevice::ReadOnly))
                edit_->setPlainText(QString::fromUtf8(f.readAll()));
        });
        auto *save = new QAction(this);
        save->setShortcut(QKeySequence::Save);
        connect(save, &QAction::triggered, this, [this] {
            QString p = QFileDialog::getSaveFileName(this, tr("Save macro"), {}, tr("Vial macro (*.vim)"));
            QFile f(p.endsWith(".vim") || p.isEmpty() ? p : p + ".vim");
            if (!p.isEmpty() && f.open(QIODevice::WriteOnly))
                f.write(edit_->toPlainText().toUtf8());
        });
        addActions({open, save});
    }
    QString text() const { return edit_->toPlainText(); }

private:
    QPlainTextEdit *edit_;
};

}

MacroPage::MacroPage(Controller *ctl)
    : Page(ctl)
    , tabs_(new QTabBar)
    , rowsHost_(new QWidget)
    , rows_(new QVBoxLayout(rowsHost_))
    , memory_(new QLabel)
    , save_(new QPushButton(tr("Save")))
    , revert_(new QPushButton(tr("Revert")))
    , record_(new QPushButton(tr("Record macro")))
    , stop_(new QPushButton(tr("Stop recording")))
{
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(true);
    rows_->setContentsMargins(0, ui::theme::S2, 0, 0);
    rows_->setSpacing(ui::theme::S2);
    auto *scroll = new QScrollArea;
    scroll->setWidget(rowsHost_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *textEdit = new QPushButton(tr("Edit as text..."));
    auto *add = new QPushButton(tr("Add action"));
    auto *enter = new QPushButton(tr("Tap Enter"));
    connect(textEdit, &QPushButton::clicked, this, &MacroPage::openTextEditor);
    connect(add, &QPushButton::clicked, this, [this] {
        macros_[current_].append(Action{});
        rebuildRows();
        changed();
    });
    connect(enter, &QPushButton::clicked, this, [this] {
        macros_[current_].append(Action{Kind::Tap, {}, {"KC_ENTER"}, 0});
        rebuildRows();
        changed();
    });
    recorder_ = MacroRecorder::create(this);
    auto *menu = new QMenu(this);
    menu->addAction(tr("Append to current"), this, [this] { startRecording(true); });
    menu->addAction(tr("Replace everything"), this, [this] { startRecording(false); });
    record_->setMenu(menu);
    record_->setVisible(recorder_ != nullptr);
    record_->setToolTip(tr("Type the macro on your keyboard"));
    stop_->hide();
    connect(stop_, &QPushButton::clicked, this, &MacroPage::stopRecording);
    editButtons_ = {textEdit, add, enter, record_};

    auto *tools = new QHBoxLayout;
    tools->addWidget(textEdit);
    tools->addStretch(1);
    tools->addWidget(add);
    tools->addWidget(enter);
    tools->addWidget(record_);
    tools->addWidget(stop_);

    memory_->setObjectName("armedHint");
    save_->setDefault(true);
    save_->setToolTip(tr("Write all macros to the keyboard (Ctrl+Enter)"));
    save_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    connect(save_, &QPushButton::clicked, this, &MacroPage::save);
    connect(revert_, &QPushButton::clicked, this, &MacroPage::revert);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(memory_);
    bottom->addStretch(1);
    bottom->addWidget(revert_);
    bottom->addWidget(save_);

    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, ui::theme::S2, 0, 0);
    l->addWidget(tabs_);
    l->addWidget(scroll, 1);
    l->addLayout(tools);
    l->addLayout(bottom);
    connect(tabs_, &QTabBar::currentChanged, this, &MacroPage::showMacro);

    if (recorder_) {
        connect(recorder_, &MacroRecorder::failed, this, [this](const QString &e) { this->ctl->toast(e, Controller::ToastKind::Error); });
        auto strokes = std::make_shared<QList<MacroRecorder::Stroke>>();
        connect(recorder_, &MacroRecorder::keystroke, this, [strokes](bool down, const QString &alias) {
            if (const auto *k = keycodes::Registry::instance().findByRecorderAlias(alias))
                strokes->append({down ? MacroRecorder::Stroke::Down : MacroRecorder::Stroke::Up, k->qmkId, {}});
        });
        connect(recorder_, &MacroRecorder::stopped, this, [this, strokes] {
            macro::Macro got = MacroRecorder::toActions(MacroRecorder::optimize(*strokes));
            strokes->clear();
            if (append_)
                macros_[current_] += got;
            else
                macros_[current_] = got;
            stop_->hide();
            for (auto *b : editButtons_)
                b->setVisible(b != record_ || recorder_);
            tabs_->setEnabled(true);
            rebuildRows();
            changed();
        });
    }
}

MacroPage::~MacroPage() = default;

void MacroPage::keyboardChanged()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    macros_ = macro::deserializeAll(kb->macros, kb->macroInfo.count, kb->vialProtocol);
    QSignalBlocker block(tabs_);
    while (tabs_->count())
        tabs_->removeTab(0);
    for (int i = 0; i < kb->macroInfo.count; ++i)
        tabs_->addTab(QStringLiteral("M%1").arg(i));
    current_ = 0;
    tabs_->setCurrentIndex(0);
    rebuildRows();
    changed();
}

void MacroPage::deactivate()
{
    if (stop_->isVisible())
        stopRecording();
}

void MacroPage::showMacro(int index)
{
    current_ = std::max(0, index);
    rebuildRows();
}

QWidget *MacroPage::rowWidget(int index)
{
    const KeyboardState *kb = ctl->keyboard();
    const bool delays = kb && kb->vialProtocol >= vial::VIAL_PROTOCOL_ADVANCED_MACROS;
    const bool basicOnly = kb && kb->vialProtocol < vial::VIAL_PROTOCOL_EXT_MACROS;
    Action &a = macros_[current_][index];

    auto *row = new QWidget;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(ui::theme::S1);
    auto *up = new QToolButton;
    up->setText(QStringLiteral("▲"));
    up->setToolTip(tr("Move up"));
    auto *down = new QToolButton;
    down->setText(QStringLiteral("▼"));
    down->setToolTip(tr("Move down"));
    up->setEnabled(index > 0);
    down->setEnabled(index + 1 < macros_[current_].size());
    connect(up, &QToolButton::clicked, this, [this, index] {
        macros_[current_].swapItemsAt(index, index - 1);
        rebuildRows();
        changed();
    });
    connect(down, &QToolButton::clicked, this, [this, index] {
        macros_[current_].swapItemsAt(index, index + 1);
        rebuildRows();
        changed();
    });
    l->addWidget(up, 0, Qt::AlignTop);
    l->addWidget(down, 0, Qt::AlignTop);

    auto *type = new QComboBox;
    for (const auto &[name, kind] : kKinds)
        if (kind != Kind::Delay || delays)
            type->addItem(tr(qPrintable(name)), int(kind));
    type->setCurrentIndex(type->findData(int(a.kind)));
    type->setFixedWidth(130);
    connect(type, &QComboBox::currentIndexChanged, this, [this, index, type] {
        macros_[current_][index] = Action{Kind(type->currentData().toInt()), {}, {}, 0};
        rebuildRows();
        changed();
    });
    l->addWidget(type, 0, Qt::AlignTop);

    switch (a.kind) {
    case Kind::Text: {
        auto *text = new QLineEdit(a.text);
        text->setPlaceholderText(tr("Text to type"));
        connect(text, &QLineEdit::textChanged, this, [this, index](const QString &t) {
            macros_[current_][index].text = t;
            changed();
        });
        l->addWidget(text, 1, Qt::AlignTop);
        break;
    }
    case Kind::Delay: {
        auto *ms = new QSpinBox;
        ms->setRange(0, 64000);
        ms->setSuffix(tr(" ms"));
        ms->setValue(a.delay);
        connect(ms, &QSpinBox::valueChanged, this, [this, index](int v) {
            macros_[current_][index].delay = v;
            changed();
        });
        l->addWidget(ms, 0, Qt::AlignTop);
        l->addStretch(1);
        break;
    }
    default: {
        auto *keys = new QWidget;
        auto *kl = new QHBoxLayout(keys);
        kl->setContentsMargins(0, 0, 0, 0);
        kl->setSpacing(ui::theme::S1);
        for (int k = 0; k < a.keys.size(); ++k) {
            auto *b = new KeyButton;
            b->setBasicOnly(basicOnly);
            b->setScale(0.85);
            b->setKeycode(a.keys[k]);
            connect(b, &KeyButton::changed, this, [this, index, k](const QString &code) {
                auto &keysRef = macros_[current_][index].keys;
                if (code == QLatin1String("KC_NO")) {
                    keysRef.removeAt(k);
                    QMetaObject::invokeMethod(this, &MacroPage::rebuildRows, Qt::QueuedConnection);
                } else {
                    keysRef[k] = code;
                }
                changed();
            });
            kl->addWidget(b);
        }
        auto *plus = new QToolButton;
        plus->setText(QStringLiteral("+"));
        plus->setToolTip(tr("Add a key"));
        plus->setFixedSize(40, 40);
        connect(plus, &QToolButton::clicked, this, [this, index] {
            macros_[current_][index].keys.append(QStringLiteral("KC_TRNS"));
            rebuildRows();
            changed();
        });
        kl->addWidget(plus);
        kl->addStretch(1);
        l->addWidget(keys, 1);
        break;
    }
    }
    auto *remove = new QToolButton;
    remove->setText(QStringLiteral("×"));
    remove->setToolTip(tr("Remove this action"));
    connect(remove, &QToolButton::clicked, this, [this, index] {
        macros_[current_].removeAt(index);
        rebuildRows();
        changed();
    });
    l->addWidget(remove, 0, Qt::AlignTop);
    return row;
}

void MacroPage::rebuildRows()
{
    if (auto *tray = KeycodeTray::instance())
        tray->close();
    while (QLayoutItem *it = rows_->takeAt(0)) {
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    if (current_ >= macros_.size())
        return;
    if (macros_[current_].isEmpty()) {
        auto *empty = new QLabel(tr("This macro is empty: add an action, type text, or record it."));
        empty->setObjectName("armedHint");
        rows_->addWidget(empty);
    }
    for (int i = 0; i < macros_[current_].size(); ++i)
        rows_->addWidget(rowWidget(i));
    rows_->addStretch(1);
}

QByteArray MacroPage::serialized() const
{
    const KeyboardState *kb = ctl->keyboard();
    return macro::serializeAll(macros_, kb->macroInfo.count, kb->vialProtocol);
}

void MacroPage::changed()
{
    const KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    QByteArray data;
    try {
        data = serialized();
    } catch (const std::exception &) {
    }
    const int used = int(data.size());
    const bool fits = used <= kb->macroInfo.memory;
    memory_->setText(tr("MEMORY %1 / %2 BYTES").arg(used).arg(kb->macroInfo.memory));
    memory_->setStyleSheet(fits ? QString() : QStringLiteral("color: %1").arg(ui::theme::WARN.name()));
    const bool dirty = data != kb->macros;
    save_->setEnabled(dirty && fits);
    revert_->setEnabled(dirty);
    const QList<QByteArray> saved = kb->macros.split('\0');
    for (int i = 0; i < tabs_->count(); ++i) {
        QByteArray mine;
        try {
            mine = macro::serialize(macros_.value(i), kb->vialProtocol);
        } catch (const std::exception &) {
        }
        tabs_->setTabText(i, QStringLiteral("M%1%2").arg(i).arg(mine != saved.value(i) ? "*" : ""));
    }
}

void MacroPage::save()
{
    KeyboardState *kb = ctl->keyboard();
    if (!kb)
        return;
    const QByteArray data = serialized();
    const QByteArray old = kb->macros;
    const int memory = kb->macroInfo.memory;
    kb->macros = data;
    ctl->write(true, [data, memory](hid::Link &link) { vial::setMacroBuffer(link, data, memory); },
               [this, old] {
                   if (KeyboardState *k = ctl->keyboard())
                       k->macros = old;
                   changed();
               },
               [this] { ctl->toast(tr("Macros saved"), Controller::ToastKind::Ok); });
    changed();
}

void MacroPage::revert()
{
    keyboardChanged();
}

void MacroPage::openTextEditor()
{
    const QByteArray json = QJsonDocument(macro::save(macros_[current_])).toJson(QJsonDocument::Compact);
    auto *dlg = new TextEditor(QString::fromUtf8(json), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        QString text = dlg->text().trimmed();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(text.isEmpty() ? QByteArray("[]") : text.toUtf8(), &err);
        if (!doc.isArray()) {
            ctl->toast(tr("Not a macro: %1").arg(err.errorString()), Controller::ToastKind::Error);
            return;
        }
        macros_[current_] = macro::restore(doc.array());
        rebuildRows();
        changed();
    });
    dlg->open();
}

void MacroPage::startRecording(bool append)
{
    if (!recorder_)
        return;
    append_ = append;
    for (auto *b : editButtons_)
        b->hide();
    stop_->show();
    tabs_->setEnabled(false);
    ctl->toast(tr("Recording: type the macro, then press Stop recording"));
    recorder_->start();
}

void MacroPage::stopRecording()
{
    if (recorder_)
        recorder_->stop();
}
