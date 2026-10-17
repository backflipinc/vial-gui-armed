#include "core/appinfo.h"
#include "core/version.h"

#include <QStringList>

namespace appinfo {

QString version()
{
    return QStringLiteral(VIAL_ARMED_VERSION);
}

Mode modeFromArgs(const QStringList &args)
{
    if (args.size() == 2 && args[1] == QLatin1String("--agent"))
        return Mode::Agent;
    if (args.size() == 2 && args[1] == QLatin1String("--linux-recorder"))
        return Mode::LinuxRecorder;
    return Mode::Window;
}

}
