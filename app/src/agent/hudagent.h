#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

// HUD agent: feeds a qmk-armed keyboard's HUD with this computer's metrics and the output of commands.
// Runs inside Vial Armed, or on its own with `vial-armed --agent`; both share the config file with the
// Python app. Commands are stored only on this computer: the keyboard never tells the agent what to run.
namespace agent {

inline constexpr int HOST_VALUES = 16;
inline constexpr int HOST_TEXTS = 4;
inline constexpr int METRIC_TTL = 5; // s: the keyboard shows "--" once a value is this old

struct Metric {
    QString key, name, unit;
    int dec, lo, hi;
};
const QList<Metric> &metrics();
const Metric *metric(const QString &key);
extern const QList<QPair<QString, QString>> kTextExamples;

struct ValueSlot {
    QString metric, command;
    double interval = 1;
    int dec = 0;
};
struct TextSlot {
    QString command;
    double interval = 5;
};
struct Config {
    QList<ValueSlot> values;
    QList<TextSlot> texts;
    static Config defaults();
    static Config load();
    void save() const;
};
QString configPath();
QString slotName(const Config &cfg, int slot);
QString textName(const Config &cfg, int slot);

QString toAscii(const QString &text); // the keyboard's fonts are ASCII: Cyrillic is spelled in Latin
std::optional<double> firstNumber(const QString &text);

// Reads every wanted metric once per call; rates and loads are over the time since the previous call
class Sampler {
public:
    Sampler();
    ~Sampler();
    QHash<QString, std::optional<double>> sample(const QSet<QString> &wanted);

private:
    std::optional<double> read(const QString &key);
    std::optional<std::pair<double, double>> net();
    std::optional<QList<double>> nvidia();
    QString amdGpu() const;

    std::optional<std::pair<quint64, quint64>> prevCpu_;
    std::optional<std::tuple<qint64, quint64, quint64>> prevNet_;
    std::optional<std::pair<double, double>> netRates_;
    bool netDone_ = false;
    std::unique_ptr<QProcess> nvidiaProc_;
    std::optional<QList<double>> nvidia_;
    std::optional<bool> nvidiaOk_;
    QElapsedTimer clock_;
#ifdef Q_OS_WIN
    quint64 prevIdle_ = 0, prevTotal_ = 0;
#endif
};

// Runs a shell command every `interval` seconds in the background, keeps its last output
class CommandRunner : public QObject {
    Q_OBJECT
public:
    bool due(const QString &command, double interval);
    void start(const QString &command, int timeoutMs);
    std::optional<QString> output;
    QString error;

private:
    QString command_;
    qint64 at_ = -1;
    std::unique_ptr<QProcess> proc_;
    QElapsedTimer clock_;
};

// Call tick() a few times a second. send() writes reports to the keyboard (no replies) and returns false when
// the keyboard is gone
class HudAgent {
public:
    HudAgent();
    Config cfg;
    QList<std::optional<std::pair<double, int>>> values; // value, decimals
    QList<std::optional<QString>> texts;
    QList<QList<std::optional<std::pair<double, int>>>> history;
    QList<CommandRunner *> valueRunners, textRunners;

    std::function<bool(const QList<QByteArray> &)> send;
    int limit = 160;
    qint64 pushes = 0;
    QString lastError;

    void attach(std::function<bool(const QList<QByteArray> &)> s, int textLen);
    void detach() { send = nullptr; }
    void reload();
    void tick();

private:
    bool write(const QList<QByteArray> &packets);
    Sampler sampler_;
    QList<std::optional<QString>> sentText_;
    QList<qint64> textAt_;
    qint64 sampleAt_ = -100000;
    QElapsedTimer clock_;
    std::vector<std::unique_ptr<CommandRunner>> own_;
};

// `vial-armed --agent`: no window, finds the keyboard by itself
int runHeadless();

QString autostartPath();
bool setAutostart(bool on, QString *error);
bool autostartOn();

}
