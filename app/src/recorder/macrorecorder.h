#pragma once

#include "model/macro.h"

#include <QObject>
#include <QStringList>

// Records keystrokes from the computer's keyboard for a macro: Linux through a root helper (pkexec) reading
// evdev, Windows through a low-level hook. Key names are the macro recorder aliases of the keycode table.
class MacroRecorder : public QObject {
    Q_OBJECT

public:
    static MacroRecorder *create(QObject *parent); // nullptr where recording isn't supported
    virtual void start() = 0;
    virtual void stop() = 0;

    struct Stroke {
        enum Kind { Down, Up, Tap, Text } kind;
        QString keycode; // Down / Up / Tap
        QString text;    // Text
    };
    // Python app's macro_optimize: repeats out, down+up to taps, printable taps to text
    static QList<Stroke> optimize(QList<Stroke> strokes);
    static macro::Macro toActions(const QList<Stroke> &strokes);

signals:
    void keystroke(bool down, const QString &alias);
    void stopped();
    void failed(const QString &error);

protected:
    using QObject::QObject;
};

// the --linux-recorder helper: prints "down:<name>" / "up:<name>" lines until "q" arrives on stdin
int runLinuxRecorderHelper();
