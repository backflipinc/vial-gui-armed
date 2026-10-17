#pragma once

#include <QList>

#include <span>

// Static keycode data, generated from the Python app's tables (tables_gen.cpp)
namespace keycodes::tables {

enum class Group {
    Special, Basic, Shifted, Iso, Boot, Modifiers, Quantum, Backlight, Media, MacroBase, MidiBasic, MidiAdvanced,
    // subsets of the above, for the keycode tabs
    BasicNumpad, BasicNav, IsoKr,
};

struct KeycodeDef {
    const char *qmkId;
    const char *label;
    const char *tooltip;
    bool masked;
    const char *printable;
    const char *aliases;         // '|'-separated, besides qmkId
    const char *recorderAliases; // '|'-separated key names the macro recorder sees
    const char *requiresFeature;
};

struct GroupDef {
    Group group;
    const KeycodeDef *defs;
    int count;
};

struct RawCode {
    const char *name;
    int code;
};

struct KeymapLabel {
    const char *qmkId;
    const char *label;
};

struct KeymapDef {
    const char *title;
    const KeymapLabel *labels;
    int count;
};

const QList<GroupDef> &groups();
std::span<const RawCode> rawCodes(int protocol);
const QList<KeymapDef> &keymaps();

}
