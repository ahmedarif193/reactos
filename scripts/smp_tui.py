#!/usr/bin/env python3
"""
SMP boot analyzer / TUI for ReactOS ARM64.

Parses the kernel serial log (default /tmp/freeldr_arm64.log) produced by an
instrumented SMP boot and surfaces the KPIs needed to understand SMP stalls:

  * per-CPU heartbeats from the timer ISR:
        SMPHB cpu=N tick=T ipi=I park=P wake=W next=PTR ready=R idle=IDLE
    -> tick rate (timer liveness), ipi service count, park/wake balance,
       a NextThread that a core is failing to pick up.
  * boot-progress breadcrumbs:  [BCRUMB] ...
  * events: driver loads, KdDriver delete, crashes/asserts/unhandled exceptions.
  * stall detection (no new serial for a while) + an auto-diagnosis.

Usage:
    scripts/smp_tui.py                     # live curses TUI, follows the log
    scripts/smp_tui.py --once              # parse current log, print summary, exit
    scripts/smp_tui.py -f /path/to.log     # custom log path
    scripts/smp_tui.py --stall 8           # seconds of silence => "STALLED"
"""

import argparse
import os
import re
import sys
import time

DEFAULT_LOG = "/tmp/freeldr_arm64.log"

# ---- line patterns -------------------------------------------------------
RE_TS      = re.compile(r"^\[\s*(\d+\.\d+)\]")
RE_SMPHB   = re.compile(
    r"SMPHB\s+cpu=(\d+)\s+tick=(\d+)\s+ipi=(\d+)\s+park=(\d+)\s+wake=(\d+)"
    r"\s+next=(\S+)\s+ready=(\S+)\s+idle=(\S+)")
RE_BCRUMB  = re.compile(r"\[BCRUMB\]\s*(.*?)\s*$")
RE_LOADING = re.compile(r"Loading:\s+(\S+)")
RE_DELDRV  = re.compile(r"Deleting driver object '([^']*)'")
RE_CRASH   = re.compile(r"(Fatal System Error[^\n]*|Unhandled Kernel Mode[^\n]*|"
                        r"\*\*\* Assertion failed[^\n]*|UNHANDLED ESR[^\n]*|"
                        r"Code [0-9A-Fa-f]{6,}[^\n]*)")


def ptr_nonzero(s):
    """True if a printed pointer/value is non-zero (handles 0x.., 000.., FFFF..)."""
    t = s.strip().lower()
    if t.startswith("0x"):
        t = t[2:]
    t = t.strip("()")
    if not t:
        return False
    try:
        return int(t, 16) != 0
    except ValueError:
        return False


class State:
    def __init__(self, stall_secs):
        self.cpus = {}            # cpu -> dict(tick,ipi,park,wake,next,ready,idle,bts, prev_tick,prev_bts, n)
        self.breadcrumbs = []     # (bts, text)
        self.events = []          # (bts, kind, text)
        self.last_boot_ts = None  # latest kernel timestamp seen
        self.last_change_wall = time.monotonic()
        self.stall_secs = stall_secs
        self.crashed = None       # crash text if any
        self.total_lines = 0
        self.deadlock = []        # SMPWD watchdog thread-dump lines

    def feed(self, line, boot_ts_hint=None):
        self.total_lines += 1
        m = RE_TS.search(line)
        bts = float(m.group(1)) if m else boot_ts_hint
        if bts is not None:
            self.last_boot_ts = bts

        if "SMPWD" in line:
            # watchdog deadlock dump line
            self.deadlock.append(line.split("SMPWD", 1)[1].strip())
            self._touch()
            return

        hb = RE_SMPHB.search(line)
        if hb:
            cpu = int(hb.group(1))
            tick = int(hb.group(2))
            c = self.cpus.get(cpu)
            if c is None:
                c = dict(prev_tick=None, prev_bts=None, n=0)
                self.cpus[cpu] = c
            # rate uses the previous sample
            c["prev_tick"], c["prev_bts"] = c.get("tick"), c.get("bts")
            c["tick"] = tick
            c["ipi"]  = int(hb.group(3))
            c["park"] = int(hb.group(4))
            c["wake"] = int(hb.group(5))
            c["next"] = hb.group(6)
            c["ready"] = hb.group(7)
            c["idle"] = hb.group(8)
            c["bts"] = bts
            c["wall"] = time.monotonic()
            c["n"] += 1
            self._touch()
            return

        bc = RE_BCRUMB.search(line)
        if bc:
            self.breadcrumbs.append((bts, bc.group(1)))
            self._touch()
            return

        cr = RE_CRASH.search(line)
        if cr:
            self.crashed = cr.group(1).strip()
            self.events.append((bts, "CRASH", cr.group(1).strip()))
            self._touch()
            return

        dd = RE_DELDRV.search(line)
        if dd:
            self.events.append((bts, "drvdel", dd.group(1)))
            self._touch()
            return

        ld = RE_LOADING.search(line)
        if ld:
            self.events.append((bts, "load", ld.group(1)))
            self._touch()
            return

    def _touch(self):
        self.last_change_wall = time.monotonic()

    def idle_wall(self):
        return time.monotonic() - self.last_change_wall

    def status(self):
        if self.crashed:
            return "CRASHED", self.crashed
        if self.cpus and self.idle_wall() >= self.stall_secs:
            return "STALLED", "no serial for %.0fs" % self.idle_wall()
        if self.last_boot_ts is None:
            return "WAITING", "no log yet"
        return "BOOTING", ""

    def cpu_rate(self, c):
        if c.get("prev_tick") is None or c.get("prev_bts") is None or c.get("bts") is None:
            return None
        dt = c["bts"] - c["prev_bts"]
        if dt <= 0:
            return None
        return (c["tick"] - c["prev_tick"]) / dt

    def diagnose(self):
        st, _ = self.status()
        if st == "CRASHED":
            return "CRASH: " + (self.crashed or "")
        if st != "STALLED" or not self.cpus:
            return ""
        # At a stall: compare each CPU's last heartbeat boot-time to the latest.
        latest = max((c.get("bts") or 0) for c in self.cpus.values())
        froze_early, with_next, alive = [], [], []
        for cpu, c in sorted(self.cpus.items()):
            bts = c.get("bts") or 0
            lag = latest - bts
            if lag > 0.5:
                froze_early.append((cpu, lag))
            else:
                alive.append(cpu)
            if c.get("next") and ptr_nonzero(c["next"]):
                with_next.append(cpu)
        bits = []
        if froze_early:
            bits.append("timer STOPPED on cpu " +
                        ", ".join("%d(%.1fs early)" % (c, l) for c, l in froze_early) +
                        " -> timer-delivery bug")
        if with_next:
            bits.append("cpu %s hold a NextThread that never ran -> lost wakeup"
                        % ",".join(map(str, with_next)))
        if not froze_early and len(alive) == len(self.cpus):
            bits.append("all timers still ticking but boot wedged -> pure lost-wakeup / deadlock "
                        "(check ready=/idle=)")
        return " | ".join(bits) if bits else "stalled; inspect per-CPU table"


# ---- text (--once) renderer ---------------------------------------------
def render_text(st):
    out = []
    status, detail = st.status()
    out.append("SMP analyzer  status=%s  %s   last_boot=%.3fs  idle=%.1fs  lines=%d" % (
        status, detail,
        st.last_boot_ts or 0.0, st.idle_wall(), st.total_lines))
    out.append("")
    out.append("PER-CPU HEARTBEATS")
    out.append("  cpu   tick    d_tick/s   ipi    park    wake   p-w   next            ready   idle    last_boot")
    for cpu, c in sorted(st.cpus.items()):
        rate = st.cpu_rate(c)
        out.append("  %3d  %7s  %8s  %5s  %6s  %6s  %4s  %-14s  %-6s  %-6s  %.3f" % (
            cpu, c.get("tick", "?"),
            ("%.1f" % rate) if rate is not None else "  -",
            c.get("ipi", "?"), c.get("park", "?"), c.get("wake", "?"),
            (c.get("park", 0) - c.get("wake", 0)) if "park" in c and "wake" in c else "?",
            c.get("next", "?"), c.get("ready", "?"), c.get("idle", "?"),
            c.get("bts") or 0.0))
    out.append("")
    out.append("BOOT PROGRESS (last breadcrumbs)")
    for bts, txt in st.breadcrumbs[-10:]:
        out.append("  %8s  %s" % (("%.3f" % bts) if bts is not None else "    -", txt))
    out.append("")
    out.append("EVENTS (recent)")
    for bts, kind, txt in st.events[-8:]:
        out.append("  %8s  [%s] %s" % (("%.3f" % bts) if bts is not None else "    -", kind, txt))
    if st.deadlock:
        out.append("")
        out.append("DEADLOCK WATCHDOG DUMP (SMPWD) — blocked threads / wait state")
        for d in st.deadlock[-40:]:
            out.append("  " + d)
    out.append("")
    diag = st.diagnose()
    if diag:
        out.append("DIAGNOSIS: " + diag)
    return "\n".join(out)


def parse_whole(path, stall):
    st = State(stall)
    if os.path.exists(path):
        with open(path, "r", errors="replace") as f:
            for line in f:
                st.feed(line.rstrip("\n"))
    return st


# ---- curses live TUI -----------------------------------------------------
def run_curses(path, stall):
    import curses

    def follow(stdscr):
        curses.curs_set(0)
        stdscr.nodelay(True)
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_GREEN, -1)
        curses.init_pair(2, curses.COLOR_RED, -1)
        curses.init_pair(3, curses.COLOR_YELLOW, -1)
        curses.init_pair(4, curses.COLOR_CYAN, -1)
        C_GRN, C_RED, C_YEL, C_CYN = (curses.color_pair(i) for i in (1, 2, 3, 4))

        st = State(stall)
        fpos = 0
        inode = None
        buf = ""

        while True:
            # (re)open + read new bytes
            try:
                stat = os.stat(path)
                if inode != stat.st_ino:
                    inode = stat.st_ino
                    fpos = 0
                    buf = ""
                with open(path, "r", errors="replace") as f:
                    f.seek(fpos)
                    chunk = f.read()
                    fpos = f.tell()
                if chunk:
                    buf += chunk
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        st.feed(line)
            except FileNotFoundError:
                pass

            draw(stdscr, st, C_GRN, C_RED, C_YEL, C_CYN)

            ch = stdscr.getch()
            if ch in (ord("q"), ord("Q")):
                break
            time.sleep(0.25)

    def draw(scr, st, C_GRN, C_RED, C_YEL, C_CYN):
        scr.erase()
        h, w = scr.getmaxyx()
        status, detail = st.status()
        scol = {"BOOTING": C_GRN, "STALLED": C_RED, "CRASHED": C_RED,
                "WAITING": C_YEL}.get(status, 0)

        def put(y, x, s, attr=0):
            if 0 <= y < h:
                scr.addnstr(y, x, s, max(0, w - x - 1), attr)

        put(0, 0, "ReactOS SMP Analyzer  —  %s" % path, curses.A_BOLD)
        put(0, max(0, w - 22), "q=quit", curses.A_DIM)
        put(1, 0, "status: ", curses.A_BOLD)
        put(1, 8, "%-9s %s" % (status, detail), scol | curses.A_BOLD)
        put(1, max(40, w - 40),
            "last_boot=%.3fs  idle=%.1fs" % (st.last_boot_ts or 0.0, st.idle_wall()))

        y = 3
        put(y, 0, "PER-CPU HEARTBEATS", curses.A_BOLD | curses.A_UNDERLINE); y += 1
        put(y, 0, " cpu    tick    dtick/s   ipi    park    wake  p-w  next           ready  idle   state", C_CYN); y += 1
        latest = max((c.get("bts") or 0) for c in st.cpus.values()) if st.cpus else 0
        for cpu, c in sorted(st.cpus.items()):
            rate = st.cpu_rate(c)
            lag = latest - (c.get("bts") or 0)
            stalled_cpu = (status == "STALLED")
            dead = stalled_cpu and lag > 0.5
            hasnext = c.get("next") and ptr_nonzero(c["next"])
            state = "ALIVE"
            attr = C_GRN
            if dead:
                state, attr = "TIMER-DEAD %.1fs" % lag, C_RED
            elif stalled_cpu and hasnext:
                state, attr = "HAS-NEXT(wedged)", C_RED
            elif stalled_cpu:
                state, attr = "idle/stalled", C_YEL
            put(y, 0, " %3d  %7s  %8s  %5s  %6s  %6s  %3s  %-13s  %-5s  %-5s  " % (
                cpu, c.get("tick", "?"),
                ("%.1f" % rate) if rate is not None else "-",
                c.get("ipi", "?"), c.get("park", "?"), c.get("wake", "?"),
                (c.get("park", 0) - c.get("wake", 0)),
                (c.get("next", "?") or "?")[:13], c.get("ready", "?"), c.get("idle", "?")))
            put(y, 86, state, attr | curses.A_BOLD)
            y += 1

        y += 1
        put(y, 0, "BOOT PROGRESS (breadcrumbs)", curses.A_BOLD | curses.A_UNDERLINE); y += 1
        for bts, txt in st.breadcrumbs[-max(3, (h - y - 10)):]:
            put(y, 0, " %8s  %s" % (("%.3f" % bts) if bts is not None else "-", txt))
            y += 1
            if y > h - 9:
                break

        y = max(y, h - 8)
        put(y, 0, "EVENTS", curses.A_BOLD | curses.A_UNDERLINE); y += 1
        for bts, kind, txt in st.events[-4:]:
            a = C_RED if kind == "CRASH" else 0
            put(y, 0, " %8s [%s] %s" % (("%.3f" % bts) if bts is not None else "-", kind, txt), a)
            y += 1

        diag = st.diagnose()
        if diag:
            put(h - 2, 0, "DIAGNOSIS: " + diag, C_RED | curses.A_BOLD)
        scr.refresh()

    curses.wrapper(follow)


def main():
    ap = argparse.ArgumentParser(description="ReactOS ARM64 SMP boot analyzer / TUI")
    ap.add_argument("-f", "--file", default=DEFAULT_LOG, help="serial log path")
    ap.add_argument("--once", action="store_true", help="parse current log, print summary, exit")
    ap.add_argument("--stall", type=float, default=8.0, help="seconds of silence => STALLED")
    args = ap.parse_args()

    if args.once:
        st = parse_whole(args.file, args.stall)
        # In --once mode there is no live tail, so treat the file as final:
        st.last_change_wall = time.monotonic() - 1e6
        print(render_text(st))
        return

    try:
        run_curses(args.file, args.stall)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
