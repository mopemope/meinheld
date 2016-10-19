import meinheld
from multiprocessing import Process
import signal

workers = []

def hello_world(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain'),('Content-Length',str(len(res)))]
    start_response(status, response_headers)
#    print environ
    return [res]

def run(app, i):
    meinheld.run(app)

def kill_all(sig, st):
    for w in workers:
        w.terminate()

def start(num=4):
    for i in range(num):
        p = Process(name="worker-%d" % i, target=run, args=(hello_world,i))
        workers.append(p)
        p.start()

signal.signal(signal.SIGTERM, kill_all)
meinheld.set_keepalive(10)
meinheld.set_access_logger(None)
meinheld.set_error_logger(None)
meinheld.listen(("0.0.0.0", 8000))
start()





