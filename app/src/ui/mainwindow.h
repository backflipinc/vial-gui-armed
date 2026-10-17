#pragma once

#include "app/controller.h"
#include "ui/page.h"

#include <QHash>
#include <QMainWindow>
#include <QSettings>

class QAction;
class QButtonGroup;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QToolButton;
class SideNav;
class Toast;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    Controller *controller() { return &ctl_; }
    void showFromTray();
    void quit();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildTop();
    QWidget *buildEmpty();
    QWidget *buildFoot();
    void buildMenus();
    void buildTray();

    void onDevicesChanged();
    void onStateChanged();
    void onKeyboardChanged();
    void updateStatus();
    void rebuildNav();
    void showEntry(int visibleIndex, int part = -1);
    Page *page(int entry, int part);
    void hideToTray();

    void saveLayout();
    void loadLayout();
    void sideloadJson();
    void downloadViaStack();
    void rebootToBootloader();
    void aboutKeyboard();
    void aboutApp();

    Controller ctl_;
    QSettings settings_;
    QList<NavEntry> entries_;
    QList<int> visible_;           // entries_ indexes shown in the nav
    QHash<int, QList<Page *>> pages_; // entry -> parts, built on demand
    QHash<int, int> partOf_;          // entry -> last shown part
    int currentEntry_ = -1;
    Page *currentPage_ = nullptr;

    QComboBox *devices_ = nullptr;
    QLabel *linkChip_ = nullptr;
    QToolButton *lockChip_ = nullptr;
    QPushButton *refresh_ = nullptr;
    SideNav *nav_ = nullptr;
    QLabel *crumb_ = nullptr;
    QWidget *subnav_ = nullptr;
    QButtonGroup *subnavGroup_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QStackedWidget *body_ = nullptr;
    QWidget *empty_ = nullptr;
    QLabel *emptyTitle_ = nullptr;
    QLabel *emptyText_ = nullptr;
    QProgressBar *emptyBusy_ = nullptr;
    QPushButton *emptyRetry_ = nullptr;
    QLabel *foot_ = nullptr;
    QLabel *footLive_ = nullptr;
    Toast *toast_ = nullptr;
    QAction *closeToTray_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    bool quitting_ = false;
};
