#include "ui/mainwindow.h"

#include "core/appinfo.h"
#include "keycodes/tables.h"
#include "ui/keycodedisplay.h"
#include "ui/pages.h"
#include "ui/sidenav.h"
#include "ui/theme.h"
#include "ui/toast.h"
#include "ui/widgets/keybutton.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

QString square(const QColor &c)
{
    return QStringLiteral("<span style=\"color:%1\">■</span> ").arg(c.name());
}

QString viaStackPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/via_keyboards.json");
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , settings_(appinfo::kSettingsOrg, appinfo::kSettingsApp)
    , entries_(navEntries())
{
    setWindowTitle(appinfo::kDisplayName);
    resize(settings_.value("size", QSize(1280, 860)).toSize());
    const QPoint pos = settings_.value("pos").toPoint();
    if (settings_.contains("pos") && QGuiApplication::screenAt(pos))
        move(pos);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTop());

    auto *middle = new QHBoxLayout;
    middle->setContentsMargins(0, 0, 0, 0);
    middle->setSpacing(0);
    nav_ = new SideNav;
    middle->addWidget(nav_);

    auto *content = new QVBoxLayout;
    content->setContentsMargins(18, 10, 18, 14);
    content->setSpacing(ui::theme::S2);
    auto *crumbRow = new QHBoxLayout;
    crumb_ = new QLabel;
    crumb_->setObjectName("armedCrumb");
    crumbRow->addWidget(crumb_);
    crumbRow->addSpacing(ui::theme::S5);
    subnav_ = new QWidget;
    subnav_->setObjectName("armedSubNav");
    auto *subLayout = new QHBoxLayout(subnav_);
    subLayout->setContentsMargins(0, 0, 0, 0);
    subLayout->setSpacing(0);
    subnavGroup_ = new QButtonGroup(this);
    crumbRow->addWidget(subnav_);
    crumbRow->addStretch(1);
    content->addLayout(crumbRow);
    stack_ = new QStackedWidget;
    body_ = new QStackedWidget;
    body_->addWidget(buildEmpty());
    auto *pageHost = new QWidget;
    auto *pageLayout = new QVBoxLayout(pageHost);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addLayout(content);
    content->addWidget(stack_, 1);
    keycodeTray_ = new KeycodeTray;
    content->addWidget(keycodeTray_, 1);
    body_->addWidget(pageHost);
    middle->addWidget(body_, 1);
    root->addLayout(middle, 1);
    root->addWidget(buildFoot());
    setCentralWidget(central);
    toast_ = new Toast(central);
    ctl_.setDialogParent(this);

    connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            showEntry(row);
    });
    connect(subnavGroup_, &QButtonGroup::idClicked, this, [this](int part) {
        if (currentEntry_ >= 0)
            showEntry(int(visible_.indexOf(currentEntry_)), part);
    });
    for (int i = 0; i < 9; ++i) {
        auto *sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)), this);
        connect(sc, &QShortcut::activated, this, [this, i] {
            if (i < visible_.size() && nav_->isEnabled())
                nav_->setCurrentRow(i);
        });
    }

    connect(&ctl_, &Controller::devicesChanged, this, &MainWindow::onDevicesChanged);
    connect(&ctl_, &Controller::stateChanged, this, &MainWindow::onStateChanged);
    connect(&ctl_, &Controller::keyboardChanged, this, &MainWindow::onKeyboardChanged);
    connect(&ctl_, &Controller::lockChanged, this, &MainWindow::updateStatus);
    connect(&ctl_, &Controller::toastRequested, toast_, &Toast::show);
    connect(&ctl_, &Controller::busyChanged, this, [this](bool busy) {
        devices_->setEnabled(!busy);
        refresh_->setEnabled(!busy);
        nav_->setEnabled(!busy);
        subnav_->setEnabled(!busy);
        menuBar()->setEnabled(!busy);
    });

    QFile via(viaStackPath());
    if (via.open(QIODevice::ReadOnly))
        ctl_.setViaStack(via.readAll());

    buildMenus();
    buildTray();
    onStateChanged();
    rebuildNav();

    if (settings_.value("maximized", false).toBool())
        showMaximized();
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::buildTop()
{
    auto *top = new QWidget;
    top->setObjectName("armedTop");
    top->setAttribute(Qt::WA_StyledBackground);
    auto *l = new QHBoxLayout(top);
    l->setContentsMargins(16, 6, 16, 6);
    l->setSpacing(ui::theme::S3);
    auto *logo = new QLabel("VIAL");
    logo->setObjectName("armedLogo");
    auto *sub = new QLabel("ARMED");
    sub->setObjectName("armedLogoSub");
    l->addWidget(logo);
    l->addSpacing(-4);
    l->addWidget(sub);
    l->addSpacing(ui::theme::S3);

    devices_ = new QComboBox;
    devices_->setMinimumWidth(360);
    devices_->setAccessibleName(tr("Keyboard"));
    connect(devices_, &QComboBox::activated, &ctl_, &Controller::select);
    l->addWidget(devices_, 1);
    l->addStretch(1);

    linkChip_ = new QLabel;
    linkChip_->setObjectName("armedChip");
    linkChip_->setTextFormat(Qt::RichText);
    l->addWidget(linkChip_);

    lockChip_ = new QToolButton;
    lockChip_->setObjectName("armedChip");
    lockChip_->setProperty("lockchip", true);
    lockChip_->setToolTip(tr("Vial unlock state: changing QK_BOOT or macros needs the keyboard unlocked"));
    connect(lockChip_, &QToolButton::clicked, this, [this] {
        if (ctl_.unlocked())
            ctl_.lockKeyboard();
        else
            ctl_.toast(tr("Unlocking arrives with the Keymap page in 2.0"));
    });
    l->addWidget(lockChip_);

    refresh_ = new QPushButton(tr("Refresh"));
    refresh_->setToolTip(tr("Look for keyboards again (F5)"));
    refresh_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refresh_, &QPushButton::clicked, &ctl_, &Controller::refreshDevices);
    l->addWidget(refresh_);
    return top;
}

QWidget *MainWindow::buildEmpty()
{
    empty_ = new QWidget;
    auto *outer = new QVBoxLayout(empty_);
    outer->addStretch(2);
    auto *box = new QVBoxLayout;
    box->setSpacing(ui::theme::S3);
    emptyTitle_ = new QLabel;
    emptyTitle_->setObjectName("armedEmptyTitle");
    emptyTitle_->setAlignment(Qt::AlignCenter);
    emptyText_ = new QLabel;
    emptyText_->setObjectName("armedEmptyText");
    emptyText_->setAlignment(Qt::AlignCenter);
    emptyText_->setWordWrap(true);
    emptyText_->setTextFormat(Qt::RichText);
    emptyText_->setOpenExternalLinks(true);
    emptyText_->setMinimumWidth(420);
    emptyText_->setMaximumWidth(620);
    emptyBusy_ = new QProgressBar;
    emptyBusy_->setRange(0, 0);
    emptyBusy_->setFixedSize(260, 6);
    emptyRetry_ = new QPushButton(tr("Try again"));
    connect(emptyRetry_, &QPushButton::clicked, &ctl_, &Controller::reload);
    box->addWidget(emptyTitle_);
    box->addWidget(emptyText_, 0, Qt::AlignHCenter);
    box->addWidget(emptyBusy_, 0, Qt::AlignHCenter);
    box->addWidget(emptyRetry_, 0, Qt::AlignHCenter);
    outer->addLayout(box);
    outer->addStretch(3);
    return empty_;
}

QWidget *MainWindow::buildFoot()
{
    auto *foot = new QWidget;
    foot->setObjectName("armedFoot");
    foot->setAttribute(Qt::WA_StyledBackground);
    auto *l = new QHBoxLayout(foot);
    l->setContentsMargins(16, 6, 16, 6);
    foot_ = new QLabel;
    foot_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    footLive_ = new QLabel(QStringLiteral("● LIVE"));
    footLive_->setObjectName("armedFootAcc");
    footLive_->setToolTip(tr("Changes go to the keyboard as you make them"));
    l->addWidget(foot_);
    l->addStretch(1);
    l->addWidget(footLive_);
    return foot;
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("File"));
    file->addAction(tr("Load saved layout..."), QKeySequence::Open, this, &MainWindow::loadLayout);
    file->addAction(tr("Save current layout..."), QKeySequence::Save, this, &MainWindow::saveLayout);
    file->addSeparator();
    file->addAction(tr("Sideload VIA JSON..."), this, &MainWindow::sideloadJson);
    file->addAction(tr("Download VIA definitions"), this, &MainWindow::downloadViaStack);
    file->addSeparator();
    closeToTray_ = file->addAction(tr("Minimize to tray on close"));
    closeToTray_->setCheckable(true);
    closeToTray_->setChecked(settings_.value("close_to_tray", true).toBool());
    connect(closeToTray_, &QAction::toggled, this, [this](bool on) { settings_.setValue("close_to_tray", on); });
    closeToTray_->setVisible(QSystemTrayIcon::isSystemTrayAvailable());
    file->addAction(tr("Exit"), QKeySequence(Qt::CTRL | Qt::Key_Q), this, &MainWindow::quit);

    QMenu *layout = menuBar()->addMenu(tr("Keyboard layout"));
    auto *group = new QActionGroup(this);
    const QString selected = settings_.value("keymap", "QWERTY").toString();
    KeycodeDisplay::instance().setKeymap(selected);
    for (const auto &km : keycodes::tables::keymaps()) {
        QAction *a = layout->addAction(QString::fromUtf8(km.title));
        a->setCheckable(true);
        a->setChecked(a->text() == selected);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, a] {
            settings_.setValue("keymap", a->text());
            KeycodeDisplay::instance().setKeymap(a->text());
        });
    }

    QMenu *security = menuBar()->addMenu(tr("Security"));
    security->addAction(tr("Lock"), &ctl_, &Controller::lockKeyboard);
    security->addSeparator();
    security->addAction(tr("Reboot to bootloader"), this, &MainWindow::rebootToBootloader);

    QMenu *about = menuBar()->addMenu(tr("About"));
    about->addAction(tr("About keyboard..."), this, &MainWindow::aboutKeyboard);
    about->addAction(tr("About Vial Armed..."), this, &MainWindow::aboutApp);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    // the window may hide while dialogs come and go; quitting is explicit from here on
    qApp->setQuitOnLastWindowClosed(false);
    auto *menu = new QMenu(this);
    menu->addAction(tr("Show Vial Armed"), this, &MainWindow::showFromTray);
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, &MainWindow::quit);
    tray_ = new QSystemTrayIcon(qApp->windowIcon(), this);
    tray_->setToolTip(appinfo::kDisplayName);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r != QSystemTrayIcon::Trigger && r != QSystemTrayIcon::DoubleClick)
            return;
        if (isVisible() && !isMinimized())
            hideToTray();
        else
            showFromTray();
    });
    connect(qApp, &QGuiApplication::commitDataRequest, this, [this] { quitting_ = true; });
    tray_->show();
}

void MainWindow::onDevicesChanged()
{
    QSignalBlocker block(devices_);
    devices_->clear();
    int cur = -1;
    const auto current = ctl_.current();
    for (int i = 0; i < ctl_.devices().size(); ++i) {
        devices_->addItem(ctl_.devices()[i].title());
        if (current && *current == ctl_.devices()[i])
            cur = i;
    }
    devices_->setCurrentIndex(cur);
    devices_->setEnabled(!ctl_.devices().isEmpty() && !ctl_.busy());
}

void MainWindow::onStateChanged()
{
    onDevicesChanged();
    using S = Controller::State;
    const S s = ctl_.state();
    const bool ready = s == S::Ready;
    body_->setCurrentIndex(ready ? 1 : 0);
    emptyBusy_->setVisible(s == S::Connecting);
    emptyRetry_->setVisible(s == S::Failed);
    switch (s) {
    case S::NoDevice: {
        emptyTitle_->setText(tr("NO KEYBOARD"));
        QString text = tr("Connect a keyboard with Vial or qmk-armed firmware: it shows up here by itself.<br>"
                          "Wireless: plug in the 2.4 GHz dongle and turn the keyboard on.");
#ifdef Q_OS_LINUX
        text += tr("<br><br>On Linux, keyboards need a udev rule to be seen: "
                   "<a href=\"https://get.vial.today/manual/linux-udev.html\" style=\"color:#ffaa00\">"
                   "get.vial.today/manual/linux-udev.html</a>");
#endif
        emptyText_->setText(text);
        break;
    }
    case S::Connecting:
        emptyTitle_->setText(tr("CONNECTING"));
        emptyText_->setText(ctl_.progressText());
        break;
    case S::Failed:
        emptyTitle_->setText(tr("COULD NOT READ THE KEYBOARD"));
        emptyText_->setText(ctl_.error().toHtmlEscaped().replace("\n", "<br>"));
        break;
    case S::Ready:
        break;
    }
    updateStatus();
}

void MainWindow::updateStatus()
{
    using S = Controller::State;
    using namespace ui::theme;
    const auto cur = ctl_.current();
    QString state = "ok";
    QString text;
    switch (ctl_.state()) {
    case S::NoDevice:
        text = square(LINE2) + tr("NO DEVICE");
        state = "";
        break;
    case S::Connecting:
        text = square(ACC) + tr("CONNECTING…");
        state = "busy";
        break;
    case S::Failed:
        text = square(WARN) + tr("ERROR");
        state = "error";
        break;
    case S::Ready:
        text = square(OK) + (cur && cur->isDongle() ? tr("2.4 GHZ LINKED") : tr("USB LINKED"));
        break;
    }
    linkChip_->setText(text);
    linkChip_->setProperty("state", state);
    repolish(linkChip_);

    const KeyboardState *kb = ctl_.keyboard();
    const bool vial = kb && kb->vialProtocol >= 0;
    lockChip_->setVisible(vial);
    lockChip_->setText(ctl_.unlocked() ? QStringLiteral("□ UNLOCKED") : QStringLiteral("■ LOCKED"));
    lockChip_->setProperty("unlocked", ctl_.unlocked());
    repolish(lockChip_);

    QString foot;
    if (kb && vial) {
        QString uid = QStringLiteral("-");
        if (kb->keyboardId) {
            // little-endian bytes, as Vial and Vial Armed 1.x show it
            QByteArray b;
            for (int i = 0; i < 8; ++i)
                b.append(char((*kb->keyboardId >> (8 * i)) & 0xFF));
            uid = QString::fromLatin1(b.toHex()).toUpper();
        }
        foot = tr("VIAL PROTO %1 · %2 LAYERS · UID %3").arg(kb->vialProtocol).arg(kb->layers).arg(uid);
        if (kb->armed)
            foot = QStringLiteral("QMK-ARMED %1 · ").arg(kb->armed->versionString()) + foot;
    } else if (cur) {
        foot = cur->title().toUpper();
    }
    foot_->setText(foot);
    footLive_->setVisible(ctl_.state() == S::Ready && kb);
}

void MainWindow::onKeyboardChanged()
{
    if (currentPage_)
        currentPage_->deactivate();
    currentPage_ = nullptr;
    for (auto &parts : pages_)
        for (Page *p : parts)
            if (p)
                p->deleteLater();
    pages_.clear();
    partOf_.clear();
    keycodeTray_->close();
    keycodeTray_->rebuild();
    rebuildNav();
    updateStatus();
}

void MainWindow::rebuildNav()
{
    const QString was = currentEntry_ >= 0 ? entries_[currentEntry_].title : QString();
    visible_.clear();
    QStringList titles;
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_[i].available(ctl_)) {
            visible_.append(i);
            titles.append(entries_[i].title);
        }
    }
    int row = titles.indexOf(was);
    // development aid, with VIAL_ARMED_SCREENSHOT: open this page first
    static QString wanted = qEnvironmentVariable("VIAL_ARMED_PAGE");
    if (!wanted.isEmpty() && titles.contains(wanted, Qt::CaseInsensitive)) {
        for (int i = 0; i < titles.size(); ++i)
            if (titles[i].compare(wanted, Qt::CaseInsensitive) == 0)
                row = i;
        wanted.clear();
    }
    if (row < 0 && !titles.isEmpty())
        row = 0;
    currentEntry_ = -1;
    nav_->setEntries(titles, row);
    if (row >= 0)
        showEntry(row);
    else {
        crumb_->clear();
        subnav_->hide();
    }
}

Page *MainWindow::page(int entry, int part)
{
    auto &parts = pages_[entry];
    if (parts.isEmpty())
        parts = QList<Page *>(entries_[entry].parts.size(), nullptr);
    if (!parts[part]) {
        parts[part] = entries_[entry].parts[part].make(&ctl_);
        stack_->addWidget(parts[part]);
        parts[part]->keyboardChanged();
    }
    return parts[part];
}

void MainWindow::showEntry(int visibleIndex, int part)
{
    if (visibleIndex < 0 || visibleIndex >= visible_.size())
        return;
    const int entry = visible_[visibleIndex];
    const NavEntry &e = entries_[entry];
    if (part < 0)
        part = partOf_.value(entry, 0);
    partOf_[entry] = part;

    crumb_->setText(e.title.toUpper());
    for (auto *b : subnavGroup_->buttons()) {
        subnavGroup_->removeButton(b);
        b->deleteLater();
    }
    subnav_->setVisible(e.parts.size() > 1);
    if (e.parts.size() > 1) {
        for (int i = 0; i < e.parts.size(); ++i) {
            auto *b = new QPushButton(e.parts[i].title.toUpper());
            b->setCheckable(true);
            b->setChecked(i == part);
            subnavGroup_->addButton(b, i);
            subnav_->layout()->addWidget(b);
        }
    }

    Page *p = page(entry, part);
    if (p != currentPage_) {
        keycodeTray_->close();
        if (currentPage_)
            currentPage_->deactivate();
        stack_->setCurrentWidget(p);
        currentPage_ = p;
        p->activate();
    }
    currentEntry_ = entry;
}

void MainWindow::saveLayout()
{
    const KeyboardState *kb = ctl_.keyboard();
    if (!kb)
        return;
    QString path = QFileDialog::getSaveFileName(this, tr("Save layout"), settings_.value("layout_dir").toString(),
                                                tr("Vial layout (*.vil)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".vil"))
        path += ".vil";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(kb->saveLayout()) < 0) {
        ctl_.toast(tr("Could not save %1: %2").arg(path, f.errorString()), Controller::ToastKind::Error);
        return;
    }
    settings_.setValue("layout_dir", QFileInfo(path).absolutePath());
    ctl_.toast(tr("Layout saved to %1").arg(QFileInfo(path).fileName()), Controller::ToastKind::Ok);
}

void MainWindow::loadLayout()
{
    if (!ctl_.keyboard())
        return;
    QString path = QFileDialog::getOpenFileName(this, tr("Load layout"), settings_.value("layout_dir").toString(),
                                                tr("Vial layout (*.vil)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ctl_.toast(tr("Could not open %1: %2").arg(path, f.errorString()), Controller::ToastKind::Error);
        return;
    }
    settings_.setValue("layout_dir", QFileInfo(path).absolutePath());
    ctl_.restoreLayout(f.readAll(), [this](bool ok, const QString &msg) {
        ctl_.toast(msg, ok ? Controller::ToastKind::Ok : Controller::ToastKind::Error);
    });
}

void MainWindow::sideloadJson()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Sideload VIA JSON"), {}, tr("VIA definition (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    QString err;
    if (!f.open(QIODevice::ReadOnly) || !ctl_.sideload(f.readAll(), &err))
        ctl_.toast(err.isEmpty() ? f.errorString() : err, Controller::ToastKind::Error);
}

void MainWindow::downloadViaStack()
{
    auto *net = new QNetworkAccessManager(this);
    ctl_.toast(tr("Downloading VIA definitions…"));
    QNetworkReply *reply = net->get(QNetworkRequest(QUrl("https://github.com/vial-kb/via-keymap-precompiled/raw/main/via_keyboard_stack.json")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, net] {
        reply->deleteLater();
        net->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            ctl_.toast(tr("Download failed: %1").arg(reply->errorString()), Controller::ToastKind::Error);
            return;
        }
        QByteArray data = reply->readAll();
        QDir().mkpath(QFileInfo(viaStackPath()).absolutePath());
        QFile f(viaStackPath());
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
        ctl_.setViaStack(data);
        ctl_.toast(tr("VIA definitions updated"), Controller::ToastKind::Ok);
    });
}

void MainWindow::rebootToBootloader()
{
    KeyboardState *kb = ctl_.keyboard();
    if (!kb || !ctl_.session())
        return;
    if (!ctl_.unlocked()) {
        ctl_.toast(tr("Unlock the keyboard first"), Controller::ToastKind::Error);
        return;
    }
    ctl_.session()->run(hid::Priority::Interactive, this, [](hid::Link &link) { vial::jumpToBootloader(link); },
                        [](hid::Result<hid::Unit>) {});
}

void MainWindow::aboutKeyboard()
{
    const KeyboardState *kb = ctl_.keyboard();
    const auto cur = ctl_.current();
    if (!cur)
        return;
    QString text = QStringLiteral("<b>%1</b><br>").arg(cur->title().toHtmlEscaped());
    text += tr("VID:PID %1:%2<br>").arg(cur->vid, 4, 16, QLatin1Char('0')).arg(cur->pid, 4, 16, QLatin1Char('0'));
    if (kb) {
        text += tr("VIA protocol %1, Vial protocol %2<br>").arg(kb->viaProtocol).arg(kb->vialProtocol);
        if (kb->armed)
            text += tr("qmk-armed %1, features 0x%2<br>").arg(kb->armed->versionString()).arg(kb->armed->features, 0, 16);
        text += tr("%1 layers, matrix %2x%3").arg(kb->layers).arg(kb->def.rows).arg(kb->def.cols);
    }
    QMessageBox::about(this, tr("About keyboard"), text);
}

void MainWindow::aboutApp()
{
    QMessageBox::about(this, tr("About Vial Armed"),
                       tr("<b>Vial Armed %1</b><br>based on <a href=\"https://get.vial.today/\">Vial</a><br><br>"
                          "Qt %2<br><br>Licensed under the terms of the<br>GNU General Public License (version 2 or later)"
                          "<br><br><a href=\"https://github.com/backflipinc/vial-gui-armed\">github.com/backflipinc/vial-gui-armed</a>")
                           .arg(appinfo::version(), QString::fromLatin1(qVersion())));
}

void MainWindow::hideToTray()
{
    // a hidden page would keep polling the keyboard
    if (currentPage_)
        currentPage_->deactivate();
    hide();
}

void MainWindow::showFromTray()
{
    const bool wasHidden = !isVisible();
    if (isMaximized())
        showMaximized();
    else
        showNormal();
    raise();
    activateWindow();
    if (wasHidden && currentPage_)
        currentPage_->activate();
}

void MainWindow::quit()
{
    quitting_ = true;
    close();
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    settings_.setValue("size", size());
    settings_.setValue("pos", pos());
    settings_.setValue("maximized", isMaximized());
    if (tray_ && !quitting_ && closeToTray_->isChecked()) {
        hideToTray();
        e->ignore();
        return;
    }
    if (tray_) {
        tray_->hide();
        qApp->quit();
    }
    e->accept();
}
