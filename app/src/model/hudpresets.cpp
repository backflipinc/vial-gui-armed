#include "model/hudpresets.h"

namespace hud {

namespace {

Layout monitor()
{
    auto h = [](int i) { return S_HOST + i; };
    QList<Widget> w = {
        {.type = W_TEXT, .font = 0, .fg = C_ACCENT, .x = 8, .y = 3, .w = 50, .label = "IO//84"},
        {.type = W_TEXT, .font = 0, .fg = C_DIM, .x = 62, .y = 3, .w = 60, .label = "SYS"},
        {.type = W_TEXT, .source = S_TIME, .font = 0, .fg = C_AUTO, .flags = 2, .x = 200, .y = 3, .w = 112},
        {.type = W_FRAME, .fg = C_LINE, .x = 0, .y = 22, .w = 320, .h = 1, .param = FRAME_HLINE},
    };
    struct Row {
        const char *name;
        int load, temp;
    };
    const Row rows[] = {{"CPU", 0, 1}, {"GPU", 3, 4}, {"RAM", 2, -1}};
    for (int r = 0; r < 3; ++r) {
        const int y = 30 + r * 22;
        w.append({.type = W_TEXT, .font = 0, .fg = C_DIM, .x = 8, .y = y, .w = 36, .label = rows[r].name});
        w.append({.type = W_BAR, .source = h(rows[r].load), .fg = C_ACCENT, .flags = F_WARN, .x = 46, .y = y + 1,
                  .w = 150, .h = 13, .hi = 100, .warn = 90, .param = 10});
        w.append({.type = W_TEXT, .source = h(rows[r].load), .font = 0, .fg = C_TEXT, .flags = 2, .x = 200, .y = y,
                  .w = 48, .label = "{}%"});
        if (rows[r].temp >= 0)
            w.append({.type = W_TEXT, .source = h(rows[r].temp), .font = 0, .fg = C_TEXT, .flags = 2 | F_WARN,
                      .x = 252, .y = y, .w = 60, .warn = 85, .label = "{}C"});
    }
    w += QList<Widget>{
        {.type = W_FRAME, .fg = C_DIM, .x = 4, .y = 96, .w = 206, .h = 50, .lo = 5, .param = FRAME_BRACKETS},
        {.type = W_GRAPH, .source = h(0), .fg = C_ACCENT, .flags = F_FILL, .x = 10, .y = 100, .w = 194, .h = 42,
         .hi = 100, .param = 10},
        {.type = W_TEXT, .font = 0, .fg = C_DIM, .x = 218, .y = 100, .w = 24, .label = "DN"},
        {.type = W_TEXT, .source = h(5), .font = 0, .fg = C_TEXT, .flags = 2, .x = 244, .y = 100, .w = 68, .label = "{}M"},
        {.type = W_TEXT, .font = 0, .fg = C_DIM, .x = 218, .y = 122, .w = 24, .label = "UP"},
        {.type = W_TEXT, .source = h(6), .font = 0, .fg = C_TEXT, .flags = 2, .x = 244, .y = 122, .w = 68, .label = "{}M"},
        {.type = W_FRAME, .fg = C_LINE, .x = 0, .y = 150, .w = 320, .h = 1, .param = FRAME_HLINE},
        {.type = W_INDICATOR, .source = S_LINK, .fg = C_AUTO, .flags = F_DOT, .x = 8, .y = 158, .w = 6, .h = 6},
        {.type = W_TEXT, .source = S_LINK, .font = 0, .fg = C_TEXT, .x = 20, .y = 154, .w = 180, .h = 17},
        {.type = W_TEXT, .source = S_LAYER, .font = 0, .fg = C_AUTO, .flags = 2, .x = 204, .y = 154, .w = 108},
    };
    return {w};
}

Layout console()
{
    return {{
        {.type = W_TEXT, .font = 0, .fg = C_ACCENT, .x = 8, .y = 3, .w = 50, .label = "IO//84"},
        {.type = W_TEXT, .font = 0, .fg = C_DIM, .x = 62, .y = 3, .w = 80, .label = "CONSOLE"},
        {.type = W_TEXT, .source = S_TIME, .font = 0, .fg = C_AUTO, .flags = 2, .x = 200, .y = 3, .w = 112},
        {.type = W_FRAME, .fg = C_LINE, .x = 0, .y = 22, .w = 320, .h = 1, .param = FRAME_HLINE},
        {.type = W_TEXTBOX, .source = S_HOST_TXT, .font = 0, .fg = C_TEXT, .flags = F_TAIL, .x = 8, .y = 28, .w = 304,
         .h = 118, .param = 2},
        {.type = W_FRAME, .fg = C_LINE, .x = 0, .y = 150, .w = 320, .h = 1, .param = FRAME_HLINE},
        {.type = W_INDICATOR, .source = S_LINK, .fg = C_AUTO, .flags = F_DOT, .x = 8, .y = 158, .w = 6, .h = 6},
        {.type = W_TEXT, .source = S_LINK, .font = 0, .fg = C_TEXT, .x = 20, .y = 154, .w = 180, .h = 17},
        {.type = W_TEXT, .source = S_BATTERY, .font = 0, .fg = C_AUTO, .flags = 2, .x = 204, .y = 154, .w = 76,
         .label = "{}%"},
        {.type = W_BATTERY, .source = S_BATTERY, .fg = C_ACCENT, .x = 287, .y = 157, .w = 25, .h = 11},
    }};
}

Layout bigClock()
{
    return {{
        {.type = W_FRAME, .fg = C_DIM, .x = 6, .y = 6, .w = 308, .h = 160, .lo = 8, .param = FRAME_BRACKETS},
        {.type = W_TEXT, .source = S_TIME, .font = 2, .fg = C_AUTO, .flags = 1, .x = 10, .y = 30, .w = 300, .h = 46},
        {.type = W_TEXT, .source = S_DATE, .font = 1, .fg = C_DIM, .flags = 1, .x = 10, .y = 86, .w = 300, .h = 22},
        {.type = W_FRAME, .fg = C_LINE, .x = 40, .y = 120, .w = 240, .h = 1, .param = FRAME_HLINE},
        {.type = W_TEXT, .source = S_LAYER, .font = 0, .fg = C_AUTO, .flags = 1, .x = 10, .y = 130, .w = 150},
        {.type = W_TEXT, .source = S_BATTERY, .font = 0, .fg = C_AUTO, .flags = 1, .x = 160, .y = 130, .w = 150,
         .label = "BAT {}%"},
    }};
}

}

const QList<Preset> &presets()
{
    static const QList<Preset> p = {
        {"KEYBOARD DEFAULT", {}},
        {"SYSTEM MONITOR", monitor},
        {"CONSOLE (HOST TEXT 1)", console},
        {"BIG CLOCK", bigClock},
    };
    return p;
}

}
