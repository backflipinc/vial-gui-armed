// Where a connect's time goes: reads the first keyboard found the way the app does, counting requests and
// round-trip time per command. --entries also reads the tap dance / combo / key override / alt repeat key lists,
// --cache uses the app's definition cache.
#include "core/paths.h"
#include "hid/devices.h"
#include "hid/link.h"
#include "model/keyboard.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMap>

#include <algorithm>
#include <cstdio>

namespace {

struct Stat {
    int n = 0;
    qint64 ns = 0;
};

QMap<QString, Stat> stats;
QElapsedTimer clk;
QString current;
qint64 sentAt = 0;

QString command(const QByteArray &r)
{
    const quint8 a = quint8(r.at(0)), b = quint8(r.size() > 1 ? r.at(1) : 0), c = quint8(r.size() > 2 ? r.at(2) : 0);
    if (a == 0xFE)
        return QStringLiteral("vial %1").arg(b, 2, 16, QLatin1Char('0')) + (b == 0x0D ? QStringLiteral(" op%1").arg(c) : QString());
    if (a == 0xAD)
        return QStringLiteral("armed %1").arg(b) + (b == 1 ? QStringLiteral(" list%1").arg(c) : QString());
    return QStringLiteral("via %1").arg(a, 2, 16, QLatin1Char('0'));
}

class Counting : public hid::RawHid {
public:
    explicit Counting(std::unique_ptr<hid::RawHid> d)
        : d_(std::move(d))
    {
    }
    int write(const QByteArray &r) override
    {
        current = command(r);
        sentAt = clk.nsecsElapsed();
        stats[current].n++;
        return d_->write(r);
    }
    QByteArray read(int timeoutMs) override
    {
        QByteArray r = d_->read(timeoutMs);
        if (!r.isEmpty())
            stats[current].ns += clk.nsecsElapsed() - sentAt;
        return r;
    }

private:
    std::unique_ptr<hid::RawHid> d_;
};

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const bool entries = app.arguments().contains(QLatin1String("--entries"));
    const auto devices = hid::discover({});
    if (devices.isEmpty()) {
        std::fprintf(stderr, "no keyboard found\n");
        return 1;
    }
    const auto d = devices.first();
    std::printf("%s\n", qPrintable(d.title()));
    hid::Link link(std::make_unique<Counting>(hid::HidapiDevice::open(d.path)), d.isDongle());
    clk.start();
    const QString cache = app.arguments().contains(QLatin1String("--cache"))
        ? paths::configDir() + QStringLiteral("/cache") : QString();
    RawKeyboard raw = loadRaw(link, std::nullopt, 0, cache);
    const qint64 keymap = clk.elapsed();
    loadParts(link, raw, entries ? AllParts : BackgroundParts);
    const qint64 total = clk.elapsed();
    int n = 0;
    for (const Stat &s : std::as_const(stats))
        n += s.n;
    std::printf("keymap after %lld ms\n", keymap);
    std::printf("load %lld ms, %d requests, %.1f ms per request\n", total, n, double(total) / std::max(n, 1));
    QList<std::pair<qint64, QString>> order;
    for (auto it = stats.begin(); it != stats.end(); ++it)
        order.append({it->ns, it.key()});
    std::sort(order.rbegin(), order.rend());
    for (const auto &[ns, k] : order)
        std::printf("  %-18s %4d req %6.0f ms  (%.1f ms each)\n", qPrintable(k), stats[k].n, ns / 1e6,
                    ns / 1e6 / stats[k].n);
    return 0;
}
