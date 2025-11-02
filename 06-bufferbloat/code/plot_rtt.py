#!/usr/bin/env python3
"""
Overlay RTT over time for multiple datasets (e.g., rtt-20/100/200).

Key features to prevent prior issues:
- Robust parser that sorts samples by timestamp to avoid out-of-order artifacts.
- Time alignment modes: individual (default) or global.
- Duration trimming (default 60s) so x-axis starts at ~0 and stays bounded.
- Log-scaled y-axis (typical for RTT) can be toggled off with --linear.

Usage examples:
  python3 plot_rtt.py \
    --dirs rtt-20 rtt-100 rtt-200 \
    --labels rtt-20 rtt-100 rtt-200 \
    --duration 60 \
    --out rtt_compare_60s.png

"""

import argparse
import os
import re
from typing import List, Tuple

import matplotlib.pyplot as plt


def parse_rtt_file(path: str) -> Tuple[List[float], List[float]]:
    """Parse rtt.txt with lines like:
    "1761830788.548412, 64 bytes from 10.0.2.22: icmp_seq=1 ttl=63 time=45.0 ms"

    Returns:
        ts: list of absolute timestamps (seconds, float)
        rtt_ms: list of RTT values in milliseconds (float)
    """
    ts: List[float] = []
    rtt_ms: List[float] = []
    if not os.path.isfile(path):
        raise FileNotFoundError(f"rtt file not found: {path}")

    # Capture leading timestamp and trailing 'time=... ms'
    pattern = re.compile(r"^\s*([0-9]+\.[0-9]+)\s*,.*?time=([0-9]+\.?[0-9]*)\s*ms\b")
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            m = pattern.search(line)
            if not m:
                continue
            try:
                t = float(m.group(1))
                r = float(m.group(2))
            except ValueError:
                continue
            ts.append(t)
            rtt_ms.append(r)

    # Sort by timestamp to avoid out-of-order artifacts
    pairs = sorted(zip(ts, rtt_ms), key=lambda x: x[0])
    if not pairs:
        return [], []
    ts_sorted, rtt_sorted = zip(*pairs)
    return list(ts_sorted), list(rtt_sorted)


def align_time(series_ts: List[List[float]], mode: str = 'individual') -> List[List[float]]:
    """Normalize timestamps to start from 0 either per-series (individual) or globally.

    Args:
        series_ts: list of timestamp lists (absolute seconds)
        mode: 'individual' | 'global'

    Returns:
        List of normalized timestamp lists
    """
    if not series_ts:
        return []
    if mode not in ('individual', 'global'):
        raise ValueError("mode must be 'individual' or 'global'")

    if mode == 'global':
        global_t0 = min((ts[0] for ts in series_ts if ts), default=0.0)
        return [[t - global_t0 for t in ts] for ts in series_ts]
    else:
        out: List[List[float]] = []
        for ts in series_ts:
            if ts:
                t0 = ts[0]
                out.append([t - t0 for t in ts])
            else:
                out.append([])
        return out


def trim_duration(xs: List[float], ys: List[float], duration: float) -> Tuple[List[float], List[float]]:
    """Trim series to the given duration in seconds (inclusive)."""
    if duration is None or duration <= 0:
        return xs, ys
    n = 0
    for i, x in enumerate(xs):
        if x <= duration:
            n = i + 1
        else:
            break
    return xs[:n], ys[:n]


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    parser = argparse.ArgumentParser(description='Overlay RTT over time for multiple datasets.')
    parser.add_argument('--dirs', nargs='+', default=['rtt-20', 'rtt-100', 'rtt-200'],
                        help='Directories containing rtt.txt (relative to script dir or absolute).')
    parser.add_argument('--labels', nargs='+', default=None,
                        help='Labels for the legend; defaults to directory names.')
    parser.add_argument('--duration', type=float, default=60.0,
                        help='Duration window in seconds to plot (default: 60). Use <=0 to disable.')
    parser.add_argument('--align', choices=['individual', 'global'], default='individual',
                        help="Time alignment: 'individual' (default) subtract each series t0; or 'global'.")
    parser.add_argument('--out', default=os.path.join(script_dir, 'rtt_compare.png'),
                        help='Output image path (PNG, SVG, etc.). Default: rtt_compare.png next to this script.')
    parser.add_argument('--linear', action='store_true',
                        help='Use linear y-scale instead of log.')

    args = parser.parse_args()

    dirs: List[str] = []
    for d in args.dirs:
        if os.path.isabs(d):
            dirs.append(d)
        else:
            dirs.append(os.path.join(script_dir, d))

    labels = args.labels if args.labels else [os.path.basename(d) for d in dirs]
    if len(labels) != len(dirs):
        raise SystemExit('Number of labels must match number of dirs')

    # Load all series
    all_ts: List[List[float]] = []
    all_rtt: List[List[float]] = []
    for d in dirs:
        path = os.path.join(d, 'rtt.txt')
        ts, rtt = parse_rtt_file(path)
        all_ts.append(ts)
        all_rtt.append(rtt)

    # Align time
    all_tx = align_time(all_ts, mode=args.align)

    # Trim and plot
    plt.figure(figsize=(10, 5))
    for label, xs, ys in zip(labels, all_tx, all_rtt):
        if not xs or not ys:
            continue
        if args.duration and args.duration > 0:
            xs, ys = trim_duration(xs, ys, args.duration)
        plt.plot(xs, ys, label=label, linewidth=1.5)

    plt.xlabel('Time (s)')
    plt.ylabel('RTT (ms)')
    if not args.linear:
        plt.yscale('log')
    plt.grid(True, which='both', linestyle='--', alpha=0.4)
    plt.legend(loc='upper left')
    plt.tight_layout()

    out_dir = os.path.dirname(args.out)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir, exist_ok=True)
    plt.savefig(args.out)
    print(f"Saved figure to: {args.out}")


if __name__ == '__main__':
    main()
