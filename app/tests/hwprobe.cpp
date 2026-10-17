// Read-only smoke test against a real keyboard: hwprobe [file.vil]
#include "hid/devices.h"
#include "hid/session.h"
#include "model/keyboard.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString vilPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    QElapsedTimer t;
    t.start();
    auto devices = hid::discover({});
    std::printf("discover: %d device(s) in %lld ms\n", int(devices.size()), t.elapsed());
    for (const auto &d : devices)
        std::printf("  %s  %s\n", qPrintable(d.title()), d.path.constData());
    if (devices.isEmpty())
        return 1;

    hid::Session session(devices.first());
    t.restart();
    session.run(hid::Priority::Interactive, &app, [](hid::Link &link) { return loadRaw(link, std::nullopt); },
                [&](hid::Result<RawKeyboard> r) {
        if (!r.ok()) {
            std::printf("error: %s\n", qPrintable(r.error));
            app.exit(1);
            return;
        }
        std::printf("load: %lld ms\n", t.elapsed());
        auto state = KeyboardState::fromRaw(*r.value);
        if (auto *err = std::get_if<QString>(&state)) {
            std::printf("error: %s\n", qPrintable(*err));
            app.exit(1);
            return;
        }
        const KeyboardState &kb = std::get<KeyboardState>(state);
        std::printf("%s: via %d vial %lld, %d layers, %d keys, %d encoders, %d macros, %d/%d/%d/%d dynamic, "
                    "%d settings, armed %s\n",
                    qPrintable(kb.def.name), kb.viaProtocol, kb.vialProtocol, kb.layers, int(kb.def.keys.size()),
                    kb.def.encoderCount, kb.macroInfo.count, int(kb.tapDances.size()), int(kb.combos.size()),
                    int(kb.keyOverrides.size()), int(kb.altRepeatKeys.size()), int(kb.settings.size()),
                    kb.armed ? qPrintable(kb.armed->versionString()) : "no");
        std::printf("layer 0 row 0: ");
        for (int c = 0; c < kb.def.cols; ++c)
            std::printf("%s ", qPrintable(kb.key(0, 0, c)));
        std::printf("\n");
        if (!vilPath.isEmpty()) {
            QFile f(vilPath);
            f.open(QIODevice::WriteOnly);
            f.write(kb.saveLayout());
            std::printf("wrote %s\n", qPrintable(vilPath));
        }
        app.exit(0);
    });
    return app.exec();
}
