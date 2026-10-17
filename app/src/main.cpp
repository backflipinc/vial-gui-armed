#include "core/appinfo.h"
#include "recorder/macrorecorder.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QIcon>
#include <QTimer>
#include <cstdio>
#include <cstring>

int main(int argc, char *argv[])
{
    // runs as root under pkexec, without a display
    if (argc == 2 && std::strcmp(argv[1], "--linux-recorder") == 0)
        return runLinuxRecorderHelper();

    QApplication app(argc, argv);
    QApplication::setApplicationName(appinfo::kAppName);
    QApplication::setApplicationDisplayName(appinfo::kDisplayName);
    QApplication::setOrganizationDomain("vial.today");
    QApplication::setApplicationVersion(appinfo::version());
    QApplication::setWindowIcon(QIcon(":/icons/icon.svg"));
    QGuiApplication::setDesktopFileName(appinfo::kAppName);

    switch (appinfo::modeFromArgs(QApplication::arguments())) {
    case appinfo::Mode::LinuxRecorder:
    case appinfo::Mode::Agent:
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
