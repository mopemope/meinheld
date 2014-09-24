from os.path import join, abspath
from subprocess import Popen, call
import sys
import meinheld


def ensure_virtualenv():
    if not hasattr(sys, 'real_prefix'):
        raise Exception('Not inside a virtualenv')

def ensure_python2():
    src = '''
import sys
try:
    sys.real_prefix
    sys.exit(0)
except AttributeError:
    sys.exit(1)
'''

    if call(('python2', '-c', src)):
        call(('virtualenv', '-p', 'python2', sys.prefix))

    zipfile = 'meinheld-{}.zip'.format(meinheld.__version__)
    zipfile = abspath(join(sys.prefix, '..', 'dist', zipfile))
    call(('pip2', 'install', '--pre', '-U', zipfile))

def ensure_wstest():
    try:
        call(('wstest', '-a'))
    except FileNotFoundError:
        call(('pip2', 'install', 'autobahntestsuite'))

def setup_servers():
    server27 = Popen(('python2', 'autobahn_test_servers.py', '8002'))
    server34 = Popen(('python3', 'autobahn_test_servers.py', '8003'))
    return server27, server34

def teardown_servers(servers):
    for server in servers:
        try:
            server.kill()
            server.wait()
        except AttributeError:
            pass

def runtests():
    """Runs the Autobahn Test Suite against Meinheld on both Python 2.7 and
    Python 3.4. The test suite itself should run on Python 2.7, and should
    collect results from all servers from a single run (to report correctly).
    This is why both servers are tested from a single environemnt.
    """

    ensure_virtualenv()
    ensure_python2()
    ensure_wstest()

    status, server27, server34 = 1, False, False
    try:
        servers = setup_servers()
        status = call(('wstest', '-m', 'fuzzingclient',
                '-s', 'fuzzingclient.json'))
    finally:
        teardown_servers(servers)
    sys.exit(status)

if __name__ == '__main__':
    runtests()

