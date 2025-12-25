#!/usr/bin/python

import os
import sys
import glob

from mininet.node import OVSBridge
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.cli import CLI

script_deps = [ 'ethtool', 'arptables', 'iptables' ]

def check_scripts():
    dir = os.path.abspath(os.path.dirname(sys.argv[0]))
    
    for fname in glob.glob(dir + '/' + 'scripts/*.sh'):
        if not os.access(fname, os.X_OK):
            print('%s should be set executable by using `chmod +x $script_name`' % (fname))
            sys.exit(1)

    for program in script_deps:
        found = False
        for path in os.environ['PATH'].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
                found = True
                break
        if not found:
            print('`%s` is required but missing, which could be installed via `apt` or `aptitude`' % (program))
            sys.exit(2)

class myTopo(Topo):
    def build(self):
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        n1 = self.addHost('n1')
        n2 = self.addHost('n2')

        # h1-eth0 <-> n1-eth0
        self.addLink(h1, n1)
        # n1-eth1 <-> n2-eth0
        self.addLink(n1, n2)
        # n2-eth1 <-> h2-eth0
        self.addLink(n2, h2)

if __name__ == '__main__':
    check_scripts()

    topo = myTopo()
    net = Mininet(topo = topo, switch = OVSBridge, controller = None)
    h1, h2, n1, n2 = net.get('h1', 'h2', 'n1', 'n2')

    # ====== IP & 路由配置 ======

    # 内网 A：h1 <-> n1
    h1.cmd('ifconfig h1-eth0 10.21.0.1/16')
    h1.cmd('route add default gw 10.21.0.254')
    n1.cmd('ifconfig n1-eth0 10.21.0.254/16')

    # 中间网：n1 <-> n2
    n1.cmd('ifconfig n1-eth1 159.226.39.43/24')
    n2.cmd('ifconfig n2-eth0 159.226.39.44/24')

    # 内网 B：n2 <-> h2
    h2.cmd('ifconfig h2-eth0 10.22.0.1/16')
    h2.cmd('route add default gw 10.22.0.254')
    n2.cmd('ifconfig n2-eth1 10.22.0.254/16')

    # ====== 关闭内核协议栈功能，交给用户态 NAT ======
    for n in (n1, n2):
        n.cmd('./scripts/disable_arp.sh')
        n.cmd('./scripts/disable_icmp.sh')
        n.cmd('./scripts/disable_ip_forward.sh')
        n.cmd('./scripts/disable_ipv6.sh')

    for node in (h1, h2, n1, n2):
        node.cmd('./scripts/disable_offloading.sh')
        node.cmd('./scripts/disable_ipv6.sh')

    net.start()

    # ====== 启动 NAT / HTTP / 客户端请求 ======
    # n1: SNAT
    n1.cmd('./nat n1.conf > ./log/n1_snat_3.log 2>&1 &')
    # n2: DNAT
    n2.cmd('./nat n2.conf > ./log/n2_dnat_3.log 2>&1 &')
    # h2: HTTP server
    h2.cmd('python3 ./http_server.py > ./log/h2_http_3.log 2>&1 &')
    # h1: 访问 n2 的“公网”地址
    h1.cmd('wget http://159.226.39.44:8000 > ./log/h1_wget_3.log 2>&1 &')

    CLI(net)
    net.stop()