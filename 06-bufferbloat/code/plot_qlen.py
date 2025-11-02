#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
from typing import List, Tuple

import matplotlib.pyplot as plt


def parse_qlen(path: Path) -> Tuple[List[float], List[int]]:
	"""Parse qlen.txt lines of form: <timestamp>, <packets>"""
	ts: List[float] = []
	qs: List[int] = []
	with path.open() as f:
		for line in f:
			if ',' not in line:
				continue
			t_str, v_str = line.split(',', 1)
			try:
				t = float(t_str.strip())
				v = int(v_str.strip())
			except ValueError:
				continue
			ts.append(t)
			qs.append(v)
	pairs = sorted(zip(ts, qs), key=lambda p: p[0])
	if not pairs:
		return [], []
	t_sorted, q_sorted = [p[0] for p in pairs], [p[1] for p in pairs]
	return t_sorted, q_sorted


def norm_time(series: List[Tuple[List[float], List[float]]], mode: str = 'individual') -> List[Tuple[List[float], List[float]]]:
	"""
	Normalize timestamps for multiple series.
	- individual: each series starts at its own first timestamp
	- global: subtract the global minimum timestamp across all series
	"""
	if mode == 'global':
		starts = [t[0] for t, _ in series if t]
		t0 = min(starts) if starts else 0.0
		return [([ti - t0 for ti in t], v) for t, v in series]
	# individual (default)
	out: List[Tuple[List[float], List[float]]] = []
	for t, v in series:
		if t:
			t0 = t[0]
			out.append(([ti - t0 for ti in t], v))
		else:
			out.append((t, v))
	return out


essential_colors = ['#1f77b4', '#d62728', '#2ca02c', '#9467bd', '#17becf']


def main():
	ap = argparse.ArgumentParser(description='Plot queue length over time for multiple datasets')
	ap.add_argument('--dirs', nargs='*', default=['qlen-20', 'qlen-100', 'qlen-200'],
					help='directories each containing qlen.txt')
	ap.add_argument('--labels', nargs='*', default=None, help='custom labels for each dir')
	ap.add_argument('--duration', type=float, default=None, help='x-axis max seconds; default uses full length')
	ap.add_argument('--out', default='qlen_compare.png', help='output image file')
	ap.add_argument('--align', choices=['individual', 'global'], default='individual',
					help='time alignment: individual -> each series starts at 0; global -> keep real offsets')
	args = ap.parse_args()

	base = Path(os.path.dirname(os.path.abspath(__file__)))
	dirs = [base / d for d in args.dirs]
	labels = args.labels if args.labels and len(args.labels) == len(dirs) else [d.name for d in dirs]

	data: List[Tuple[List[float], List[float]]] = []
	valid_labels: List[str] = []

	for d, lab in zip(dirs, labels):
		p = d / 'qlen.txt'
		if not p.exists():
			print(f'skip {lab}: not found -> {p}')
			continue
		t, v = parse_qlen(p)
		if not t:
			print(f'skip {lab}: no data')
			continue
		data.append((t, v))
		valid_labels.append(lab)

	if not data:
		print('no datasets to plot')
		return

	# align time axes
	data = norm_time(data, mode=args.align)

	# optional duration trim
	if args.duration is not None:
		trimmed: List[Tuple[List[float], List[float]]] = []
		for t, v in data:
			tt: List[float] = []
			vv: List[float] = []
			for ti, vi in zip(t, v):
				if 0 <= ti <= args.duration:
					tt.append(ti)
					vv.append(vi)
			trimmed.append((tt, vv))
		data = trimmed

	import matplotlib as mpl
	mpl.rcParams['axes.axisbelow'] = True

	plt.figure(figsize=(10, 4))
	for i, ((t, v), lab) in enumerate(zip(data, valid_labels)):
		color = essential_colors[i % len(essential_colors)]
		plt.plot(t, v, label=lab, color=color, linewidth=1.2)

	plt.xlabel('Time (s)')
	plt.ylabel('Queue length (packets)')
	if args.duration is not None:
		plt.xlim(0, args.duration)
	plt.grid(True, alpha=0.3)
	plt.legend(loc='upper left')

	out_path = base / args.out
	plt.tight_layout()
	plt.savefig(out_path, dpi=150)
	print(f'saved -> {out_path}')


if __name__ == '__main__':
	main()

