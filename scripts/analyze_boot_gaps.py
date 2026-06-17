#!/usr/bin/env python3
"""
analyze_boot_gaps.py - find time gaps in a freeldr/ntoskrnl boot log.

Each log line is prefixed with a kernel timestamp like "[   62.220875] ...".
A "gap" is a jump between two consecutive timestamped lines that is larger
than --threshold seconds: that is where the boot actually spent wall-clock
time (a slow operation, a near-stall, a lock that eventually released, or the
final hang before the watchdog dump).

Usage:
    analyze_boot_gaps.py [log] [--threshold 0.2] [--context 3] [--top 25]
                         [--grep REGEX]   # only count lines matching REGEX
                         [--cpu N]        # only SMP4DBG lines for cpu=N

Default log: /tmp/freeldr_arm64.log
"""
import argparse
import re
import sys

TS_RE = re.compile(r'^\[\s*(\d+\.\d+)\]\s?(.*)$')


def load(path, grep=None, cpu=None):
    """Return list of (timestamp, raw_line) for lines that carry a timestamp."""
    grep_re = re.compile(grep) if grep else None
    cpu_tok = f"cpu={cpu}" if cpu is not None else None
    rows = []
    with open(path, errors='replace') as fh:
        for raw in fh:
            raw = raw.rstrip('\n')
            m = TS_RE.match(raw)
            if not m:
                continue
            if grep_re and not grep_re.search(raw):
                continue
            if cpu_tok and cpu_tok not in raw:
                continue
            rows.append((float(m.group(1)), raw))
    return rows


def find_gaps(rows, threshold):
    """Yield (delta, idx) where rows[idx] follows a jump > threshold."""
    out = []
    for i in range(1, len(rows)):
        delta = rows[i][0] - rows[i - 1][0]
        if delta >= threshold:
            out.append((delta, i))
    return out


def fmt(rows, i):
    return f"  [{rows[i][0]:11.6f}] {rows[i][1][rows[i][1].find(']') + 2:]}"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('log', nargs='?', default='/tmp/freeldr_arm64.log')
    ap.add_argument('--threshold', type=float, default=0.2,
                    help='minimum gap in seconds to report (default 0.2)')
    ap.add_argument('--context', type=int, default=3,
                    help='log lines to show before/after each gap (default 3)')
    ap.add_argument('--top', type=int, default=25,
                    help='show only the N largest gaps (default 25)')
    ap.add_argument('--grep', default=None,
                    help='only consider lines matching this regex')
    ap.add_argument('--cpu', type=int, default=None,
                    help='only SMP4DBG lines for this cpu=N')
    args = ap.parse_args(argv)

    rows = load(args.log, args.grep, args.cpu)
    if len(rows) < 2:
        print(f"No timestamped lines in {args.log} (matched {len(rows)}).")
        return 1

    span = rows[-1][0] - rows[0][0]
    gaps = find_gaps(rows, args.threshold)
    total_gap = sum(d for d, _ in gaps)
    print(f"log:        {args.log}")
    print(f"lines:      {len(rows)} timestamped"
          + (f"  (grep={args.grep!r})" if args.grep else "")
          + (f"  (cpu={args.cpu})" if args.cpu is not None else ""))
    print(f"span:       {rows[0][0]:.3f}s .. {rows[-1][0]:.3f}s  ({span:.3f}s)")
    print(f"gaps>= {args.threshold}s: {len(gaps)}  "
          f"summing {total_gap:.3f}s ({100 * total_gap / span:.0f}% of the run)")
    print('=' * 78)

    for rank, (delta, i) in enumerate(
            sorted(gaps, key=lambda g: -g[0])[:args.top], 1):
        print(f"\n#{rank}  GAP {delta:.3f}s  at {rows[i-1][0]:.6f}s -> "
              f"{rows[i][0]:.6f}s")
        lo = max(0, i - args.context)
        for j in range(lo, i):
            print(fmt(rows, j))
        print(f"  ----- {delta:.3f}s gap -----")
        hi = min(len(rows), i + args.context)
        for j in range(i, hi):
            print(fmt(rows, j))
    return 0


if __name__ == '__main__':
    sys.exit(main())
