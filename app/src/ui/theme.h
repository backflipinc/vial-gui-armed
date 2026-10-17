#pragma once

#include <QColor>
#include <QFont>
#include <QString>

// Hi-Tech / Military Black: the design code of the IO Type 84 Pro's screen UI
namespace ui::theme {

inline const QColor BG{"#000000"};
inline const QColor PANEL{"#0a0a0a"};
inline const QColor PANEL2{"#111111"};
inline const QColor KEY{"#070707"};
inline const QColor LINE{"#2a2a2a"};
inline const QColor LINE2{"#3a3a3a"};
// text on black: 4.6:1, readable at small sizes (the 1.x #5c5c5c was 3.1:1)
inline const QColor DIM{"#7a7a7a"};
inline const QColor MID{"#9a9a9a"};
inline const QColor TXT{"#dcdcdc"};
inline const QColor ACC{"#ffaa00"};
inline const QColor ACC_D{"#6b4700"};
inline const QColor ACC_BG{"#1a1100"};
inline const QColor OK{"#2fd05a"};
inline const QColor WARN{"#ff3b2f"};

inline constexpr auto MONO = "Share Tech Mono";
inline constexpr auto HEAD = "Rajdhani";

// spacing grid
inline constexpr int S1 = 4, S2 = 8, S3 = 12, S4 = 16, S5 = 24;

void loadFonts();
QFont mono(int px = 13);
QFont head(int px = 17);
void apply(); // palette + stylesheet on the application
QString css(const QColor &c);

}
