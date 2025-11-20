#!/usr/bin/python

import os
import sys
import glob

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

class MultiRouterTopo(Topo):
    def build(self):
        # Add hosts
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')

        # Add routers
        r1 = self.addHost('r1')
        r2 = self.addHost('r2')
        r3 = self.addHost('r3')

        # Add links
        self.addLink(h1, r1)
        self.addLink(r1, r2)
        self.addLink(r2, r3)
        self.addLink(r3, h2)

if __name__ == '__main__':
    check_scripts()

    topo = MultiRouterTopo()
    net = Mininet(topo=topo, controller=None)

    # Get nodes
    h1, h2, r1, r2, r3 = net.get('h1', 'h2', 'r1', 'r2', 'r3')

    # Configure hosts
    h1.cmd('ifconfig h1-eth0 10.0.1.11/24')
    h2.cmd('ifconfig h2-eth0 10.0.4.22/24')
    h1.cmd('route add default gw 10.0.1.1')
    h2.cmd('route add default gw 10.0.4.1')

    # Configure routers
    r1.cmd('ifconfig r1-eth0 10.0.1.1/24')
    r1.cmd('ifconfig r1-eth1 10.0.2.1/24')
    r2.cmd('ifconfig r2-eth0 10.0.2.2/24')
    r2.cmd('ifconfig r2-eth1 10.0.3.1/24')
    r3.cmd('ifconfig r3-eth0 10.0.3.2/24')
    r3.cmd('ifconfig r3-eth1 10.0.4.1/24')

    # Configure static routes on routers
    r1.cmd('route add -net 10.0.3.0/24 gw 10.0.2.2')
    r1.cmd('route add -net 10.0.4.0/24 gw 10.0.2.2')

    r2.cmd('route add -net 10.0.1.0/24 gw 10.0.2.1')
    r2.cmd('route add -net 10.0.4.0/24 gw 10.0.3.2')

    r3.cmd('route add -net 10.0.1.0/24 gw 10.0.3.1')
    r3.cmd('route add -net 10.0.2.0/24 gw 10.0.3.1')

    # Disable offloading and IPv6
    for n in (h1, h2, r1, r2, r3):
        n.cmd('./scripts/disable_offloading.sh')
        n.cmd('./scripts/disable_ipv6.sh')

    # Start network
    net.start()

    # Connectivity test
    r1.cmd("./router &")
    r2.cmd("./router &")
    r3.cmd("./router &")

    CLI(net)
    net.stop()
