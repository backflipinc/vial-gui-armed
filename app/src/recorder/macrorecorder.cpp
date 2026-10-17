#include "recorder/macrorecorder.h"

#include "keycodes/keycodes.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

#include <cstdio>

#ifdef Q_OS_LINUX
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <unistd.h>
#include <vector>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
// evdev / Windows virtual key -> the names the Python `keyboard` library reported
struct KeyName {
    int code;
    const char *name;
};
#endif

#ifdef Q_OS_LINUX
const KeyName kEvdev[] = {
    {KEY_ESC, "esc"}, {KEY_1, "1"}, {KEY_2, "2"}, {KEY_3, "3"}, {KEY_4, "4"}, {KEY_5, "5"}, {KEY_6, "6"},
    {KEY_7, "7"}, {KEY_8, "8"}, {KEY_9, "9"}, {KEY_0, "0"}, {KEY_MINUS, "-"}, {KEY_EQUAL, "="},
    {KEY_BACKSPACE, "backspace"}, {KEY_TAB, "tab"}, {KEY_Q, "q"}, {KEY_W, "w"}, {KEY_E, "e"}, {KEY_R, "r"},
    {KEY_T, "t"}, {KEY_Y, "y"}, {KEY_U, "u"}, {KEY_I, "i"}, {KEY_O, "o"}, {KEY_P, "p"}, {KEY_LEFTBRACE, "["},
    {KEY_RIGHTBRACE, "]"}, {KEY_ENTER, "enter"}, {KEY_LEFTCTRL, "left ctrl"}, {KEY_A, "a"}, {KEY_S, "s"},
    {KEY_D, "d"}, {KEY_F, "f"}, {KEY_G, "g"}, {KEY_H, "h"}, {KEY_J, "j"}, {KEY_K, "k"}, {KEY_L, "l"},
    {KEY_SEMICOLON, ";"}, {KEY_APOSTROPHE, "'"}, {KEY_GRAVE, "`"}, {KEY_LEFTSHIFT, "left shift"},
    {KEY_BACKSLASH, "\\"}, {KEY_Z, "z"}, {KEY_X, "x"}, {KEY_C, "c"}, {KEY_V, "v"}, {KEY_B, "b"}, {KEY_N, "n"},
    {KEY_M, "m"}, {KEY_COMMA, ","}, {KEY_DOT, "."}, {KEY_SLASH, "/"}, {KEY_RIGHTSHIFT, "right shift"},
    {KEY_LEFTALT, "alt"}, {KEY_SPACE, "space"}, {KEY_CAPSLOCK, "caps lock"}, {KEY_F1, "f1"}, {KEY_F2, "f2"},
    {KEY_F3, "f3"}, {KEY_F4, "f4"}, {KEY_F5, "f5"}, {KEY_F6, "f6"}, {KEY_F7, "f7"}, {KEY_F8, "f8"},
    {KEY_F9, "f9"}, {KEY_F10, "f10"}, {KEY_NUMLOCK, "num lock"}, {KEY_SCROLLLOCK, "scroll lock"},
    {KEY_F11, "f11"}, {KEY_F12, "f12"}, {KEY_RIGHTCTRL, "right ctrl"}, {KEY_HOME, "home"}, {KEY_UP, "up"},
    {KEY_PAGEUP, "page up"}, {KEY_LEFT, "left"}, {KEY_RIGHT, "right"}, {KEY_END, "end"}, {KEY_DOWN, "down"},
    {KEY_PAGEDOWN, "page down"}, {KEY_INSERT, "insert"}, {KEY_DELETE, "delete"}, {KEY_PAUSE, "pause"},
    {KEY_LEFTMETA, "left windows"}, {KEY_RIGHTMETA, "right windows"}, {KEY_COMPOSE, "menu"},
};

class LinuxRecorder : public MacroRecorder {
public:
    explicit LinuxRecorder(QObject *parent)
        : MacroRecorder(parent)
    {
        connect(&proc_, &QProcess::readyReadStandardOutput, this, [this] {
            while (proc_.canReadLine()) {
                const QString line = QString::fromUtf8(proc_.readLine()).trimmed();
                const int colon = line.indexOf(QLatin1Char(':'));
                if (colon > 0)
                    emit keystroke(line.left(colon) == QLatin1String("down"), line.mid(colon + 1));
            }
        });
        connect(&proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            if (!stopping_ && code != 0)
                emit failed(tr("Recording needs administrator rights (pkexec), and they were not granted"));
            stopping_ = false;
            emit stopped();
        });
    }

    void start() override
    {
        // the AppImage runs from a temporary mount; pkexec has to start the AppImage itself
        QString self = qEnvironmentVariable("APPIMAGE");
        if (self.isEmpty())
            self = QCoreApplication::applicationFilePath();
        proc_.start(QStringLiteral("pkexec"), {self, QStringLiteral("--linux-recorder")},
                    QIODevice::ReadWrite | QIODevice::Unbuffered);
    }

    void stop() override
    {
        stopping_ = true;
        proc_.write("q");
        if (!proc_.waitForFinished(2000))
            proc_.kill();
    }

private:
    QProcess proc_;
    bool stopping_ = false;
};
#endif

#ifdef Q_OS_WIN
const KeyName kVk[] = {
    {VK_ESCAPE, "esc"}, {VK_BACK, "backspace"}, {VK_TAB, "tab"}, {VK_RETURN, "enter"}, {VK_SPACE, "space"},
    {VK_CAPITAL, "caps lock"}, {VK_LCONTROL, "left ctrl"}, {VK_RCONTROL, "right ctrl"},
    {VK_LSHIFT, "left shift"}, {VK_RSHIFT, "right shift"}, {VK_LMENU, "alt"}, {VK_LWIN, "left windows"},
    {VK_RWIN, "right windows"}, {VK_APPS, "menu"}, {VK_OEM_MINUS, "-"}, {VK_OEM_PLUS, "="}, {VK_OEM_4, "["},
    {VK_OEM_6, "]"}, {VK_OEM_5, "\\"}, {VK_OEM_1, ";"}, {VK_OEM_7, "'"}, {VK_OEM_3, "`"}, {VK_OEM_COMMA, ","},
    {VK_OEM_PERIOD, "."}, {VK_OEM_2, "/"}, {VK_NUMLOCK, "num lock"}, {VK_SCROLL, "scroll lock"},
    {VK_PAUSE, "pause"}, {VK_INSERT, "insert"}, {VK_DELETE, "delete"}, {VK_HOME, "home"}, {VK_END, "end"},
    {VK_PRIOR, "page up"}, {VK_NEXT, "page down"}, {VK_LEFT, "left"}, {VK_RIGHT, "right"}, {VK_UP, "up"},
    {VK_DOWN, "down"},
};

class WindowsRecorder;
WindowsRecorder *active = nullptr;

class WindowsRecorder : public MacroRecorder {
public:
    using MacroRecorder::MacroRecorder;

    void start() override
    {
        active = this;
        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &WindowsRecorder::proc, GetModuleHandleW(nullptr), 0);
        if (!hook_)
            emit failed(tr("Could not listen to the keyboard"));
    }

    void stop() override
    {
        if (hook_)
            UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
        active = nullptr;
        emit stopped();
    }

private:
    static LRESULT CALLBACK proc(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HC_ACTION && active) {
            const auto *k = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lp);
            const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
            QString name;
            if ((k->vkCode >= 'A' && k->vkCode <= 'Z') || (k->vkCode >= '0' && k->vkCode <= '9'))
                name = QString(QChar(char(k->vkCode))).toLower();
            else if (k->vkCode >= VK_F1 && k->vkCode <= VK_F24)
                name = QStringLiteral("f%1").arg(k->vkCode - VK_F1 + 1);
            else
                for (const auto &kn : kVk)
                    if (DWORD(kn.code) == k->vkCode)
                        name = QString::fromLatin1(kn.name);
            if (!name.isEmpty())
                QMetaObject::invokeMethod(active, [down, name] {
                    if (active)
                        emit active->keystroke(down, name);
                });
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }

    HHOOK hook_ = nullptr;
};
#endif

}

MacroRecorder *MacroRecorder::create(QObject *parent)
{
#if defined(Q_OS_LINUX)
    if (!QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty())
        return new LinuxRecorder(parent);
    return nullptr;
#elif defined(Q_OS_WIN)
    return new WindowsRecorder(parent);
#else
    Q_UNUSED(parent);
    return nullptr;
#endif
}

QList<MacroRecorder::Stroke> MacroRecorder::optimize(QList<Stroke> in)
{
    // exact repeats of a press or a release
    QList<Stroke> a;
    for (const Stroke &s : in) {
        if (!a.isEmpty() && (s.kind == Stroke::Down || s.kind == Stroke::Up) && a.last().kind == s.kind
            && a.last().keycode == s.keycode)
            continue;
        a.append(s);
    }
    // a press right before its release is a tap
    QList<Stroke> b;
    for (int i = 0; i < a.size(); ++i) {
        if (i + 1 < a.size() && a[i].kind == Stroke::Down && a[i + 1].kind == Stroke::Up
            && a[i].keycode == a[i + 1].keycode) {
            b.append({Stroke::Tap, a[i].keycode, {}});
            ++i;
        } else {
            b.append(a[i]);
        }
    }
    // two or more printable taps in a row become text
    const auto &reg = keycodes::Registry::instance();
    auto printable = [&reg](const Stroke &s) -> QString {
        if (s.kind != Stroke::Tap)
            return {};
        const auto *k = reg.findByQmkId(s.keycode);
        return k ? k->printable : QString();
    };
    QList<Stroke> out;
    for (int i = 0; i < b.size();) {
        if (i + 1 < b.size() && !printable(b[i]).isEmpty() && !printable(b[i + 1]).isEmpty()) {
            QString text;
            while (i < b.size() && !printable(b[i]).isEmpty())
                text += printable(b[i++]);
            out.append({Stroke::Text, {}, text});
        } else {
            out.append(b[i++]);
        }
    }
    return out;
}

macro::Macro MacroRecorder::toActions(const QList<Stroke> &strokes)
{
    macro::Macro m;
    for (const Stroke &s : strokes) {
        macro::Action a;
        if (s.kind == Stroke::Text) {
            a.kind = macro::Kind::Text;
            a.text = s.text;
        } else {
            a.kind = s.kind == Stroke::Tap ? macro::Kind::Tap : s.kind == Stroke::Down ? macro::Kind::Down : macro::Kind::Up;
            a.keys = {s.keycode};
        }
        // merge, like a round trip through the keyboard's format: several keys in one action
        if (!m.isEmpty() && a.kind != macro::Kind::Text && m.last().kind == a.kind)
            m.last().keys += a.keys;
        else if (!m.isEmpty() && a.kind == macro::Kind::Text && m.last().kind == macro::Kind::Text)
            m.last().text += a.text;
        else
            m.append(a);
    }
    return m;
}

int runLinuxRecorderHelper()
{
#ifdef Q_OS_LINUX
    std::vector<pollfd> fds;
    fds.push_back({STDIN_FILENO, POLLIN, 0});
    if (DIR *dir = opendir("/dev/input")) {
        while (dirent *e = readdir(dir)) {
            if (strncmp(e->d_name, "event", 5) != 0)
                continue;
            std::string path = std::string("/dev/input/") + e->d_name;
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0)
                continue;
            unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = {};
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
            const auto has = [&keys](int k) { return keys[k / (8 * sizeof(long))] >> (k % (8 * sizeof(long))) & 1; };
            if (has(KEY_A) && has(KEY_SPACE))
                fds.push_back({fd, POLLIN, 0});
            else
                close(fd);
        }
        closedir(dir);
    }
    if (fds.size() < 2) {
        std::fputs("no keyboards found\n", stderr);
        return 1;
    }
    for (;;) {
        if (poll(fds.data(), fds.size(), -1) < 0)
            return 1;
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char c = 0;
            if (read(STDIN_FILENO, &c, 1) <= 0 || c == 'q')
                return 0;
        }
        for (size_t i = 1; i < fds.size(); ++i) {
            if (!(fds[i].revents & POLLIN))
                continue;
            input_event ev;
            while (read(fds[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type != EV_KEY || ev.value == 2)
                    continue;
                for (const auto &kn : kEvdev) {
                    if (kn.code == ev.code) {
                        std::printf("%s:%s\n", ev.value ? "down" : "up", kn.name);
                        std::fflush(stdout);
                    }
                }
            }
        }
    }
#else
    return 1;
#endif
}
