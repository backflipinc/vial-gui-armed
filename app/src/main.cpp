#include "core/appinfo.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QIcon>
#include <QTimer>
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

    ui::theme::loadFonts();
    ui::theme::apply();

    MainWindow window;
    window.show();
    // development aid: VIAL_ARMED_SCREENSHOT=file.png[:delay ms] saves the window and quits
    const QString shot = qEnvironmentVariable("VIAL_ARMED_SCREENSHOT");
    if (!shot.isEmpty()) {
        const QStringList parts = shot.split(QLatin1Char(':'));
        QTimer::singleShot(parts.value(1, "3000").toInt(), &window, [&window, path = parts[0]] {
            window.grab().save(path);
            window.quit();
        });
    }
    return app.exec();
}
