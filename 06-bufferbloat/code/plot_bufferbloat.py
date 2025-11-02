import re
import argparse
from pathlib import Path
import matplotlib.pyplot as plt

def parse_cwnd(p: Path):
    ts, cw = [], []
    with p.open() as f:
        for line in f:
            m = re.search(r'^\s*([\d.]+),.*\bcwnd:(\d+)\b', line)
            if m:
                ts.append(float(m.group(1)))
                cw.append(int(m.group(2)))
    return ts, cw

def parse_rtt(p: Path):
    ts, rtt = [], []
    with p.open() as f:
        for line in f:
            m = re.search(r'^\s*([\d.]+),.*time=([\d.]+)\s*ms', line)
            if m:
                ts.append(float(m.group(1)))
                rtt.append(float(m.group(2)))
    return ts, rtt

def parse_qlen(p: Path):
    ts, q = [], []
    with p.open() as f:
        for line in f:
            if ',' in line:
                t, v = line.split(',', 1)
                try:
                    ts.append(float(t.strip()))
                    q.append(int(v.strip()))
                except ValueError:
                    pass
    return ts, q

def norm_time(*series):
    # 按最早时间戳对齐到 0 秒
    starts = [s[0][0] for s in series if s[0]]
    t0 = min(starts) if starts else 0.0
    nseries = []
    for t, v in series:
        nseries.append(([ti - t0 for ti in t], v))
    return nseries

def main():
    ap = argparse.ArgumentParser(description="Plot Bufferbloat metrics")
    ap.add_argument("--dir", default=".", help="包含 cwnd.txt/qlen.txt/rtt.txt 的目录")
    ap.add_argument("--mss", type=int, default=1448, help="MSS 字节数（用于把 cwnd 段转换为KB）")
    ap.add_argument("--out", default="bufferbloat_plots.png", help="输出图片路径")
    args = ap.parse_args()

    d = Path(args.dir)
    t_cwnd, v_cwnd = parse_cwnd(d / "cwnd.txt")
    t_qlen, v_qlen = parse_qlen(d / "qlen.txt")
    t_rtt,  v_rtt  = parse_rtt(d / "rtt.txt")

    (t_cwnd, v_cwnd), (t_qlen, v_qlen), (t_rtt, v_rtt) = norm_time(
        (t_cwnd, v_cwnd), (t_qlen, v_qlen), (t_rtt, v_rtt)
    )

    # cwnd 段数 -> KB（近似：段数 * MSS / 1024）
    cwnd_kb = [c * args.mss / 1024.0 for c in v_cwnd]

    fig, ax = plt.subplots(3, 1, figsize=(10, 9), sharex=True)
    # CWND
    ax[0].plot(t_cwnd, cwnd_kb, color="#1f77b4", linewidth=1)
    ax[0].set_ylabel("CWND (KB)")
    ax[0].grid(True, alpha=0.3)

    # Qlen
    ax[1].plot(t_qlen, v_qlen, color="#d62728", linewidth=1)
    ax[1].set_ylabel("Qlen (packets)")
    ax[1].grid(True, alpha=0.3)

    # RTT
    ax[2].plot(t_rtt, v_rtt, color="#9467bd", linewidth=1)
    ax[2].set_ylabel("RTT (ms)")
    ax[2].set_xlabel("Seconds")
    ax[2].grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    print(f"saved -> {args.out}")

if __name__ == "__main__":
    main()