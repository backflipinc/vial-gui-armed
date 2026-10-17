#pragma once

#include <QString>

namespace appinfo {

inline constexpr auto kAppName = "vial-armed";
inline constexpr auto kDisplayName = "Vial Armed";
// the Python app's QSettings scope, kept so window state and choices carry over
inline constexpr auto kSettingsOrg = "Vial";
inline constexpr auto kSettingsApp = "Vial Armed";

QString version();

enum class Mode { Window, Agent, LinuxRecorder };
Mode modeFromArgs(const QStringList &args);

}
