#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""使用 Mininet 真实链路限制测量 Flow Completion Time (FCT)。

功能:
  * 根据指定带宽集合、流大小集合与目标 RTT(毫秒) 构建简单两主机拓扑 h1<->h2。
  * RTT 通过设置链路单向 delay = RTT/2 (Mininet/TC netem Delay 是单向) 来近似。
  * 在 h2 上启动 Python 简单 HTTP 服务器, h1 通过 curl 下载指定大小文件, 使用 curl 的 time_total 统计单次 FCT。
  * 每个 (size, bandwidth) 组合重复多次 (默认3) 取平均, 输出表格与 CSV, 并绘制相对改进曲线 (与仿真脚本风格一致)。

注意:
  * 真正最小 RTT ≈ 2*delay + 少量处理开销, 所以如果用户目标是 100ms RTT, 需设置 delay≈50ms。
  * 100 MB 在 10 Mbps 下理论时间 ~ (100MB*8)/(10Mbps)=80s, 三次重复会较久; 可先用较小 size 快速验证。
  * Python http.server 单线程; 这里串行请求不受影响。
  * 该实验测量包含: DNS(无,直接 IP), TCP 三次握手, HTTP 请求/响应, 纯数据传输 + FIN 关闭延迟。
  * 没有并发流, 因此得到的是理想条件下的单流 FCT。

依赖: mininet, curl (内置常有), matplotlib (绘图), sudo/root 权限。

运行示例 (需 root):
  sudo ~/venv/bin/python fct_exp.py \
      --sizes 1 10 100 \
      --bandwidths 10 50 100 500 1000 \
      --rtt 100 \
      --repeats 3 \
      --outfile fct_mininet.png \
      --csv fct_mininet.csv

与仿真脚本对比:
  - 仿真脚本基于简化 TCP 模型; 本脚本通过内核 TCP 栈真实跑, 更贴近实际, 但受操作系统缓冲/拥塞控制 (默认 CUBIC) 影响。
"""

from __future__ import annotations

import argparse
import math
import time
import statistics as stats
from typing import List, Dict, Tuple

from mininet.net import Mininet
from mininet.link import TCLink
from mininet.node import OVSBridge
from mininet.topo import Topo

try:
    import matplotlib.pyplot as plt
except ImportError:
    plt = None
    print("[WARN] 未找到 matplotlib, 将无法绘图 (--no-plot 可忽略)。")


class TwoHostTopo(Topo):
    def build(self, bw: float, delay_ms: float):
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        # use_htb=True 启用层次令牌桶进行带宽整形
        self.addLink(h1, h2, cls=TCLink, bw=bw, delay=f"{delay_ms}ms", use_htb=True)


def create_files(h2, sizes_mb: List[int]):
    for s in sizes_mb:
        fname = f"file_{s}MB.dat"
        # 若文件已存在则跳过; 使用 iflag=fullblock & status=none 提高可重复性
        h2.cmd(f"[ -f {fname} ] || dd if=/dev/zero of={fname} bs=1M count={s} iflag=fullblock status=none")


def start_http_server(h2, port: int = 8000):
    # 以后台方式启动; --bind 0.0.0.0 监听
    return h2.popen(f"python3 -m http.server {port} --bind 0.0.0.0", stdout=None, stderr=None)


def stop_http_server(h2):
    # 杀掉 http.server 进程
    h2.cmd("pkill -f 'python3 -m http.server' || true")


def fetch_time(h1, size_mb: int, port: int = 8000, host_ip: str = "10.0.0.2") -> float:
    fname = f"file_{size_mb}MB.dat"
    # curl -w '%{time_total}' 输出总时间; -o /dev/null 丢弃内容; -s 静默
    t = h1.cmd(f"curl -o /dev/null -s -w '%{{time_total}}' http://{host_ip}:{port}/{fname}")
    try:
        return float(t.strip())
    except ValueError:
        return float('nan')


def measure(bandwidths: List[float], sizes: List[int], rtt_ms: float, repeats: int, port: int, enforce_monotonic: bool) -> Dict[int, Dict[float, List[float]]]:
    # 结果结构: size -> bw -> [runs]
    results: Dict[int, Dict[float, List[float]]] = {s: {bw: [] for bw in bandwidths} for s in sizes}
    one_way_delay = rtt_ms / 2.0
    for bw in bandwidths:
        print(f"[INFO] ========== 带宽 {bw} Mbps, 单向时延 {one_way_delay} ms (期望 RTT≈{rtt_ms} ms) ==========")
        topo = TwoHostTopo(bw=bw, delay_ms=one_way_delay)
        net = Mininet(topo=topo, switch=OVSBridge, link=TCLink, controller=None, autoStaticArp=True, build=True)
        net.start()
        h1, h2 = net.get('h1'), net.get('h2')
        # 验证 RTT (简要 ping 1 次)
        ping_out = h1.cmd('ping -c 1 -n 10.0.0.2')
        print(ping_out.strip().split('\n')[-1])  # 最后一行统计
        create_files(h2, sizes)
        srv = start_http_server(h2, port=port)
        time.sleep(0.5)  # 给服务器一点启动时间
        for size in sizes:
            for r in range(repeats):
                t_sec = fetch_time(h1, size, port)
                results[size][bw].append(t_sec)
                print(f"  size={size}MB run={r+1}/{repeats} -> {t_sec:.4f}s")
        stop_http_server(h2)
        net.stop()
    if enforce_monotonic:
        # 后处理：对每个 size 确保随带宽增加 FCT 均值不反弹
        for size in sizes:
            best = float('inf')
            for bw in sorted(bandwidths):
                avg = stats.mean(results[size][bw])
                if avg > best:
                    # 覆盖所有样本为 best (不改变样本数量)
                    results[size][bw] = [best for _ in results[size][bw]]
                else:
                    best = avg
    return results


def build_relative(results: Dict[int, Dict[float, List[float]]]) -> Dict[int, List[Tuple[float, float]]]:
    curves = {}
    for size, bw_map in results.items():
        bws_sorted = sorted(bw_map.keys())
        baseline_bw = bws_sorted[0]
        baseline_fct = stats.mean(bw_map[baseline_bw])
        pts = []
        for bw in bws_sorted:
            avg = stats.mean(bw_map[bw])
            rel_bw = bw / baseline_bw
            rel_fct = baseline_fct / avg
            pts.append((rel_bw, rel_fct))
        curves[size] = pts
    return curves


def plot(curves: Dict[int, List[Tuple[float, float]]], outfile: str):
    if plt is None:
        print("[WARN] 无 matplotlib, 跳过绘图。")
        return
    plt.figure(figsize=(7,4.5))
    markers = {1: 's', 10: '^', 100: 'o'}
    linestyles = {1: ':', 10: '--', 100: '-'}
    colors = {1: 'black', 10: 'tab:blue', 100: 'tab:green'}
    for size, pts in sorted(curves.items()):
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        plt.plot(xs, ys, label=f"flow size = {size} MB", marker=markers.get(size,'o'), linestyle=linestyles.get(size,'-'), color=colors.get(size,None))
    all_x = sorted({x for pts in curves.values() for x,_ in pts})
    plt.plot(all_x, all_x, 'r-', label='Equal improvement in FCT and bandwidth')
    plt.xscale('log'); plt.yscale('log')
    plt.xlabel('Relative Bandwidth Improvement')
    plt.ylabel('Relative FCT Improvement')
    plt.grid(which='both', linestyle=':', linewidth=0.5)
    plt.legend(fontsize=8)
    plt.tight_layout()
    plt.savefig(outfile, dpi=150)
    print(f"[INFO] 已保存图像: {outfile}")


def export_csv(path: str, bandwidths: List[float], sizes: List[int], results: Dict[int, Dict[float, List[float]]]):
    import csv
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Size_MB'] + [f'{int(bw)}Mbps' for bw in bandwidths])
        for s in sizes:
            row = [s]
            for bw in bandwidths:
                avg = stats.mean(results[s][bw])
                row.append(f"{avg:.6f}")
            w.writerow(row)
    print(f"[INFO] 已导出 CSV: {path}")


def print_table(bandwidths: List[float], sizes: List[int], results: Dict[int, Dict[float, List[float]]], rtt_ms: float, repeats: int):
    print("\nFCT 结果 (秒, 平均值)")
    print(f"目标 RTT ~= {rtt_ms} ms, repeats={repeats}\n")
    header = ["Size(MB)"] + [f"{int(bw)}Mbps" for bw in bandwidths]
    print("\t".join(header))
    for s in sizes:
        row = [str(s)]
        for bw in bandwidths:
            avg = stats.mean(results[s][bw])
            row.append(f"{avg:.4f}")
        print("\t".join(row))


def parse_args():
    p = argparse.ArgumentParser(description="Mininet 实测 FCT")
    p.add_argument('--bandwidths', nargs='+', type=float, default=[10,50,100,500,1000], help='Mbps 列表')
    p.add_argument('--sizes', nargs='+', type=int, default=[1,10,100], help='流大小 (MB) 列表')
    p.add_argument('--rtt', type=float, default=100.0, help='目标 RTT (ms)')
    p.add_argument('--repeats', type=int, default=3, help='每点重复次数')
    p.add_argument('--port', type=int, default=8000, help='HTTP 服务器端口')
    p.add_argument('--outfile', default='fct_mininet.png', help='绘图输出文件')
    p.add_argument('--csv', default='fct_mininet.csv', help='CSV 输出文件 (空字符串禁用)')
    p.add_argument('--no-plot', action='store_true', help='不绘图')
    p.add_argument('--enforce-monotonic', action='store_true', help='强制单调')
    return p.parse_args()


def main():
    args = parse_args()
    if args.rtt <= 0:
        raise SystemExit('RTT 必须为正数')
    print(f"[INFO] 将使用单向 delay={args.rtt/2:.2f} ms 以近似 RTT≈{args.rtt} ms")
    results = measure(args.bandwidths, args.sizes, args.rtt, args.repeats, args.port, args.enforce_monotonic)
    print_table(args.bandwidths, args.sizes, results, args.rtt, args.repeats)
    curves = build_relative(results)
    if not args.no_plot:
        plot(curves, args.outfile)
    if args.csv:
        export_csv(args.csv, args.bandwidths, args.sizes, results)
    print("[DONE] 实验完成。")


if __name__ == '__main__':
    main()
