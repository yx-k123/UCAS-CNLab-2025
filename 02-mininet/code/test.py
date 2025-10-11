#!/usr/bin/python

from mininet.net import Mininet
from mininet.cli import CLI
from time import sleep

net = Mininet()
h1 = net.addHost('h1')
h2 = net.addHost('h2')
net.addLink(h1, h2)
net.start()
h2.cmd('python3 -m http.server 80 &')
sleep(2)
h1.cmd('wget %s -O result.txt' % (h2.IP()))
net.stop()
