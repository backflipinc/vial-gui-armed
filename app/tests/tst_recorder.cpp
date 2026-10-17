#include "keycodes/keycodes.h"
#include "recorder/macrorecorder.h"

#include <QTest>

using S = MacroRecorder::Stroke;

// The Python app's macro_optimize: repeats out, press+release to tap, printable taps to text
class TestRecorder : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        keycodes::KeyboardTraits kb;
        kb.protocol = 6;
        keycodes::Registry::instance().rebuild(kb);
    }

    void optimize()
    {
        QList<S> in = {
            {S::Down, "KC_LSHIFT", {}}, {S::Down, "KC_LSHIFT", {}}, {S::Down, "KC_H", {}}, {S::Up, "KC_H", {}},
            {S::Up, "KC_LSHIFT", {}}, {S::Down, "KC_I", {}}, {S::Up, "KC_I", {}}, {S::Down, "KC_1", {}},
            {S::Up, "KC_1", {}}, {S::Down, "KC_ENTER", {}}, {S::Up, "KC_ENTER", {}}, {S::Down, "KC_A", {}},
            {S::Up, "KC_A", {}},
        };
        auto out = MacroRecorder::optimize(in);
        QCOMPARE(out.size(), 6);
        QCOMPARE(out[0].kind, S::Down);
        QCOMPARE(out[1].kind, S::Tap); // a lone printable tap stays a tap
        QCOMPARE(out[1].keycode, QStringLiteral("KC_H"));
        QCOMPARE(out[2].kind, S::Up);
        QCOMPARE(out[3].kind, S::Text);
        QCOMPARE(out[3].text, QStringLiteral("i1"));
        QCOMPARE(out[4].kind, S::Tap);
        QCOMPARE(out[4].keycode, QStringLiteral("KC_ENTER"));
        QCOMPARE(out[5].keycode, QStringLiteral("KC_A"));

        auto actions = MacroRecorder::toActions({{S::Tap, "KC_A", {}}, {S::Tap, "KC_ENTER", {}}, {S::Text, {}, "x"}});
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions[0].keys, (QStringList{"KC_A", "KC_ENTER"}));
    }
};

QTEST_GUILESS_MAIN(TestRecorder)
#include "tst_recorder.moc"
