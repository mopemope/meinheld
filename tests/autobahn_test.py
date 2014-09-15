import logging
import subprocess
import sys

if __name__ == '__main__':
    server27 = subprocess.Popen(('python2', 'autobahn_test_servers.py',
                               '8002'), cwd='tests')
    server34 = subprocess.Popen(('python3', 'autobahn_test_servers.py',
                               '8003'), cwd='tests')
    client = subprocess.Popen(('wstest', '-m', 'fuzzingclient', '-s',
                               'fuzzingclient.json'), cwd='tests')

    status = client.wait()
    server27.kill()
    server34.kill()

    server27.wait()
    server34.wait()
    sys.exit(status)

