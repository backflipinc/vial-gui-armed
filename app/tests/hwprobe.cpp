// Read-only smoke test against a real keyboard: hwprobe [--all]
#include "hid/devices.h"
#include "hid/session.h"
#include "protocol/armed.h"
#include "protocol/vial.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
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
    session.run(hid::Priority::Interactive, &app, [](hid::Link &link) {
        QStringList out;
        int via = vial::viaProtocol(link);
        auto id = vial::identity(link);
        QByteArray def = vial::definition(link);
        auto json = QJsonDocument::fromJson(def).object();
        int layers = vial::layerCount(link);
        auto m = json.value("matrix").toObject();
        int rows = m.value("rows").toInt(), cols = m.value("cols").toInt();
        QByteArray keymap = vial::keymapBuffer(link, layers * rows * cols * 2);
        auto macro = vial::macroInfo(link);
        auto counts = vial::dynamicCounts(link);
        out << QStringLiteral("via %1, vial %2, id %3").arg(via).arg(id.vialProtocol).arg(id.keyboardId, 16, 16, QLatin1Char('0'));
        out << QStringLiteral("definition %1 bytes, name %2, %3 layers %4x%5, keymap %6 bytes")
                   .arg(def.size()).arg(json.value("name").toString()).arg(layers).arg(rows).arg(cols).arg(keymap.size());
        out << QStringLiteral("macros %1 / %2 bytes, tap dance %3, combos %4, key overrides %5, alt repeat %6")
                   .arg(macro.count).arg(macro.memory).arg(counts.tapDance).arg(counts.combo)
                   .arg(counts.keyOverride).arg(counts.altRepeatKey);
        if (auto info = armed::probe(link, id.vialProtocol, id.keyboardId)) {
            out << QStringLiteral("qmk-armed %1, features %2, %3 effects, %4 LEDs, %5 values")
                       .arg(info->versionString()).arg(info->features, 0, 16).arg(info->effects.size())
                       .arg(info->leds.size()).arg(info->values.size());
            if (auto clock = armed::clockGet(link))
                out << QStringLiteral("clock %1").arg(clock->toString(Qt::ISODate));
        }
        return out;
    }, [&](hid::Result<QStringList> r) {
        if (!r.ok())
            std::printf("error: %s\n", qPrintable(r.error));
        else
            for (const auto &l : *r.value)
                std::printf("%s\n", qPrintable(l));
        std::printf("session: %lld ms\n", t.elapsed());
        app.exit(r.ok() ? 0 : 1);
    });
    return app.exec();
}
