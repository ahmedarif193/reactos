#!/usr/bin/env python3
"""
analyze_cpu_state.py - per-CPU state & KPI analysis of an ARM64 SMP boot log.

Reads the SMP4DBG-instrumented freeldr/ntoskrnl log and reconstructs, per CPU:
  * timer health   - per-CPU architected-timer tick rate over time, and exactly
                     when (and at what count) a core's timer stops firing
  * liveness       - when each core is producing events vs silent
  * activity mix   - what each core spends events on (scheduler, IPI, object mgr)
  * freezes        - global all-CPU silence windows, with the per-CPU state
                     entering and leaving each one

Source of truth for timer health is the "idle-but-work ... ticks=[c0,c1,c2,c3]"
probe (per-CPU KiArm64PerCpuTimerTicks). Everything else is derived from the
cpu=N tag carried by SMP4DBG lines.

Usage: analyze_cpu_state.py [log] [--bucket 2.0] [--freeze 0.5] [--ncpu 4]
Default log: /tmp/freeldr_arm64.log
"""
import argparse
import re
import sys
from collections import defaultdict

TS = re.compile(r'^\[\s*(\d+\.\d+)\]\s?(.*)$')
CPU = re.compile(r'\bcpu=(\d+)')
TICKS = re.compile(r'ticks=\[([0-9,]+)\]')
SMP = 'SMP4DBG'
# event class -> matching tokens in the SMP4DBG message
CLASS = [
    ('sched',  ('ke-unwait', 'ke-deferred-ready', 'idle-switch', 'dispatch-',
                'set-next', 'replace-next', 'ke-wait', 'ke-setevent', 'trace-')),
    ('ipi',    ('ipi-send', 'ipi-service', 'ipi-generic', 'ipi-synch',
                'int-entry-sgi', 'softint', 'hal-send-sgi', 'hal-ack-sgi')),
    ('idle',   ('idle-check', 'idle-work', 'idle-before-wfi', 'idle-after',
                'idle-but-work')),
    ('obj',    ('ob-', 'ntclose')),
    ('mm',     ('mountmgr', 'driveletters', 'auto-assign')),
    ('kdb',    ('kdb',)),
]


def classify(msg):
    for name, toks in CLASS:
        if any(t in msg for t in toks):
            return name
    return 'other'


def load(path):
    """Return (rows, ticks, gdb). rows=[(t,cpu,cls,msg)], ticks=[(t,[c0..])]."""
    rows, ticks = [], []
    gdb = {}
    gdb_re = re.compile(r'CPU#(\d+)\s*\[(\w+)')
    with open(path, errors='replace') as fh:
        for raw in fh:
            raw = raw.rstrip('\n')
            for m in gdb_re.finditer(raw):
                gdb[int(m.group(1))] = m.group(2)
            m = TS.match(raw)
            if not m:
                continue
            t = float(m.group(1))
            body = m.group(2)
            if SMP in body:
                msg = body.split(SMP, 1)[1].strip()
                cm = CPU.search(msg)
                cpu = int(cm.group(1)) if cm else -1
                rows.append((t, cpu, classify(msg), msg))
                tk = TICKS.search(msg)
                if tk:
                    ticks.append((t, [int(x) for x in tk.group(1).split(',')]))
            else:
                rows.append((t, -2, 'kernel', body))   # cpu=-2 => kernel DPRINT
    return rows, ticks, gdb


def fmt_hz(dt, dn):
    return f"{dn/dt:6.1f}Hz" if dt > 0 else "   -  "


def timer_health(ticks, ncpu, end):
    print("\n== PER-CPU TIMER HEALTH (architected timer tick counter) ==")
    if not ticks:
        print("  no ticks=[...] samples (idle-but-work probe never fired)")
        return
    print(f"  {len(ticks)} tick snapshots, {ticks[0][0]:.2f}s .. {ticks[-1][0]:.2f}s")
    for c in range(ncpu):
        seq = [(t, v[c]) for t, v in ticks if c < len(v)]
        if not seq:
            continue
        first_t, first_n = seq[0]
        last_t, last_n = seq[-1]
        # last time the counter advanced
        adv_t, adv_n = first_t, first_n
        for t, n in seq:
            if n > adv_n:
                adv_t, adv_n = t, n
        alive_dt = adv_t - first_t
        hz = (adv_n - first_n) / alive_dt if alive_dt > 0.2 else 0
        dead_for = end - adv_t
        status = "OK" if dead_for < 1.0 else f"*** DIED at t={adv_t:.2f}s (count stuck at {adv_n}, silent {dead_for:.1f}s) ***"
        print(f"  cpu{c}: {first_n:>6}@{first_t:5.1f}s -> {adv_n:>6}@{adv_t:5.1f}s "
              f"| ~{hz:5.0f}Hz alive | {status}")


def liveness(rows, ncpu, end):
    print("\n== PER-CPU LIVENESS & ACTIVITY ==")
    by_cpu = defaultdict(list)
    mix = defaultdict(lambda: defaultdict(int))
    for t, cpu, cls, _ in rows:
        if cpu >= 0:
            by_cpu[cpu].append(t)
            mix[cpu][cls] += 1
    for c in range(ncpu):
        ts = by_cpu.get(c)
        if not ts:
            print(f"  cpu{c}: no events")
            continue
        ts.sort()
        gaps = [(ts[i] - ts[i-1], ts[i-1]) for i in range(1, len(ts))]
        maxgap, gapat = max(gaps, default=(0, 0))
        silent_tail = end - ts[-1]
        mixs = ' '.join(f"{k}:{v}" for k, v in sorted(mix[c].items(), key=lambda x: -x[1]))
        print(f"  cpu{c}: {len(ts):>6} ev | first {ts[0]:5.1f}s last {ts[-1]:5.1f}s "
              f"(tail-silent {silent_tail:4.1f}s) | maxgap {maxgap:4.1f}s@{gapat:.1f}s")
        print(f"         mix: {mixs}")


def buckets(rows, ncpu, end, width):
    print(f"\n== CPU ACTIVITY TIMELINE ({width:g}s buckets; digit = log10(events), '.' = idle, K = kernel DPRINT) ==")
    start = min((t for t, *_ in rows), default=0)
    nb = int((end - start) / width) + 1
    grid = [[0]*nb for _ in range(ncpu)]
    kern = [0]*nb
    for t, cpu, _, _ in rows:
        b = int((t - start) / width)
        if 0 <= b < nb:
            if cpu >= 0 and cpu < ncpu:
                grid[cpu][b] += 1
            elif cpu == -2:
                kern[b] += 1
    def cell(n):
        if n == 0:
            return '.'
        d = len(str(n)) - 1
        return str(min(d, 9))
    hdr = ''.join('K' if kern[b] else ' ' for b in range(nb))
    print(f"   kern {hdr}")
    for c in range(ncpu):
        line = ''.join(cell(grid[c][b]) for b in range(nb))
        print(f"   cpu{c} {line}")
    # time axis ticks every 10 buckets
    axis = ''.join((f"{int(start + b*width):<10d}"[:10]) if b % 10 == 0 else '' for b in range(0, nb, 1))
    print(f"   t/s  {axis}")


SCHED_EV = [
    ('wakeups',       'ke-unwait enter'),            # thread woken from wait
    ('readies',       'ke-deferred-ready enter'),    # thread made runnable
    ('select',        'ke-deferred-ready selected'),  # processor chosen
    ('set-next',      'ke-deferred-ready set-next'),  # installed as NextThread
    ('replace-next',  'ke-deferred-ready replace-next'),  # bumped a Standby NextThread
    ('requeued',      'ke-deferred-ready after-requeue'),
    ('ctxsw-idle',    'idle-switch'),                 # idle thread -> real thread
    ('ctxsw-preempt', 'dispatch-before-switch'),      # preemptive switch
    ('setevent',      'ke-setevent locked'),
    ('ipi-send',      'ipi-send '),
    ('ipi-target',    'ipi-send-target'),
    ('ipi-service',   'ipi-service'),
    ('soft-reject',   'softint-begin-reject'),        # DPC SGI rejected at IRQL
]


def scheduler(rows, ncpu, end):
    print("\n== SCHEDULER STATS ==")
    glob = defaultdict(int)
    per = defaultdict(lambda: defaultdict(int))
    xcpu = 0          # cross-CPU hand-offs (source != target)
    tgt = defaultdict(int)   # IPI/handoff target distribution
    for t, cpu, cls, msg in rows:
        for key, pat in SCHED_EV:
            if msg.startswith(pat):
                glob[key] += 1
                if cpu >= 0:
                    per[cpu][key] += 1
                if key in ('set-next', 'replace-next', 'ipi-target'):
                    mt = re.search(r'target=(\d+)', msg)
                    if mt:
                        tgt[int(mt.group(1))] += 1
                        if cpu >= 0 and int(mt.group(1)) != cpu:
                            xcpu += 1
                break
    span = end - min(t for t, *_ in rows)
    print(f"  {'event':13}{'total':>8}{'/s':>7}   " + ''.join(f'cpu{c:<5}' for c in range(ncpu)))
    for key, _ in SCHED_EV:
        if glob.get(key):
            pc = ''.join(f"{per[c].get(key,0):<8}" for c in range(ncpu))
            print(f"  {key:13}{glob[key]:>8}{glob[key]/span:>7.1f}   {pc}")
    print("  --")
    print(f"  context switches / CPU: " +
          ' '.join(f"cpu{c}={per[c].get('ctxsw-idle',0)+per[c].get('ctxsw-preempt',0)}"
                   for c in range(ncpu)))
    print(f"  cross-CPU hand-offs (target!=source): {xcpu}")
    print(f"  hand-off / IPI target distribution: " +
          ' '.join(f"cpu{c}={tgt.get(c,0)}" for c in range(ncpu)))


def freezes(rows, end, thr, ncpu):
    print(f"\n== GLOBAL FREEZES (all-CPU silence >= {thr:g}s) ==")
    times = sorted(t for t, *_ in rows)
    seen = []
    for i in range(1, len(times)):
        d = times[i] - times[i-1]
        if d >= thr:
            seen.append((d, times[i-1], times[i]))
    total = sum(d for d, *_ in seen)
    span = times[-1] - times[0] if times else 1
    print(f"  {len(seen)} freezes summing {total:.1f}s = {100*total/span:.0f}% of {span:.1f}s boot")
    # which cpu logged last before / first after each big freeze
    for d, a, b in sorted(seen, key=lambda x: -x[0])[:10]:
        before = [r for r in rows if r[0] == a]
        after = [r for r in rows if r[0] == b]
        bc = before[-1][1] if before else '?'
        ac = after[0][1] if after else '?'
        print(f"   {d:5.2f}s  {a:6.2f}->{b:6.2f}  last-before=cpu{bc}  first-after=cpu{ac}")


def sanity(rows, ticks, ncpu, end):
    print("\n== SANITY / ANOMALIES (the actual state) ==")
    flags = []
    start = min(t for t, *_ in rows)
    span = end - start

    rates = {}
    if ticks:
        for c in range(ncpu):
            seq = [(t, v[c]) for t, v in ticks if c < len(v)]
            if not seq:
                continue
            adv_t, adv_n, first = seq[0][0], seq[0][1], seq[0]
            for t, n in seq:
                if n > adv_n:
                    adv_t, adv_n = t, n
            dt = adv_t - first[0]
            if dt > 1:
                rates[c] = (adv_n - first[1]) / dt
            if end - adv_t > 2.0:
                flags.append(f"cpu{c} TIMER DIED at t={adv_t:.1f}s (no tick for {end-adv_t:.0f}s) "
                             f"=> core can only be woken by IPIs, not its 10ms scheduler tick")
        if rates:
            med = sorted(rates.values())[len(rates)//2]
            for c, r in sorted(rates.items()):
                if r > med * 1.8:
                    flags.append(f"cpu{c} timer runs HOT {r:.0f}Hz vs ~{med:.0f}Hz median "
                                 f"(per-CPU period inconsistent in KiArm64TimerIsr)")

    last = {}
    for t, cpu, *_ in rows:
        if cpu >= 0:
            last[cpu] = t
    for c in range(ncpu):
        if c in last and end - last[c] > 5.0:
            flags.append(f"cpu{c} went SILENT at t={last[c]:.1f}s (no events for last {end-last[c]:.0f}s)")

    times = sorted(t for t, *_ in rows)
    fz = sum(times[i]-times[i-1] for i in range(1, len(times)) if times[i]-times[i-1] >= 0.5)
    if span and fz/span > 0.15:
        flags.append(f"system FROZE {100*fz/span:.0f}% of the boot (every core idle, no runnable work reaching it)")

    # scheduling skew (instrumented context switches; gated sample, but skew is real)
    sw = defaultdict(int)
    for t, cpu, cls, msg in rows:
        if cpu >= 0 and (msg.startswith('idle-switch') or msg.startswith('dispatch-before-switch')):
            sw[cpu] += 1
    if sum(sw.values()) > 0:
        tot = sum(sw.values())
        on0 = sw.get(0, 0)
        if tot and on0/tot > 0.7:
            flags.append(f"scheduling SKEWED to cpu0: {on0}/{tot} sampled context-switches on cpu0, "
                         f"APs={[sw.get(c,0) for c in range(1,ncpu)]} (hand-offs not landing on APs)")

    if not flags:
        print("  no anomalies detected")
    for f in flags:
        print(f"  [!] {f}")
    print("  (note: idle-switch / ke-unwait / ipi-send logs are gated by KiSmp4HandoffTraceCount,")
    print("   so absolute scheduler counts are a biased SAMPLE; timer-ticks & liveness are ungated.)")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('log', nargs='?', default='/tmp/freeldr_arm64.log')
    ap.add_argument('--bucket', type=float, default=2.0)
    ap.add_argument('--freeze', type=float, default=0.5)
    ap.add_argument('--ncpu', type=int, default=4)
    a = ap.parse_args(argv)

    rows, ticks, gdb = load(a.log)
    if not rows:
        print(f"no timestamped lines in {a.log}")
        return 1
    end = max(t for t, *_ in rows)
    start = min(t for t, *_ in rows)
    smp = sum(1 for _, c, *_ in rows if c != -2)
    print(f"log: {a.log}")
    print(f"boot span: {start:.2f}s .. {end:.2f}s ({end-start:.1f}s) | "
          f"{len(rows)} lines ({smp} SMP4DBG, {len(rows)-smp} kernel)")
    if gdb:
        print("final GDB state: " + ' '.join(f"cpu{k}={v}" for k, v in sorted(gdb.items())))

    timer_health(ticks, a.ncpu, end)
    liveness(rows, a.ncpu, end)
    scheduler(rows, a.ncpu, end)
    buckets(rows, a.ncpu, end, a.bucket)
    freezes(rows, end, a.freeze, a.ncpu)
    sanity(rows, ticks, a.ncpu, end)
    return 0


if __name__ == '__main__':
    sys.exit(main())
