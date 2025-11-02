#!/usr/bin/env python3
import argparse
import os
import re
from typing import List, Tuple

import matplotlib.pyplot as plt


def parse_rtt_file(path: str) -> Tuple[List[float], List[float]]:
	"""
	Parse rtt.txt lines of the form:
	  <timestamp>, 64 bytes from ... time=<ms> ms

	Returns:
	- times: relative seconds from the first sample
	- rtts: RTT values in milliseconds
	"""
	ts: List[float] = []
	rtts: List[float] = []
	# timestamp at line start, then ... time=<num> ms
	pat = re.compile(r"^\s*([0-9]+\.[0-9]+),.*?time=\s*([0-9]+\.?[0-9]*)\s*ms\s*$")
	with open(path, 'r') as f:
		for line in f:
			m = pat.match(line)
			if not m:
				continue
			t = float(m.group(1))
			val_ms = float(m.group(2))
			ts.append(t)
			rtts.append(val_ms)

	if not ts:
		return [], []

	t0 = ts[0]
	rel = [t - t0 for t in ts]
	# Ensure monotonic time order in case writes were out-of-order
	pairs = sorted(zip(rel, rtts), key=lambda p: p[0])
	rel_sorted, rtt_sorted = [p[0] for p in pairs], [p[1] for p in pairs]
	return rel_sorted, rtt_sorted


def main():
	parser = argparse.ArgumentParser(description="Plot RTT under different AQM algorithms")
	parser.add_argument('--duration', type=int, default=60, help='x-axis range in seconds (default: 60)')
	parser.add_argument('--out', type=str, default='mitigation_rtt.png', help='output image filename')
	args = parser.parse_args()

	base_dir = os.path.dirname(os.path.abspath(__file__))
	datasets = [
		("codel", os.path.join(base_dir, 'codel', 'rtt.txt'), 'lightgreen', 'CoDel'),
		("red", os.path.join(base_dir, 'red', 'rtt.txt'), 'red', 'RED'),
		("taildrop", os.path.join(base_dir, 'taildrop', 'rtt.txt'), 'blue', 'Tail Drop'),
	]

	plt.figure(figsize=(10, 4))

	any_plotted = False
	for key, path, color, label in datasets:
		if not os.path.exists(path):
			print(f"Skip {label}: file not found: {path}")
			continue
		t, y = parse_rtt_file(path)
		if not t:
			print(f"Skip {label}: no valid data")
			continue
		# Trim to duration
		tt: List[float] = []
		yy: List[float] = []
		for ti, yi in zip(t, y):
			if 0 <= ti <= args.duration:
				tt.append(ti)
				yy.append(max(yi, 1e-3))  # avoid zero in log scale
		if not tt:
			print(f"Skip {label}: no data in [0,{args.duration}]s")
			continue
		plt.plot(tt, yy, color=color, label=label, linewidth=1.2)
		any_plotted = True

	plt.xlim(0, args.duration)
	plt.yscale('log')
	plt.xlabel('Time (s)')
	plt.ylabel('RTT (ms)')
	plt.title('RTT under AQM algorithms')
	plt.legend(loc='upper left')
	plt.grid(True, which='both', linestyle='--', alpha=0.3)

	if not any_plotted:
		print('No data plotted; abort saving.')
		return

	out_path = os.path.join(base_dir, args.out)
	plt.tight_layout()
	plt.savefig(out_path, dpi=150)
	print(f"Saved figure to {out_path}")


if __name__ == '__main__':
	main()

