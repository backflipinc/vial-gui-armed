#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace paths {

// ~/.config/vial-armed, %APPDATA%\vial-armed, ~/Library/Application Support/vial-armed: shared with the
// Python app (agent settings, caches)
inline QString configDir()
{
#if defined(Q_OS_WIN)
    QString base = qEnvironmentVariable("APPDATA", QDir::homePath());
#elif defined(Q_OS_MACOS)
    QString base = QDir::homePath() + QStringLiteral("/Library/Application Support");
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
#endif
    return base + QStringLiteral("/vial-armed");
}

}
