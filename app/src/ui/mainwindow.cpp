#include "ui/mainwindow.h"

#include "core/appinfo.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , settings_(appinfo::kSettingsOrg, appinfo::kSettingsApp)
{
    setWindowTitle(appinfo::kDisplayName);
    resize(settings_.value("size", QSize(1000, 820)).toSize());
    const QPoint pos = settings_.value("pos").toPoint();
    if (settings_.contains("pos") && QGuiApplication::screenAt(pos))
        move(pos);

    auto *placeholder = new QLabel(QStringLiteral("Vial Armed %1").arg(appinfo::version()), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    settings_.setValue("size", size());
    settings_.setValue("pos", pos());
    settings_.setValue("maximized", isMaximized());
    e->accept();
}
