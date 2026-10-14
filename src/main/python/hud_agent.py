# SPDX-License-Identifier: GPL-2.0-or-later
# HUD agent: feeds a qmk-armed keyboard's HUD with this computer's metrics and the output of commands.
# Runs inside Vial Armed, or on its own with `vial-armed --agent` (the agent config is shared).
# Commands are stored only on this computer: the keyboard never tells the agent what to run.
import json
import logging
import os
import re
import shlex
import subprocess
import sys
import threading
import time

from protocol.qmk_armed import hud_push_packets, hud_text_packets

HOST_VALUES = 16
HOST_TEXTS = 4
METRIC_TTL = 5  # s: the keyboard shows "--" once a value is this old


class Metric:

    def __init__(self, key, name, unit, dec, lo, hi):
        self.key = key
        self.name = name
        self.unit = unit  # suggested label suffix, the keyboard's fonts are ASCII
        self.dec = dec
        self.lo = lo
        self.hi = hi


METRICS = [
    Metric("cpu", "CPU LOAD", "%", 0, 0, 100),
    Metric("cpu_temp", "CPU TEMP", "C", 0, 30, 100),
    Metric("ram", "RAM USED", "%", 0, 0, 100),
    Metric("ram_gb", "RAM USED GB", "G", 1, 0, 64),
    Metric("swap", "SWAP USED", "%", 0, 0, 100),
    Metric("gpu", "GPU LOAD", "%", 0, 0, 100),
    Metric("gpu_temp", "GPU TEMP", "C", 0, 30, 100),
    Metric("vram", "VRAM USED", "%", 0, 0, 100),
    Metric("net_down", "NET DOWN MB/S", "M", 1, 0, 100),
    Metric("net_up", "NET UP MB/S", "M", 1, 0, 100),
    Metric("disk", "DISK / USED", "%", 0, 0, 100),
    Metric("load", "LOAD AVERAGE", "", 2, 0, 16),
    Metric("battery", "PC BATTERY", "%", 0, 0, 100),
    Metric("command", "COMMAND", "", 0, 0, 100),
]
METRIC = {m.key: m for m in METRICS}

TEXT_EXAMPLES = [
    ("NOW PLAYING", "playerctl metadata --format '{{ artist }} - {{ title }}'"),
    ("UPTIME", "uptime -p"),
    ("WEATHER", "curl -s 'wttr.in/?format=%C+%t'"),
    ("LAST LOG LINES", "journalctl -n 4 -o cat --no-pager"),
]


def config_dir():
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA", os.path.expanduser("~"))
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, "vial-armed")


def config_path():
    return os.path.join(config_dir(), "hud-agent.json")


def default_config():
    return {
        "values": [{"metric": m, "command": "", "interval": 1, "dec": METRIC[m].dec}
                   for m in ("cpu", "cpu_temp", "ram", "gpu", "gpu_temp", "net_down", "net_up", "disk")]
                  + [{"metric": "", "command": "", "interval": 1, "dec": 0} for _ in range(HOST_VALUES - 8)],
        "texts": [{"command": "", "interval": 5} for _ in range(HOST_TEXTS)],
    }


def load_config():
    cfg = default_config()
    try:
        with open(config_path()) as f:
            stored = json.load(f)
        for key, n in (("values", HOST_VALUES), ("texts", HOST_TEXTS)):
            for i, slot in enumerate(stored.get(key, [])[:n]):
                cfg[key][i].update(slot)
    except FileNotFoundError:
        pass
    except Exception as e:
        logging.warning("HUD agent: bad config %s: %s", config_path(), e)
    return cfg


def save_config(cfg):
    os.makedirs(config_dir(), exist_ok=True)
    tmp = config_path() + ".tmp"
    with open(tmp, "w") as f:
        json.dump(cfg, f, indent=1)
    os.replace(tmp, config_path())


def slot_name(cfg, slot):
    """ What the editor calls host value `slot` """
    s = cfg["values"][slot]
    if s["metric"] == "command":
        return "CMD " + (s["command"].split()[0] if s["command"].strip() else "?")
    if s["metric"] in METRIC:
        return METRIC[s["metric"]].name
    return "HOST {}".format(slot + 1)


def text_name(cfg, slot):
    cmd = cfg["texts"][slot]["command"].strip()
    return ("CMD " + cmd.split()[0]) if cmd else "HOST TEXT {}".format(slot + 1)


# ---- text for the keyboard: ASCII only

_RU = dict(zip("абвгдеёжзийклмнопрстуфхцчшщъыьэюя",
               ["a", "b", "v", "g", "d", "e", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p", "r", "s", "t",
                "u", "f", "h", "ts", "ch", "sh", "sch", "", "y", "", "e", "yu", "ya"]))
_ANSI = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
_NUMBER = re.compile(r"[-+]?\d+(?:[.,]\d+)?")


def to_ascii(text):
    out = []
    for ch in _ANSI.sub("", text):
        if ch == "\n" or " " <= ch <= "~":
            out.append(ch)
        elif ch == "\t":
            out.append(" ")
        elif ch.lower() in _RU:
            t = _RU[ch.lower()]
            out.append(t.upper() if ch.isupper() else t)
        elif ch in "°":
            out.append("")
        elif ch in "–—":
            out.append("-")
        elif ch in "‘’":
            out.append("'")
        elif ch in "“”«»":
            out.append('"')
        elif ch >= " ":
            out.append("?")
    return "".join(out)


def first_number(text):
    m = _NUMBER.search(text)
    return float(m.group(0).replace(",", ".")) if m else None


# ---- metrics

def _read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return None


def _hwmon_temp(names, labels=()):
    base = "/sys/class/hwmon"
    try:
        hwmons = sorted(os.listdir(base))
    except OSError:
        return None
    for h in hwmons:
        d = os.path.join(base, h)
        if (_read(os.path.join(d, "name")) or "").strip() not in names:
            continue
        inputs = sorted(f for f in os.listdir(d) if f.startswith("temp") and f.endswith("_input"))
        pick = None
        for f in inputs:
            label = (_read(os.path.join(d, f.replace("_input", "_label"))) or "").strip()
            if label in labels:
                pick = f
                break
        pick = pick or (inputs[0] if inputs else None)
        if pick:
            v = _read(os.path.join(d, pick))
            if v and v.strip().lstrip("-").isdigit():
                return int(v) / 1000
    return None


class Sampler:
    """ Reads every metric once per call; rates and loads are over the time since the previous call """

    def __init__(self):
        self.prev_cpu = None
        self.prev_net = None
        self.net_done = False
        self.net_rates = None
        self.nvidia = None
        self.nvidia_at = 0
        self.nvidia_ok = None
        self.values = {}
        try:
            import psutil
            self.psutil = psutil
        except ImportError:
            self.psutil = None

    def sample(self, wanted):
        self.net_done = False
        out = {}
        for key in wanted:
            try:
                out[key] = getattr(self, "m_" + key)()
            except Exception as e:
                logging.debug("HUD metric %s: %s", key, e)
                out[key] = None
        self.values = out
        return out

    def m_cpu(self):
        stat = _read("/proc/stat")
        if stat is None:
            return self.psutil.cpu_percent() if self.psutil else None
        f = [int(x) for x in stat.split("\n", 1)[0].split()[1:]]
        idle, total = f[3] + (f[4] if len(f) > 4 else 0), sum(f[:8])
        prev, self.prev_cpu = self.prev_cpu, (idle, total)
        if prev is None or total == prev[1]:
            return None
        return 100.0 * (1 - (idle - prev[0]) / (total - prev[1]))

    def m_cpu_temp(self):
        t = _hwmon_temp(("k10temp", "zenpower", "coretemp", "cpu_thermal", "soc_thermal"), ("Tctl", "Tdie", "Package id 0"))
        if t is None:
            z = _read("/sys/class/thermal/thermal_zone0/temp")
            t = int(z) / 1000 if z and z.strip().isdigit() else None
        return t

    def _meminfo(self):
        info = {}
        for line in (_read("/proc/meminfo") or "").splitlines():
            k, _, v = line.partition(":")
            info[k] = int(v.split()[0]) if v.split() else 0
        return info

    def m_ram(self):
        m = self._meminfo()
        if "MemTotal" not in m:
            return self.psutil.virtual_memory().percent if self.psutil else None
        return 100.0 * (m["MemTotal"] - m["MemAvailable"]) / m["MemTotal"]

    def m_ram_gb(self):
        m = self._meminfo()
        if "MemTotal" not in m:
            return self.psutil.virtual_memory().used / 2 ** 30 if self.psutil else None
        return (m["MemTotal"] - m["MemAvailable"]) / 2 ** 20

    def m_swap(self):
        m = self._meminfo()
        if not m.get("SwapTotal"):
            return 0.0 if m else (self.psutil.swap_memory().percent if self.psutil else None)
        return 100.0 * (m["SwapTotal"] - m["SwapFree"]) / m["SwapTotal"]

    def _amd_gpu(self):
        base = "/sys/class/drm"
        try:
            cards = sorted(c for c in os.listdir(base) if re.match(r"card\d+$", c))
        except OSError:
            return None
        for c in cards:
            dev = os.path.join(base, c, "device")
            if _read(os.path.join(dev, "gpu_busy_percent")) is not None:
                return dev
        return None

    def _nvidia(self):
        if self.nvidia_ok is False:
            return None
        if time.monotonic() - self.nvidia_at > 0.9:
            self.nvidia_at = time.monotonic()
            try:
                out = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
                                      "--format=csv,noheader,nounits"], stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, universal_newlines=True, timeout=2).stdout
                self.nvidia = [float(x) for x in out.splitlines()[0].split(",")]
                self.nvidia_ok = True
            except Exception:
                self.nvidia_ok = self.nvidia_ok or False
                self.nvidia = None
        return self.nvidia

    def m_gpu(self):
        dev = self._amd_gpu()
        if dev:
            return float(_read(os.path.join(dev, "gpu_busy_percent")))
        n = self._nvidia()
        return n[0] if n else None

    def m_gpu_temp(self):
        dev = self._amd_gpu()
        if dev:
            try:
                for h in os.listdir(os.path.join(dev, "hwmon")):
                    v = _read(os.path.join(dev, "hwmon", h, "temp1_input"))
                    if v:
                        return int(v) / 1000
            except OSError:
                pass
        n = self._nvidia()
        return n[1] if n else None

    def m_vram(self):
        dev = self._amd_gpu()
        if dev:
            used, total = _read(os.path.join(dev, "mem_info_vram_used")), _read(os.path.join(dev, "mem_info_vram_total"))
            if used and total:
                return 100.0 * int(used) / int(total)
        n = self._nvidia()
        return 100.0 * n[2] / n[3] if n and n[3] else None

    def _net(self):
        """ (down, up) in MB/s since the previous sample, read once per sample """
        if self.net_done:
            return self.net_rates
        self.net_done = True
        self.net_rates = None
        text = _read("/proc/net/dev")
        if text is not None:
            rx = tx = 0
            for line in text.splitlines()[2:]:
                name, _, data = line.partition(":")
                if name.strip() == "lo":
                    continue
                f = data.split()
                rx += int(f[0])
                tx += int(f[8])
        elif self.psutil:
            c = self.psutil.net_io_counters()
            rx, tx = c.bytes_recv, c.bytes_sent
        else:
            return None
        now = time.monotonic()
        prev, self.prev_net = self.prev_net, (now, rx, tx)
        if prev is not None and now > prev[0]:
            dt = now - prev[0]
            self.net_rates = (rx - prev[1]) / dt / 1e6, (tx - prev[2]) / dt / 1e6
        return self.net_rates

    def m_net_down(self):
        r = self._net()
        return r[0] if r else None

    def m_net_up(self):
        r = self._net()
        return r[1] if r else None

    def m_disk(self):
        if hasattr(os, "statvfs"):
            st = os.statvfs("/")
            return 100.0 * (1 - st.f_bavail / st.f_blocks) if st.f_blocks else None
        return self.psutil.disk_usage("/").percent if self.psutil else None

    def m_load(self):
        return os.getloadavg()[0] if hasattr(os, "getloadavg") else None

    def m_battery(self):
        base = "/sys/class/power_supply"
        try:
            for d in sorted(os.listdir(base)):
                if d.startswith("BAT"):
                    v = _read(os.path.join(base, d, "capacity"))
                    if v:
                        return float(v)
        except OSError:
            pass
        if self.psutil and self.psutil.sensors_battery():
            return self.psutil.sensors_battery().percent
        return None


class CommandRunner:
    """ Runs a shell command every `interval` seconds in the background, keeps its last output """

    def __init__(self):
        self.output = None
        self.error = ""
        self.at = 0
        self.busy = False
        self.command = None

    def due(self, command, interval):
        if command != self.command:
            self.command = command
            self.output = None
            self.at = 0
        return not self.busy and command.strip() and time.monotonic() - self.at >= max(0.5, interval)

    def start(self, command, timeout):
        self.busy = True
        self.at = time.monotonic()
        threading.Thread(target=self.run, args=(command, timeout), daemon=True).start()

    def run(self, command, timeout):
        try:
            r = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                               stdin=subprocess.DEVNULL, timeout=timeout)
            text = r.stdout.decode("utf-8", "replace")
            if command == self.command:
                self.output = text.rstrip()
                self.error = "" if r.returncode == 0 else "exit {}".format(r.returncode)
        except subprocess.TimeoutExpired:
            self.error = "timed out"
        except Exception as e:
            self.error = str(e)
        finally:
            self.busy = False


class HudAgent:
    """ Call tick() a few times a second. `send` writes one report to the keyboard and may raise """

    def __init__(self):
        self.cfg = load_config()
        self.sampler = Sampler()
        self.value_runners = [CommandRunner() for _ in range(HOST_VALUES)]
        self.text_runners = [CommandRunner() for _ in range(HOST_TEXTS)]
        self.values = [None] * HOST_VALUES  # (value as float, decimals) or None
        self.texts = [None] * HOST_TEXTS
        self.history = [[] for _ in range(HOST_VALUES)]
        self.send = None
        self.limit = 160
        self.sent_text = [None] * HOST_TEXTS
        self.text_at = [0] * HOST_TEXTS
        self.sample_at = 0
        self.pushes = 0
        self.last_error = ""

    def attach(self, send, text_len=160):
        self.send = send
        self.limit = text_len
        self.sent_text = [None] * HOST_TEXTS

    def detach(self):
        self.send = None

    def reload(self, cfg=None):
        self.cfg = cfg or load_config()
        self.sent_text = [None] * HOST_TEXTS

    def tick(self):
        now = time.monotonic()
        cfg = self.cfg
        for i, slot in enumerate(cfg["values"]):
            if slot["metric"] == "command" and self.value_runners[i].due(slot["command"], slot["interval"]):
                self.value_runners[i].start(slot["command"], max(2, slot["interval"]))
        for i, slot in enumerate(cfg["texts"]):
            if self.text_runners[i].due(slot["command"], slot["interval"]):
                self.text_runners[i].start(slot["command"], max(2, slot["interval"]))

        if now - self.sample_at >= 1:
            self.sample_at = now
            wanted = {s["metric"] for s in cfg["values"] if s["metric"] in METRIC and s["metric"] != "command"}
            got = self.sampler.sample(wanted)
            push = []
            for i, slot in enumerate(cfg["values"]):
                key = slot["metric"]
                dec = max(0, min(4, int(slot.get("dec", 0))))
                if key == "command":
                    out = self.value_runners[i].output
                    v = first_number(out) if out else None
                else:
                    v = got.get(key)
                self.values[i] = None if v is None else (v, dec)
                self.history[i] = (self.history[i] + [self.values[i]])[-64:]
                if v is not None:
                    push.append((i, dec, int(round(v * 10 ** dec))))
            self.write(hud_push_packets(push, METRIC_TTL) if push else [])

        for i, slot in enumerate(cfg["texts"]):
            out = self.text_runners[i].output
            if out is None:
                self.texts[i] = None
                continue
            text = to_ascii(out)
            if len(text) > self.limit - 1:  # keep the end, from a line start: logs grow at the bottom
                text = text[-(self.limit - 1):]
                if "\n" in text[:-1]:
                    text = text[text.index("\n") + 1:]
            self.texts[i] = text
            ttl = min(255, max(10, int(3 * slot["interval"])))
            if text != self.sent_text[i] or now - self.text_at[i] > ttl / 2:
                if self.write(hud_text_packets(i, text, ttl, self.limit)):
                    self.sent_text[i] = text
                    self.text_at[i] = now

    def write(self, packets):
        if self.send is None:
            return False
        try:
            for p in packets:
                self.send(p)
                self.pushes += 1
        except Exception as e:
            self.last_error = str(e)
            logging.warning("HUD agent: write failed: %s", e)
            self.detach()
            return False
        return True


# ---- `vial-armed --agent`: no window, finds the keyboard by itself

def _raw_write(dev):
    def send(msg):
        if dev.write(b"\x00" + msg.ljust(32, b"\x00")) < 0:
            raise OSError("write failed")
    return send


def _probe(desc):
    """ opens a Vial keyboard or dongle that has a HUD and sets its clock; returns (dev, text length) """
    import struct
    from hidproxy import hid
    from util import hid_send, dongle_send, VIAL_DONGLES
    from protocol.qmk_armed import CMD_ARMED, SUB_INFO, SUB_HUD_INFO, F_HUD, F_CLOCK, HudInfo
    send = dongle_send if (desc["vendor_id"], desc["product_id"]) in VIAL_DONGLES else hid_send
    dev = hid.device()
    dev.open_path(desc["path"])
    try:
        data = send(dev, struct.pack("BB", CMD_ARMED, SUB_INFO), retries=3)
        features = data[10] | data[11] << 8
        if data[0] != CMD_ARMED or bytes(data[2:6]) != b"ARMD" or not features & F_HUD:
            raise OSError("no HUD")
        info = HudInfo(send(dev, struct.pack("BB", CMD_ARMED, SUB_HUD_INFO), retries=3))
        if features & F_CLOCK:
            _set_clock(dev, send)
        return dev, info.text_len
    except Exception:
        dev.close()
        raise


def _set_clock(dev, send):
    import struct
    from datetime import datetime
    from protocol.qmk_armed import CMD_ARMED, SUB_CLOCK_SET
    time.sleep(1 - datetime.now().microsecond / 1e6)
    now = datetime.now()
    secs = int(round((now - datetime(1970, 1, 1)).total_seconds()))
    send(dev, struct.pack("<BBI", CMD_ARMED, SUB_CLOCK_SET, secs))


def _find():
    from hidproxy import hid
    from util import VIAL_SERIAL_NUMBER_MAGIC, VIAL_DONGLES
    for desc in hid.enumerate():
        if desc["usage_page"] != 0xFF60 or desc["usage"] != 0x61:
            continue
        if VIAL_SERIAL_NUMBER_MAGIC in (desc["serial_number"] or "") or \
                (desc["vendor_id"], desc["product_id"]) in VIAL_DONGLES:
            try:
                return _probe(desc)
            except Exception as e:
                logging.debug("HUD agent: %s: %s", desc["path"], e)
    return None


def run_headless():
    from PyQt5.QtCore import QLockFile
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    os.makedirs(config_dir(), exist_ok=True)
    lock = QLockFile(os.path.join(config_dir(), "hud-agent.lock"))
    waiting = False
    while not lock.tryLock(0):
        if not waiting:
            logging.info("HUD agent: Vial Armed or another agent is feeding the HUD, waiting")
            waiting = True
        time.sleep(5)
    agent = HudAgent()
    mtime = 0
    dev = None
    next_find = 0
    logging.info("HUD agent: config %s", config_path())
    while True:
        try:
            m = os.path.getmtime(config_path())
        except OSError:
            m = 0
        if m != mtime:
            mtime = m
            agent.reload()
        if agent.send is None:
            if dev is not None:
                dev.close()
                dev = None
                logging.info("HUD agent: keyboard gone")
            if time.monotonic() >= next_find:
                next_find = time.monotonic() + 3
                found = _find()
                if found:
                    dev, text_len = found
                    agent.attach(_raw_write(dev), text_len)
                    logging.info("HUD agent: feeding the keyboard")
        agent.tick()
        time.sleep(0.25)


def autostart_path():
    return os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"),
                        "autostart", "vial-armed-agent.desktop")


def autostart_command():
    exe = os.environ.get("APPIMAGE") or (sys.executable if getattr(sys, "frozen", False) else None)
    if exe is None:
        return None
    return "{} --agent".format(shlex.quote(exe))


def set_autostart(on):
    path = autostart_path()
    if not on:
        if os.path.exists(path):
            os.remove(path)
        return
    cmd = autostart_command()
    if cmd is None:
        raise RuntimeError("run from the AppImage or the installed package to set up autostart")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=vial-armed HUD agent\n"
                "Comment=Feeds the keyboard's screen with this computer's metrics\n"
                "Exec={}\nTerminal=false\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\n".format(cmd))
