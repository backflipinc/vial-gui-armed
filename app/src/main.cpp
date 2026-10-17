#include "core/appinfo.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(appinfo::kAppName);
    QApplication::setApplicationDisplayName(appinfo::kDisplayName);
    QApplication::setOrganizationDomain("vial.today");
    QApplication::setApplicationVersion(appinfo::version());
    QApplication::setWindowIcon(QIcon(":/icons/icon.svg"));
    QGuiApplication::setDesktopFileName(appinfo::kAppName);

    switch (appinfo::modeFromArgs(QApplication::arguments())) {
    case appinfo::Mode::Agent:
    case appinfo::Mode::LinuxRecorder:
        std::fputs("not implemented yet\n", stderr);
        return 2;
    case appinfo::Mode::Window:
        break;
    }

    QFontDatabase::addApplicationFont(":/resources/base/fonts/ShareTechMono-Regular.ttf");
    QFontDatabase::addApplicationFont(":/resources/base/fonts/Rajdhani-Bold.ttf");

    MainWindow window;
    window.show();
    return app.exec();
}
