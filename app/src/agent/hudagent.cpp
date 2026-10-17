#include "agent/hudagent.h"

#include "core/bytes.h"
#include "core/paths.h"
#include "hid/devices.h"
#include "hid/link.h"
#include "protocol/armed.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStorageInfo>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <cstdio>

#ifdef Q_OS_UNIX
#include <cstdlib>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace agent {

const QList<Metric> &metrics()
{
    static const QList<Metric> m = {
        {"cpu", "CPU LOAD", "%", 0, 0, 100},
        {"cpu_temp", "CPU TEMP", "C", 0, 30, 100},
        {"ram", "RAM USED", "%", 0, 0, 100},
        {"ram_gb", "RAM USED GB", "G", 1, 0, 64},
        {"swap", "SWAP USED", "%", 0, 0, 100},
        {"gpu", "GPU LOAD", "%", 0, 0, 100},
        {"gpu_temp", "GPU TEMP", "C", 0, 30, 100},
        {"vram", "VRAM USED", "%", 0, 0, 100},
        {"net_down", "NET DOWN MB/S", "M", 1, 0, 100},
        {"net_up", "NET UP MB/S", "M", 1, 0, 100},
        {"disk", "DISK / USED", "%", 0, 0, 100},
        {"load", "LOAD AVERAGE", "", 2, 0, 16},
        {"battery", "PC BATTERY", "%", 0, 0, 100},
        {"command", "COMMAND", "", 0, 0, 100},
    };
    return m;
}

const Metric *metric(const QString &key)
{
    for (const auto &m : metrics())
        if (m.key == key)
            return &m;
    return nullptr;
}

const QList<QPair<QString, QString>> kTextExamples = {
    {"NOW PLAYING", "playerctl metadata --format '{{ artist }} - {{ title }}'"},
    {"UPTIME", "uptime -p"},
    {"WEATHER", "curl -s 'wttr.in/?format=%C+%t'"},
    {"LAST LOG LINES", "journalctl -n 4 -o cat --no-pager"},
};

QString configPath()
{
    return paths::configDir() + QStringLiteral("/hud-agent.json");
}

Config Config::defaults()
{
    Config c;
    for (const char *m : {"cpu", "cpu_temp", "ram", "gpu", "gpu_temp", "net_down", "net_up", "disk"})
        c.values.append({m, {}, 1, metric(m)->dec});
    while (c.values.size() < HOST_VALUES)
        c.values.append(ValueSlot{});
    for (int i = 0; i < HOST_TEXTS; ++i)
        c.texts.append(TextSlot{});
    return c;
}

Config Config::load()
{
    Config c = defaults();
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return c;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray values = o.value("values").toArray();
    for (int i = 0; i < values.size() && i < HOST_VALUES; ++i) {
        const QJsonObject s = values[i].toObject();
        if (s.contains("metric"))
            c.values[i].metric = s.value("metric").toString();
        if (s.contains("command"))
            c.values[i].command = s.value("command").toString();
        if (s.contains("interval"))
            c.values[i].interval = s.value("interval").toDouble(1);
        if (s.contains("dec"))
            c.values[i].dec = s.value("dec").toInt();
    }
    const QJsonArray texts = o.value("texts").toArray();
    for (int i = 0; i < texts.size() && i < HOST_TEXTS; ++i) {
        const QJsonObject s = texts[i].toObject();
        if (s.contains("command"))
            c.texts[i].command = s.value("command").toString();
        if (s.contains("interval"))
            c.texts[i].interval = s.value("interval").toDouble(5);
    }
    return c;
}

void Config::save() const
{
    QJsonArray v, t;
    for (const auto &s : values)
        v.append(QJsonObject{{"metric", s.metric}, {"command", s.command}, {"interval", s.interval}, {"dec", s.dec}});
    for (const auto &s : texts)
        t.append(QJsonObject{{"command", s.command}, {"interval", s.interval}});
    QDir().mkpath(paths::configDir());
    QSaveFile f(configPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"values", v}, {"texts", t}}).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

QString slotName(const Config &cfg, int slot)
{
    const auto &s = cfg.values[slot];
    if (s.metric == QLatin1String("command")) {
        const QString first = s.command.trimmed().section(QLatin1Char(' '), 0, 0);
        return QStringLiteral("CMD ") + (first.isEmpty() ? QStringLiteral("?") : first);
    }
    if (const Metric *m = metric(s.metric))
        return m->name;
    return QStringLiteral("HOST %1").arg(slot + 1);
}

QString textName(const Config &cfg, int slot)
{
    const QString cmd = cfg.texts[slot].command.trimmed();
    return cmd.isEmpty() ? QStringLiteral("HOST TEXT %1").arg(slot + 1) : QStringLiteral("CMD ") + cmd.section(QLatin1Char(' '), 0, 0);
}

QString toAscii(const QString &text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;?]*[ -/]*[@-~]"));
    static const QHash<QChar, QString> ru = [] {
        const QString from = QStringLiteral("абвгдеёжзийклмнопрстуфхцчшщъыьэюя");
        const QStringList to = {"a", "b", "v", "g", "d", "e", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
                                "r", "s", "t", "u", "f", "h", "ts", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya"};
        QHash<QChar, QString> h;
        for (int i = 0; i < from.size(); ++i)
            h.insert(from[i], to[i]);
        return h;
    }();
    QString in = text;
    in.remove(ansi);
    QString out;
    for (char32_t c : in.toUcs4()) {
        const QChar ch = c < 0x10000 ? QChar(char16_t(c)) : QChar();
        if (c == '\n' || (c >= ' ' && c <= '~'))
            out += ch;
        else if (c == '\t')
            out += QLatin1Char(' ');
        else if (c < 0x10000 && ru.contains(ch.toLower())) {
            const QString t = ru.value(ch.toLower());
            out += ch.isUpper() ? t.toUpper() : t;
        } else if (c == U'\u00b0')
            continue;
        else if (c == U'\u2013' || c == U'\u2014')
            out += QLatin1Char('-');
        else if (c == U'\u2018' || c == U'\u2019')
            out += QLatin1Char('\'');
        else if (c == U'\u201c' || c == U'\u201d' || c == U'\u00ab' || c == U'\u00bb')
            out += QLatin1Char('"');
        else if (c >= ' ')
            out += QLatin1Char('?');
    }
    return out;
}

std::optional<double> firstNumber(const QString &text)
{
    static const QRegularExpression number(QStringLiteral("[-+]?\\d+(?:[.,]\\d+)?"));
    const auto m = number.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    return m.captured(0).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
}

// ---- metrics

namespace {

std::optional<QString> readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(f.readAll());
}

std::optional<double> hwmonTemp(const QStringList &names, const QStringList &labels)
{
    QDir base(QStringLiteral("/sys/class/hwmon"));
    for (const QString &h : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString d = base.filePath(h);
        if (!names.contains(readFile(d + "/name").value_or(QString()).trimmed()))
            continue;
        QStringList inputs = QDir(d).entryList({"temp*_input"}, QDir::Files, QDir::Name);
        QString pick;
        for (const QString &f : inputs) {
            if (labels.contains(readFile(d + "/" + QString(f).replace("_input", "_label")).value_or(QString()).trimmed())) {
                pick = f;
                break;
            }
        }
        if (pick.isEmpty() && !inputs.isEmpty())
            pick = inputs.first();
        if (!pick.isEmpty()) {
            bool ok = false;
            const double v = readFile(d + "/" + pick).value_or(QString()).trimmed().toLongLong(&ok);
            if (ok)
                return v / 1000;
        }
    }
    return std::nullopt;
}

QHash<QString, qint64> meminfo()
{
    QHash<QString, qint64> m;
    for (const QString &line : readFile("/proc/meminfo").value_or(QString()).split('\n')) {
        const QString k = line.section(':', 0, 0);
        const QStringList v = line.section(':', 1).split(' ', Qt::SkipEmptyParts);
        if (!k.isEmpty())
            m.insert(k, v.isEmpty() ? 0 : v.first().toLongLong());
    }
    return m;
}

}

Sampler::Sampler()
{
    clock_.start();
}

Sampler::~Sampler()
{
    if (nvidiaProc_)
        nvidiaProc_->kill();
}

QString Sampler::amdGpu() const
{
    QDir base(QStringLiteral("/sys/class/drm"));
    static const QRegularExpression card(QStringLiteral("^card\\d+$"));
    for (const QString &c : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!card.match(c).hasMatch())
            continue;
        const QString dev = base.filePath(c) + "/device";
        if (QFile::exists(dev + "/gpu_busy_percent"))
            return dev;
    }
    return {};
}

std::optional<QList<double>> Sampler::nvidia()
{
    if (nvidiaOk_ == false)
        return std::nullopt;
    // asked in the background; the answer arrives by the next sample
    if (!nvidiaProc_ || nvidiaProc_->state() == QProcess::NotRunning) {
        if (nvidiaProc_ && nvidiaProc_->exitStatus() == QProcess::NormalExit && nvidiaProc_->exitCode() == 0) {
            const QStringList parts = QString::fromUtf8(nvidiaProc_->readAllStandardOutput()).split('\n').value(0).split(',');
            QList<double> v;
            for (const QString &p : parts)
                v.append(p.trimmed().toDouble());
            if (v.size() >= 4) {
                nvidia_ = v;
                nvidiaOk_ = true;
            }
        }
        if (!nvidiaProc_) {
            nvidiaProc_ = std::make_unique<QProcess>();
            QObject::connect(nvidiaProc_.get(), &QProcess::errorOccurred, [this](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart && !nvidiaOk_)
                    nvidiaOk_ = false;
            });
        }
        nvidiaProc_->start(QStringLiteral("nvidia-smi"),
                           {"--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
                            "--format=csv,noheader,nounits"});
    }
    return nvidia_;
}

std::optional<std::pair<double, double>> Sampler::net()
{
    if (netDone_)
        return netRates_;
    netDone_ = true;
    netRates_.reset();
    const auto text = readFile("/proc/net/dev");
    if (!text)
        return std::nullopt;
    quint64 rx = 0, tx = 0;
    const QStringList lines = text->split('\n');
    for (int i = 2; i < lines.size(); ++i) {
        const QString name = lines[i].section(':', 0, 0).trimmed();
        if (name.isEmpty() || name == QLatin1String("lo"))
            continue;
        const QStringList f = lines[i].section(':', 1).split(' ', Qt::SkipEmptyParts);
        if (f.size() > 8) {
            rx += f[0].toULongLong();
            tx += f[8].toULongLong();
        }
    }
    const qint64 now = clock_.elapsed();
    auto prev = prevNet_;
    prevNet_ = std::tuple{now, rx, tx};
    if (prev && now > std::get<0>(*prev)) {
        const double dt = (now - std::get<0>(*prev)) / 1000.0;
        netRates_ = std::pair{(double(rx) - double(std::get<1>(*prev))) / dt / 1e6,
                              (double(tx) - double(std::get<2>(*prev))) / dt / 1e6};
    }
    return netRates_;
}

std::optional<double> Sampler::read(const QString &key)
{
    if (key == "cpu") {
#ifdef Q_OS_WIN
        FILETIME idle, kernel, user;
        if (!GetSystemTimes(&idle, &kernel, &user))
            return std::nullopt;
        auto u64 = [](FILETIME f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        const quint64 i = u64(idle), t = u64(kernel) + u64(user);
        const quint64 pi = prevIdle_, pt = prevTotal_;
        prevIdle_ = i;
        prevTotal_ = t;
        if (!pt || t == pt)
            return std::nullopt;
        return 100.0 * (1 - double(i - pi) / double(t - pt));
#else
        const auto stat = readFile("/proc/stat");
        if (!stat)
            return std::nullopt;
        const QStringList f = stat->section('\n', 0, 0).split(' ', Qt::SkipEmptyParts).mid(1);
        quint64 total = 0;
        for (int i = 0; i < 8 && i < f.size(); ++i)
            total += f[i].toULongLong();
        const quint64 idle = f.value(3).toULongLong() + (f.size() > 4 ? f[4].toULongLong() : 0);
        auto prev = prevCpu_;
        prevCpu_ = std::pair{idle, total};
        if (!prev || total == prev->second)
            return std::nullopt;
        return 100.0 * (1 - double(idle - prev->first) / double(total - prev->second));
#endif
    }
    if (key == "cpu_temp") {
        auto t = hwmonTemp({"k10temp", "zenpower", "coretemp", "cpu_thermal", "soc_thermal"}, {"Tctl", "Tdie", "Package id 0"});
        if (!t) {
            bool ok = false;
            const double z = readFile("/sys/class/thermal/thermal_zone0/temp").value_or(QString()).trimmed().toLongLong(&ok);
            if (ok)
                t = z / 1000;
        }
        return t;
    }
    if (key == "ram" || key == "ram_gb" || key == "swap") {
#ifdef Q_OS_WIN
        MEMORYSTATUSEX m{};
        m.dwLength = sizeof(m);
        if (!GlobalMemoryStatusEx(&m))
            return std::nullopt;
        if (key == "ram")
            return double(m.dwMemoryLoad);
        if (key == "ram_gb")
            return double(m.ullTotalPhys - m.ullAvailPhys) / (1ull << 30);
        return std::nullopt;
#else
        const auto m = meminfo();
        if (key == "swap") {
            if (!m.value("SwapTotal"))
                return m.isEmpty() ? std::nullopt : std::optional<double>(0.0);
            return 100.0 * (m.value("SwapTotal") - m.value("SwapFree")) / m.value("SwapTotal");
        }
        if (!m.contains("MemTotal"))
            return std::nullopt;
        const double used = m.value("MemTotal") - m.value("MemAvailable");
        return key == "ram" ? 100.0 * used / m.value("MemTotal") : used / (1 << 20);
#endif
    }
    if (key == "gpu" || key == "gpu_temp" || key == "vram") {
        const QString dev = amdGpu();
        if (!dev.isEmpty()) {
            if (key == "gpu")
                return readFile(dev + "/gpu_busy_percent").value_or(QString()).trimmed().toDouble();
            if (key == "gpu_temp") {
                for (const QString &h : QDir(dev + "/hwmon").entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                    if (auto v = readFile(dev + "/hwmon/" + h + "/temp1_input"))
                        return v->trimmed().toDouble() / 1000;
            }
            if (key == "vram") {
                const auto used = readFile(dev + "/mem_info_vram_used"), total = readFile(dev + "/mem_info_vram_total");
                if (used && total && total->trimmed().toDouble() > 0)
                    return 100.0 * used->trimmed().toDouble() / total->trimmed().toDouble();
            }
        }
        const auto n = nvidia();
        if (!n)
            return std::nullopt;
        if (key == "gpu")
            return (*n)[0];
        if (key == "gpu_temp")
            return (*n)[1];
        return (*n)[3] ? std::optional<double>(100.0 * (*n)[2] / (*n)[3]) : std::nullopt;
    }
    if (key == "net_down" || key == "net_up") {
        const auto r = net();
        if (!r)
            return std::nullopt;
        return key == "net_down" ? r->first : r->second;
    }
    if (key == "disk") {
        const QStorageInfo root = QStorageInfo::root();
        if (!root.isValid() || !root.bytesTotal())
            return std::nullopt;
        return 100.0 * (1 - double(root.bytesAvailable()) / double(root.bytesTotal()));
    }
    if (key == "load") {
#ifdef Q_OS_UNIX
        double l[1];
        if (getloadavg(l, 1) == 1)
            return l[0];
#endif
        return std::nullopt;
    }
    if (key == "battery") {
#ifdef Q_OS_WIN
        SYSTEM_POWER_STATUS s;
        if (GetSystemPowerStatus(&s) && s.BatteryLifePercent != 255)
            return double(s.BatteryLifePercent);
        return std::nullopt;
#else
        QDir base(QStringLiteral("/sys/class/power_supply"));
        for (const QString &d : base.entryList({"BAT*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (auto v = readFile(base.filePath(d) + "/capacity"))
                return v->trimmed().toDouble();
        return std::nullopt;
#endif
    }
    return std::nullopt;
}

QHash<QString, std::optional<double>> Sampler::sample(const QSet<QString> &wanted)
{
    netDone_ = false;
    QHash<QString, std::optional<double>> out;
    for (const QString &k : wanted)
        out.insert(k, read(k));
    return out;
}

// ---- commands

bool CommandRunner::due(const QString &command, double interval)
{
    if (!clock_.isValid())
        clock_.start();
    if (command != command_) {
        command_ = command;
        output.reset();
        at_ = -1;
    }
    const bool busy = proc_ && proc_->state() != QProcess::NotRunning;
    return !busy && !command.trimmed().isEmpty() && (at_ < 0 || clock_.elapsed() - at_ >= std::max(500.0, interval * 1000));
}

void CommandRunner::start(const QString &command, int timeoutMs)
{
    at_ = clock_.elapsed();
    proc_ = std::make_unique<QProcess>();
    QProcess *p = proc_.get();
    p->setStandardInputFile(QProcess::nullDevice());
    p->setStandardErrorFile(QProcess::nullDevice());
    connect(p, &QProcess::finished, this, [this, p, command](int code, QProcess::ExitStatus st) {
        if (command == command_) {
            QString text = QString::fromUtf8(p->readAllStandardOutput());
            while (!text.isEmpty() && text.back().isSpace())
                text.chop(1);
            output = text;
            error = st == QProcess::NormalExit && code == 0 ? QString() : QStringLiteral("exit %1").arg(code);
        }
    });
    connect(p, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            error = QStringLiteral("could not start");
    });
    QTimer::singleShot(timeoutMs, p, [this, p] {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            error = QStringLiteral("timed out");
        }
    });
#ifdef Q_OS_WIN
    p->start(QStringLiteral("cmd"), {"/c", command});
#else
    p->start(QStringLiteral("/bin/sh"), {"-c", command});
#endif
}

// ---- agent

HudAgent::HudAgent()
    : cfg(Config::load())
{
    clock_.start();
    values = QList<std::optional<std::pair<double, int>>>(HOST_VALUES);
    texts = QList<std::optional<QString>>(HOST_TEXTS);
    history = QList<QList<std::optional<std::pair<double, int>>>>(HOST_VALUES);
    sentText_ = QList<std::optional<QString>>(HOST_TEXTS);
    textAt_ = QList<qint64>(HOST_TEXTS, 0);
    for (int i = 0; i < HOST_VALUES; ++i) {
        own_.push_back(std::make_unique<CommandRunner>());
        valueRunners.append(own_.back().get());
    }
    for (int i = 0; i < HOST_TEXTS; ++i) {
        own_.push_back(std::make_unique<CommandRunner>());
        textRunners.append(own_.back().get());
    }
}

void HudAgent::attach(std::function<bool(const QList<QByteArray> &)> s, int textLen)
{
    send = std::move(s);
    limit = textLen;
    sentText_ = QList<std::optional<QString>>(HOST_TEXTS);
}

void HudAgent::reload()
{
    cfg = Config::load();
    sentText_ = QList<std::optional<QString>>(HOST_TEXTS);
}

bool HudAgent::write(const QList<QByteArray> &packets)
{
    if (!send)
        return false;
    if (packets.isEmpty())
        return true;
    if (!send(packets)) {
        lastError = QStringLiteral("write failed");
        detach();
        return false;
    }
    pushes += packets.size();
    return true;
}

void HudAgent::tick()
{
    const qint64 now = clock_.elapsed();
    for (int i = 0; i < cfg.values.size() && i < HOST_VALUES; ++i) {
        const auto &s = cfg.values[i];
        if (s.metric == QLatin1String("command") && valueRunners[i]->due(s.command, s.interval))
            valueRunners[i]->start(s.command, int(std::max(2.0, s.interval) * 1000));
    }
    for (int i = 0; i < cfg.texts.size() && i < HOST_TEXTS; ++i) {
        const auto &s = cfg.texts[i];
        if (textRunners[i]->due(s.command, s.interval))
            textRunners[i]->start(s.command, int(std::max(2.0, s.interval) * 1000));
    }

    if (now - sampleAt_ >= 1000) {
        sampleAt_ = now;
        QSet<QString> wanted;
        for (const auto &s : cfg.values)
            if (metric(s.metric) && s.metric != QLatin1String("command"))
                wanted.insert(s.metric);
        const auto got = sampler_.sample(wanted);
        QList<armed::HostValue> push;
        for (int i = 0; i < cfg.values.size() && i < HOST_VALUES; ++i) {
            const auto &s = cfg.values[i];
            const int dec = std::clamp(s.dec, 0, 4);
            std::optional<double> v;
            if (s.metric == QLatin1String("command")) {
                if (valueRunners[i]->output)
                    v = firstNumber(*valueRunners[i]->output);
            } else {
                v = got.value(s.metric);
            }
            values[i] = v ? std::optional(std::pair{*v, dec}) : std::nullopt;
            history[i].append(values[i]);
            if (history[i].size() > 64)
                history[i].removeFirst();
            if (v)
                push.append({i, dec, qint64(std::llround(*v * std::pow(10, dec)))});
        }
        write(push.isEmpty() ? QList<QByteArray>{} : armed::hudPushPackets(push, METRIC_TTL));
    }

    for (int i = 0; i < cfg.texts.size() && i < HOST_TEXTS; ++i) {
        const auto &out = textRunners[i]->output;
        if (!out) {
            texts[i].reset();
            continue;
        }
        QString text = toAscii(*out);
        // keep the end, from a line start: logs grow at the bottom
        if (text.size() > limit - 1) {
            text = text.right(limit - 1);
            const int nl = int(text.left(text.size() - 1).indexOf('\n'));
            if (nl >= 0)
                text = text.mid(nl + 1);
        }
        texts[i] = text;
        const int ttl = std::min(255, std::max(10, int(3 * cfg.texts[i].interval)));
        if (text != sentText_[i] || now - textAt_[i] > ttl * 500) {
            if (write(armed::hudTextPackets(i, text, ttl, limit))) {
                sentText_[i] = text;
                textAt_[i] = now;
            }
        }
    }
}

// ---- headless

int runHeadless()
{
    std::fprintf(stderr, "HUD agent: config %s\n", qPrintable(configPath()));
    QDir().mkpath(paths::configDir());
    auto lock = std::make_shared<QLockFile>(paths::configDir() + "/hud-agent.lock");
    lock->setStaleLockTime(0);
    auto agentPtr = std::make_shared<HudAgent>();
    auto link = std::make_shared<std::unique_ptr<hid::Link>>();
    auto haveLock = std::make_shared<bool>(false);
    auto waiting = std::make_shared<bool>(false);
    auto mtime = std::make_shared<QDateTime>();
    auto nextFind = std::make_shared<qint64>(0);
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();

    auto find = [agentPtr, link] {
        for (const auto &d : hid::discover({})) {
            if (d.kind != hid::DeviceKind::Vial && d.kind != hid::DeviceKind::Dongle)
                continue;
            auto dev = hid::HidapiDevice::open(d.path);
            if (!dev)
                continue;
            auto l = std::make_unique<hid::Link>(std::move(dev), d.isDongle());
            try {
                // only the INFO reply: the lists armed::probe reads are a lot of traffic over the dongle
                const QByteArray d0 = l->transact(QByteArray("\xAD\x00", 2), 3);
                const quint16 features = quint16(bytes::u8(d0, 10) | bytes::u8(d0, 11) << 8);
                if (bytes::u8(d0, 0) != armed::CMD_ARMED || d0.mid(2, 4) != "ARMD" || !(features & armed::F_HUD))
                    continue;
                auto hi = armed::hudInfo(*l);
                if (!hi)
                    continue;
                if (features & armed::F_CLOCK) {
                    QThread::msleep(ulong(1000 - QTime::currentTime().msec()));
                    armed::clockSet(*l, QDateTime::currentDateTime());
                }
                hid::Link *raw = l.get();
                *link = std::move(l);
                agentPtr->attach([raw](const QList<QByteArray> &packets) {
                    try {
                        for (const auto &p : packets)
                            raw->write(p);
                    } catch (const std::exception &) {
                        return false;
                    }
                    return true;
                }, hi->textLen);
                std::fprintf(stderr, "HUD agent: feeding %s\n", qPrintable(d.title()));
                return;
            } catch (const std::exception &) {
            }
        }
    };

    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, [=] {
        if (!*haveLock) {
            if (!lock->tryLock(0)) {
                if (!*waiting)
                    std::fputs("HUD agent: Vial Armed or another agent is feeding the HUD, waiting\n", stderr);
                *waiting = true;
                timer->setInterval(5000);
                return;
            }
            *haveLock = true;
            timer->setInterval(250);
        }
        const QDateTime m = QFileInfo(configPath()).lastModified();
        if (m != *mtime) {
            *mtime = m;
            agentPtr->reload();
        }
        if (!agentPtr->send) {
            if (*link) {
                link->reset();
                std::fputs("HUD agent: keyboard gone\n", stderr);
            }
            if (clock->elapsed() >= *nextFind) {
                *nextFind = clock->elapsed() + 3000;
                find();
            }
        }
        agentPtr->tick();
    });
    timer->start(250);
    return qApp->exec();
}

// ---- autostart

QString autostartPath()
{
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + "/.config";
    return base + "/autostart/vial-armed-agent.desktop";
}

bool autostartOn()
{
#ifdef Q_OS_WIN
    return QSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat)
        .contains("vial-armed-agent");
#else
    return QFile::exists(autostartPath());
#endif
}

bool setAutostart(bool on, QString *error)
{
    QString exe = qEnvironmentVariable("APPIMAGE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (on)
        run.setValue("vial-armed-agent", QStringLiteral("\"%1\" --agent").arg(QDir::toNativeSeparators(exe)));
    else
        run.remove("vial-armed-agent");
    Q_UNUSED(error);
    return true;
#else
    const QString path = autostartPath();
    if (!on) {
        QFile::remove(path);
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = f.errorString();
        return false;
    }
    QString quoted = exe;
    quoted.replace('\'', "'\\''");
    f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=vial-armed HUD agent\n"
                           "Comment=Feeds the keyboard's screen with this computer's metrics\n"
                           "Exec='%1' --agent\nTerminal=false\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\n")
                .arg(quoted).toUtf8());
    if (!f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
#endif
}

}
