#!/usr/bin/env python
"""
Ogham module live monitor.

Reads the firmware's g_telemetry struct over SWD (ST-Link/OpenOCD) and shows it
in a GUI: the 7-segment display (exact segments, even with no physical TM1637),
module state, pot positions, and CV/voltage inputs. Useful for debugging and
calibration. Uses the debugger -- no USB to the Daisy -- so it runs on Eurorack
power with the CV inputs live.

It also LIVE-EDITS the Lo-Fi band-pass sweep (g_lofiConfig) over SWD: sliders for
the sweep endpoints write straight to RAM (mww), and a frequency-response plot
shows the band-pass at several knob positions plus the current knob marker.

Usage:  python ogham_monitor.py
Close the app before flashing (it holds the ST-Link).
"""
import math, os, re, socket, struct, subprocess, sys, time
import tkinter as tk

# --- Config (paths we already use for this board) ---
HERE      = os.path.dirname(os.path.abspath(__file__))
FW_DIR    = os.path.abspath(os.path.join(HERE, ".."))
ELF       = os.path.join(FW_DIR, "build", "ogham_bytebeat.elf")
OPENOCD   = r"C:/Users/steve/AppData/Local/Microsoft/WinGet/Packages/xpack-dev-tools.openocd-xpack_Microsoft.Winget.Source_8wekyb3d8bbwe/xpack-openocd-0.12.0-7/bin/openocd.exe"
NM        = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
TELNET_PORT = 4444
DEFAULT_ADDR = 0x240008C8           # fallback if nm lookup fails
STRUCT_FMT  = "<5I5i10f3I"          # must match struct Telemetry
STRUCT_WORDS = 23
MAGIC = 0x4F474841                  # "OGHA"
POLL_MS = 80                        # ~12 Hz

# --- Lo-Fi band-pass config (g_lofiConfig, written live over SWD) ---
CFG_MAGIC = 0x4C4F4649              # 'LOFI'
CFG_WORDS = 7                       # magic + 6 floats
# (label, struct word index, slider min, max, resolution)
CFG_FIELDS = [
    ("Cutoff start", 1,   20.0, 2000.0, 10.0),
    ("Cutoff end",   2,  200.0, 8000.0, 10.0),
    ("Q start",      3,    0.3,   10.0,  0.1),
    ("Q end",        4,    0.3,   10.0,  0.1),
    ("Wet mix",      5,    0.0,    1.0,  0.01),
    ("Sweep curve",  6,    0.2,    1.0,  0.05),  # <1 front-loads cutoff+mix
]
# CW mapping -- must match firmware ogham_audio_pipeline.cpp
LOFI_CENTER, LOFI_DEAD, POT_MAX = 0.3518, 0.02, 0.8813

# 7-seg bit map: bit0=a,1=b,2=c,3=d,4=e,5=f,6=g,7=dp
SEG_NAMES = "abcdefg"


def find_symbol_addr(name, default=0):
    try:
        out = subprocess.check_output([NM, ELF], stderr=subprocess.DEVNULL).decode()
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3 and parts[2] == name:
                return int(parts[0], 16)
    except Exception as e:
        print("nm lookup failed for %s (%s)" % (name, e))
    return default


def find_telemetry_addr():
    return find_symbol_addr("g_telemetry", DEFAULT_ADDR)


def find_config_addr():
    return find_symbol_addr("g_lofiConfig", 0)


class SwdReader:
    """Holds an OpenOCD server and reads/writes memory over its telnet interface."""
    def __init__(self):
        self.proc = None
        self.sock = None

    def start(self):
        # 4444 is OpenOCD's default telnet port; no need to set it explicitly.
        self.proc = subprocess.Popen(
            [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32h7x.cfg"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # wait for the telnet port to come up
        for _ in range(50):
            try:
                self.sock = socket.create_connection(("127.0.0.1", TELNET_PORT), timeout=1)
                self.sock.settimeout(1.0)
                self._drain()
                return True
            except OSError:
                time.sleep(0.1)
        return False

    def _drain(self):
        try:
            while True:
                if not self.sock.recv(4096):
                    break
        except socket.timeout:
            pass

    # Not anchored at start: telnet prefixes the first data line with a stray
    # \x00 after the command echo, which would otherwise drop it (and the magic).
    LINE_RE = re.compile(r"0x[0-9a-fA-F]+:\s*([0-9a-fA-F ]+)")

    def _flush(self):
        # Discard anything left from a previous response so words can't shift.
        self.sock.setblocking(False)
        try:
            while self.sock.recv(4096):
                pass
        except OSError:
            pass
        self.sock.setblocking(True)
        self.sock.settimeout(1.0)

    def read_words(self, addr, count):
        self._flush()
        self.sock.sendall(("mdw 0x%08x %d\n" % (addr, count)).encode())
        buf = b""
        words = []
        t0 = time.time()
        # Read until we've parsed exactly `count` words (don't rely on the
        # prompt), re-parsing the accumulated buffer each chunk.
        while len(words) < count and time.time() - t0 < 1.0:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk
            words = []
            for line in buf.decode(errors="ignore").splitlines():
                m = self.LINE_RE.search(line)
                if m:
                    words += [int(x, 16) for x in m.group(1).split()]
        return words[:count]

    def write_word(self, addr, value):
        """Write a single 32-bit word to RAM (used to live-edit g_lofiConfig)."""
        self._flush()
        self.sock.sendall(("mww 0x%08x 0x%08x\n" % (addr, value & 0xFFFFFFFF)).encode())
        time.sleep(0.01)
        self._flush()

    def stop(self):
        try:
            if self.sock: self.sock.close()
        except Exception:
            pass
        try:
            if self.proc: self.proc.terminate()
        except Exception:
            pass


class SevenSeg:
    """Draws a 4-digit 7-segment display on a canvas."""
    ON, OFF, BG = "#ff3b30", "#3a0d0b", "#140404"

    def __init__(self, canvas, x, y, w=46, h=84, t=8, gap=20):
        self.c = canvas
        self.digits = []
        for d in range(4):
            ox = x + d * (w + gap)
            self.digits.append(self._build(ox, y, w, h, t))

    def _seg(self, pts):
        return self.c.create_polygon(pts, fill=self.OFF, outline="")

    def _build(self, x, y, w, h, t):
        hh = h / 2
        segs = {}
        def horiz(cx, cy):
            return [cx+t/2, cy, cx+t, cy-t/2, cx+w-t, cy-t/2, cx+w-t/2, cy,
                    cx+w-t, cy+t/2, cx+t, cy+t/2]
        def vert(cx, cy):
            return [cx, cy+t/2, cx+t/2, cy+t, cx+t/2, cy+hh-t, cx, cy+hh-t/2,
                    cx-t/2, cy+hh-t, cx-t/2, cy+t]
        segs['a'] = self._seg(horiz(x, y))
        segs['g'] = self._seg(horiz(x, y+hh))
        segs['d'] = self._seg(horiz(x, y+h))
        segs['f'] = self._seg(vert(x, y))
        segs['b'] = self._seg(vert(x+w, y))
        segs['e'] = self._seg(vert(x, y+hh))
        segs['c'] = self._seg(vert(x+w, y+hh))
        segs['dp'] = self.c.create_oval(x+w+4, y+h-t, x+w+4+t, y+h, fill=self.OFF, outline="")
        return segs

    def set(self, idx, byte):
        d = self.digits[idx]
        for i, name in enumerate(SEG_NAMES):
            self.c.itemconfig(d[name], fill=self.ON if (byte >> i) & 1 else self.OFF)
        self.c.itemconfig(d['dp'], fill=self.ON if (byte >> 7) & 1 else self.OFF)


class App:
    FMIN, FMAX = 20.0, 20000.0
    DBMIN, DBMAX = -42.0, 9.0

    def __init__(self, root, reader, addr, cfg_addr=0):
        self.reader = reader
        self.addr = addr
        self.cfg_addr = cfg_addr
        self.last_potL = None
        root.title("Ogham Monitor")
        root.configure(bg="#1b1b1b")
        self.root = root

        # 7-seg display
        cv = tk.Canvas(root, width=320, height=120, bg=SevenSeg.BG, highlightthickness=0)
        cv.grid(row=0, column=0, columnspan=2, padx=10, pady=10)
        self.seg = SevenSeg(cv, 24, 18)

        # state panel
        self.state = tk.Label(root, justify="left", anchor="w", font=("Consolas", 11),
                              fg="#e0e0e0", bg="#1b1b1b")
        self.state.grid(row=1, column=0, sticky="nw", padx=10)

        # pots / CV bars
        bars = tk.Frame(root, bg="#1b1b1b")
        bars.grid(row=1, column=1, sticky="ne", padx=10)
        self.bars = {}
        for i, name in enumerate(["Pot A", "Pot B", "Pot Rate", "Pot Level",
                                  "Comb A", "Comb B", "CV A adc", "CV B adc"]):
            tk.Label(bars, text=name, width=9, anchor="w", font=("Consolas", 9),
                     fg="#bbb", bg="#1b1b1b").grid(row=i, column=0, sticky="w")
            c = tk.Canvas(bars, width=180, height=14, bg="#222", highlightthickness=0)
            c.grid(row=i, column=1, pady=1)
            rect = c.create_rectangle(0, 0, 0, 14, fill="#4caf50", outline="")
            val = tk.Label(bars, width=7, anchor="e", font=("Consolas", 9),
                           fg="#ddd", bg="#1b1b1b")
            val.grid(row=i, column=2, sticky="e")
            self.bars[name] = (c, rect, val)

        self.status = tk.Label(root, font=("Consolas", 9), fg="#888", bg="#1b1b1b")
        self.status.grid(row=2, column=0, columnspan=2, sticky="w", padx=10, pady=(0,6))

        self.last_counter = None
        if self.cfg_addr:
            self.build_filter_panel(root)
        self.poll()

    # --- Lo-Fi band-pass live editor + response plot ---
    def build_filter_panel(self, root):
        cfg = self.reader.read_words(self.cfg_addr, CFG_WORDS)
        ok = len(cfg) == CFG_WORDS and cfg[0] == CFG_MAGIC
        self.cfg_vals = {}
        panel = tk.Frame(root, bg="#1b1b1b")
        panel.grid(row=3, column=0, columnspan=2, sticky="we", padx=10, pady=(2, 10))

        sl = tk.Frame(panel, bg="#1b1b1b")
        sl.grid(row=0, column=0, sticky="nw")
        tk.Label(sl, text="Band-pass sweep (live edit → RAM)",
                 font=("Consolas", 10, "bold"), fg="#7fd0ff", bg="#1b1b1b"
                 ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 2))
        self.scales = {}
        for i, (name, idx, lo, hi, res) in enumerate(CFG_FIELDS):
            val = struct.unpack("<f", struct.pack("<I", cfg[idx]))[0] if ok else (lo + hi) / 2.0
            self.cfg_vals[idx] = val
            tk.Label(sl, text=name, width=11, anchor="w", font=("Consolas", 9),
                     fg="#bbb", bg="#1b1b1b").grid(row=i + 1, column=0, sticky="w")
            s = tk.Scale(sl, from_=lo, to=hi, resolution=res, orient="horizontal",
                         length=170, bg="#1b1b1b", fg="#ddd", troughcolor="#333",
                         highlightthickness=0, font=("Consolas", 8), sliderlength=14,
                         command=lambda v, idx=idx: self.on_param(idx, float(v)))
            s.set(val)
            s.grid(row=i + 1, column=1, sticky="w")
            self.scales[idx] = s
        if not ok:
            tk.Label(sl, text="(g_lofiConfig not found / bad magic)", fg="#f77",
                     bg="#1b1b1b", font=("Consolas", 8)
                     ).grid(row=99, column=0, columnspan=2, sticky="w")

        self.plot = tk.Canvas(panel, width=380, height=200, bg="#101418",
                              highlightthickness=1, highlightbackground="#333")
        self.plot.grid(row=0, column=1, sticky="ne", padx=(14, 0))
        self.draw_response()

    def on_param(self, idx, val):
        self.cfg_vals[idx] = val
        bits = struct.unpack("<I", struct.pack("<f", val))[0]
        try:
            self.reader.write_word(self.cfg_addr + idx * 4, bits)
        except Exception as e:
            self.status.config(text="write error: %s" % e)
        self.draw_response()

    def draw_response(self):
        if not hasattr(self, "plot"):
            return
        c = self.plot
        c.delete("all")
        W = int(c["width"]); H = int(c["height"])
        L, R, T, B = 30, W - 6, 8, H - 16
        lf0, lf1 = math.log10(self.FMIN), math.log10(self.FMAX)

        def xf(f):
            return L + (math.log10(f) - lf0) / (lf1 - lf0) * (R - L)

        def yf(db):
            db = max(self.DBMIN, min(self.DBMAX, db))
            return T + (self.DBMAX - db) / (self.DBMAX - self.DBMIN) * (B - T)

        for f in (100, 1000, 10000):
            x = xf(f)
            c.create_line(x, T, x, B, fill="#222")
            c.create_text(x, B + 8, text=("%dk" % (f // 1000)) if f >= 1000 else str(f),
                          fill="#666", font=("Consolas", 7))
        for db in (0, -20, -40):
            y = yf(db)
            c.create_line(L, y, R, y, fill="#222")
            c.create_text(L - 3, y, text=str(db), fill="#666", anchor="e", font=("Consolas", 7))

        cs = self.cfg_vals

        def lerp(a, b, t):
            return a + t * (b - a)

        def shaped(cw):  # front-loading curve applied to cutoff + mix (Q stays linear)
            return cw ** max(0.1, cs[6])

        # band-pass magnitude at several CW positions (constant-peak 2nd-order BP)
        for cw, col in ((0.15, "#2a6b4a"), (0.4, "#3a9a6a"), (0.65, "#4ccaa0"),
                        (0.85, "#7fe0d0"), (1.0, "#aef0ff")):
            fc = lerp(cs[1], cs[2], shaped(cw)); Q = lerp(cs[3], cs[4], cw)
            if fc <= 0 or Q <= 0:
                continue
            pts = []
            f = self.FMIN
            while f <= self.FMAX:
                x_ = f / fc
                denom = math.sqrt((1 - x_ * x_) ** 2 + (x_ / Q) ** 2)
                mag = (x_ / Q) / denom if denom > 0 else 0.0
                db = 20 * math.log10(mag) if mag > 1e-4 else -80
                pts += [xf(f), yf(db)]
                f *= 1.05
            if len(pts) >= 4:
                c.create_line(pts, fill=col, width=1)

        # live knob marker (current CW position)
        pL = self.last_potL
        if pL is not None and pL > LOFI_CENTER + LOFI_DEAD:
            cw = (pL - (LOFI_CENTER + LOFI_DEAD)) / (POT_MAX - (LOFI_CENTER + LOFI_DEAD))
            cw = max(0.0, min(1.0, cw))
            fc = lerp(cs[1], cs[2], shaped(cw))
            x = xf(fc)
            c.create_line(x, T, x, B, fill="#ffa000", width=1, dash=(3, 2))
            c.create_text((L + R) / 2, T + 1,
                          text="knob: cw=%.2f  fc=%.0fHz  mix=%.2f" % (cw, fc, shaped(cw) * cs[5]),
                          fill="#ffa000", anchor="n", font=("Consolas", 7))

    def poll(self):
        try:
            words = self.reader.read_words(self.addr, STRUCT_WORDS)
            if len(words) == STRUCT_WORDS:
                raw = b"".join(struct.pack("<I", w) for w in words)
                self.update(struct.unpack(STRUCT_FMT, raw))
        except Exception as e:
            self.status.config(text="read error: %s" % e)
        self.root.after(POLL_MS, self.poll)

    def update(self, v):
        (magic, counter, segs, modeState, ioState, out1, out2, delay,
         pA, pB, rate, extRate, potA, potB, potR, potL,
         cA, cB, cvA, cvB, cpuPeak, cpuPer, syncCount) = v
        if magic != MAGIC:
            self.status.config(text="bad magic 0x%08x (firmware mismatch?)" % magic)
            return
        # 7-seg
        for i in range(4):
            self.seg.set(i, (segs >> (8*i)) & 0xFF)
        # decode state
        funcMode = modeState & 0xFF
        selOut   = (modeState >> 8) & 0xFF
        clean    = (modeState >> 16) & 0xFF
        extClk   = (modeState >> 24) & 0xFF
        gate     = ioState & 0xFF
        clock    = (ioState >> 8) & 0xFF
        mode = "DELAY" if funcMode else "SELECT"
        voice = "Out2" if selOut else "Out1"
        load = 100.0 * cpuPeak / cpuPer if cpuPer else 0
        self.state.config(text=(
            f"Mode    : {mode}   editing {voice}\n"
            f"Out1 fmla: {out1+1:>3}   Out2 fmla: {out2+1:>3}\n"
            f"Delay   : {delay:>3} cycles\n"
            f"Param A : {pA:>3}   Param B : {pB:>3}\n"
            f"Rate    : {rate:5.2f}x   ext-clk: {'ON ' if extClk else 'off'} {extRate:4.2f}\n"
            f"Lo-Fi   : {'CLEAN' if clean else 'active'}\n"
            f"Gate in : {'HI' if gate else 'lo'}   Clock in: {'HI' if clock else 'lo'}\n"
            f"Sync evt: {syncCount}\n"
            f"CPU peak: {load:4.1f}%"
        ))
        # bars (value, display) -- pots/comb are 0..1; CV adc 0..1 -> volts at the pin
        vals = {
            "Pot A": (potA, f"{potA:.3f}"), "Pot B": (potB, f"{potB:.3f}"),
            "Pot Rate": (potR, f"{potR:.3f}"), "Pot Level": (potL, f"{potL:.3f}"),
            "Comb A": (cA, f"{cA:.3f}"), "Comb B": (cB, f"{cB:.3f}"),
            "CV A adc": (cvA, f"{cvA*3.3:.2f}V"), "CV B adc": (cvB, f"{cvB*3.3:.2f}V"),
        }
        for name, (frac, txt) in vals.items():
            c, rect, lbl = self.bars[name]
            frac = max(0.0, min(1.0, frac))
            c.coords(rect, 0, 0, int(180*frac), 14)
            lbl.config(text=txt)
        # live band-pass marker follows the Level knob
        prev = self.last_potL
        self.last_potL = potL
        if self.cfg_addr and (prev is None or abs(potL - prev) > 0.003):
            self.draw_response()
        live = "live" if counter != self.last_counter else "STALE"
        self.last_counter = counter
        self.status.config(text=f"telemetry @0x{self.addr:08x}  counter={counter}  ({live})")


def main():
    addr = find_telemetry_addr()
    cfg_addr = find_config_addr()
    print("Telemetry @ 0x%08x   Config @ 0x%08x" % (addr, cfg_addr))
    reader = SwdReader()
    print("Starting OpenOCD ...")
    if not reader.start():
        print("Could not connect to OpenOCD telnet. Is the ST-Link connected and the board powered?")
        sys.exit(1)
    root = tk.Tk()
    app = App(root, reader, addr, cfg_addr)
    try:
        root.mainloop()
    finally:
        reader.stop()


if __name__ == "__main__":
    main()
