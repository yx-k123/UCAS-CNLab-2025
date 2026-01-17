import requests
from os.path import dirname, realpath

requests.packages.urllib3.disable_warnings()

test_dir = dirname(realpath(__file__))
timeout = 2 # 2 seconds

# http 200 OK
r = requests.get('http://10.0.0.1/index.html', timeout = timeout, proxies={})
assert(r.status_code == 200 and open(test_dir + '/index.html', 'rb').read() == r.content)

# http 404
r = requests.get('http://10.0.0.1/notfound.html', timeout = timeout, proxies={})
assert(r.status_code == 404)

# file in directory
r = requests.get('http://10.0.0.1/dir/index.html', timeout = timeout, proxies={})
assert(r.status_code == 200 and open(test_dir + '/index.html', 'rb').read() == r.content)

# http 206
headers = { 'Range': 'bytes=100-200' }
r = requests.get('http://10.0.0.1/index.html', headers=headers, timeout = timeout, proxies={})
assert(r.status_code == 206 and open(test_dir + '/index.html', 'rb').read()[100:201] == r.content)

# http 206
headers = { 'Range': 'bytes=100-' }
r = requests.get('http://10.0.0.1/index.html', headers=headers, timeout = timeout, proxies={})
assert(r.status_code == 206 and open(test_dir + '/index.html', 'rb').read()[100:] == r.content)
