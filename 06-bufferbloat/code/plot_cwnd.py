#!/usr/bin/env python3
import argparse
import os
import re
from pathlib import Path
from typing import List, Tuple

import matplotlib.pyplot as plt


def parse_cwnd(path: Path) -> Tuple[List[float], List[int]]:
	"""Parse cwnd.txt lines produced by `ss -i` grepping, e.g.
	1762099570.485090,  reno ... cwnd:28 ...
	Returns (times, cwnd_segments), with time as float seconds.
	"""
	ts: List[float] = []
	cw: List[int] = []
	pat = re.compile(r"^\s*([0-9]+\.[0-9]+),.*?\bcwnd:(\d+)\b")
	with path.open() as f:
		for line in f:
			m = pat.search(line)
			if not m:
				continue
			ts.append(float(m.group(1)))
			cw.append(int(m.group(2)))
	# sort by timestamp to avoid out-of-order writes
	pairs = sorted(zip(ts, cw), key=lambda p: p[0])
	if not pairs:
		return [], []
	ts_sorted, cw_sorted = [p[0] for p in pairs], [p[1] for p in pairs]
	return ts_sorted, cw_sorted


def norm_time(series: List[Tuple[List[float], List[float]]], mode: str = 'individual') -> List[Tuple[List[float], List[float]]]:
	"""Normalize times to start at 0.

	mode='individual': each series subtracts its own first timestamp (recommended when datasets are from different runs/dates).
	mode='global':     subtract the global minimum timestamp across all series (useful when datasets were captured simultaneously).
	"""
	if mode == 'global':
		starts = [t[0] for t, _ in series if t]
		t0 = min(starts) if starts else 0.0
		return [([ti - t0 for ti in t], v) for t, v in series]
	else:
		out: List[Tuple[List[float], List[float]]] = []
		for t, v in series:
			if t:
				t0 = t[0]
				out.append(([ti - t0 for ti in t], v))
			else:
				out.append((t, v))
		return out


def main():
	ap = argparse.ArgumentParser(description="Plot cwnd over time for multiple datasets")
	ap.add_argument('--dirs', nargs='*', default=['qlen-20', 'qlen-100', 'qlen-200'],
					help='directories each containing cwnd.txt')
	ap.add_argument('--labels', nargs='*', default=None, help='custom labels for each dir')
	ap.add_argument('--duration', type=float, default=None, help='x-axis max seconds; default uses max available')
	ap.add_argument('--out', default='cwnd_compare.png', help='output image file')
	ap.add_argument('--unit', choices=['segments', 'KB'], default='segments',
					help='y-axis unit: TCP segments or approximate KB (segments*MSS/1024)')
	ap.add_argument('--mss', type=int, default=1448, help='MSS for KB conversion')
	ap.add_argument('--align', choices=['individual', 'global'], default='individual',
					help='time alignment method: individual (each dataset starts at 0) or global (same origin)')
	args = ap.parse_args()

	base = Path(os.path.dirname(os.path.abspath(__file__)))
	dirs = [base / d for d in args.dirs]
	labels = args.labels if args.labels and len(args.labels) == len(dirs) else [d.name for d in dirs]

	data: List[Tuple[List[float], List[float]]] = []
	valid_labels: List[str] = []

	for d, lab in zip(dirs, labels):
		p = d / 'cwnd.txt'
		if not p.exists():
			print(f"skip {lab}: not found -> {p}")
			continue
		t, v = parse_cwnd(p)
		if not t:
			print(f"skip {lab}: no data")
			continue
		if args.unit == 'KB':
			v_plot = [c * args.mss / 1024.0 for c in v]
		else:
			v_plot = v
		data.append((t, v_plot))
		valid_labels.append(lab)

	if not data:
		print('no datasets to plot')
		return

	# time normalization across sets
	data = norm_time(data, mode=args.align)

	# Trim to duration if specified
	if args.duration is not None:
		data_trimmed: List[Tuple[List[float], List[float]]] = []
		for t, v in data:
			tt: List[float] = []
			vv: List[float] = []
			for ti, vi in zip(t, v):
				if 0 <= ti <= args.duration:
					tt.append(ti)
					vv.append(vi)
			data_trimmed.append((tt, vv))
		data = data_trimmed

	colors = ['#1f77b4', '#d62728', '#2ca02c', '#9467bd', '#17becf']
	plt.figure(figsize=(10, 4))
	for i, ((t, v), lab) in enumerate(zip(data, valid_labels)):
		color = colors[i % len(colors)]
		plt.plot(t, v, label=lab, color=color, linewidth=1.2)

	plt.xlabel('Time (s)')
	plt.ylabel('cwnd (%s)' % args.unit)
	if args.duration is not None:
		plt.xlim(0, args.duration)
	plt.grid(True, alpha=0.3)
	plt.legend(loc='upper left')

	out_path = base / args.out
	plt.tight_layout()
	plt.savefig(out_path, dpi=150)
	print(f"saved -> {out_path}")


if __name__ == '__main__':
	main()

