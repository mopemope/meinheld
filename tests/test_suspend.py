import time
from base import *
import requests
from meinheld.middleware import ContinuationMiddleware, CONTINUATION_KEY

RESPONSE = b"Hello world!"

class App(BaseApp):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        return [RESPONSE]

class SuspendApp(BaseApp):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        c = environ[CONTINUATION_KEY]
        c.suspend(1)
        return [RESPONSE]

class ResumeApp(BaseApp):
    waiter = True
    suspend = True
    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        if self.suspend:
            c = environ[CONTINUATION_KEY]
            self.waiter = c
            self.suspend = False
            c.suspend(3)
            return [b"RESUMED"]
        else:
            self.waiter.resume()

        return [RESPONSE]

class DoubleSuspendApp(BaseApp):
    waiter = True
    suspend = True
    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        if self.suspend:
            c = environ[CONTINUATION_KEY]
            self.waiter = c
            self.suspend = False
            c.suspend(2)
            return [b"RESUMED"]
        else:
            #suspend 
            self.waiter.suspend()

        return [RESPONSE]

class IlligalResumeApp(BaseApp):
    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        c = environ[CONTINUATION_KEY]
        c.resume()

        return [RESPONSE]

class ManyResumeApp(BaseApp):
    waiters = []
    environ = dict() 
    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        path = environ.get("PATH_INFO")
        if path == "/wakeup":
            for waiter in self.waiters:
                waiter.resume()
        else:
            c = environ[CONTINUATION_KEY]
            self.waiters.append(c)
            c.suspend(10)

        return [path.encode()]

def test_middleware():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, App, ContinuationMiddleware)
    assert(RESPONSE == res.content)
    assert(env.get("PATH_INFO") == "/")
    assert(env.get("QUERY_STRING") == None)
    assert(env.get(CONTINUATION_KEY))

def test_suspend():
    """
    Timeout error test
    """
    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, SuspendApp, ContinuationMiddleware)
    assert(res.status_code == 500)
    assert(env.get(CONTINUATION_KEY))

def test_resume():

    def client1():
        return requests.get("http://localhost:8000/1")
    def client2():
        return requests.get("http://localhost:8000/2")
    
    application = ResumeApp()
    s = ServerRunner(application, ContinuationMiddleware)
    r1 = ClientRunner(application, client1, False)
    r2 = ClientRunner(application, client2, False)
    r1.run()
    r2.run()
    s.run(shutdown=True)

    env1, res1 = r1.get_result()
    env2, res2 = r2.get_result()
    assert(res1.status_code == 200)
    assert(res2.status_code == 200)
    assert(res1.content == b"RESUMED")
    assert(res2.content == RESPONSE)
    assert(env1.get(CONTINUATION_KEY))
    assert(env2.get(CONTINUATION_KEY))

def test_double_suspend():
    def client():
        return requests.get("http://localhost:8000/")
    
    application = DoubleSuspendApp()
    s = ServerRunner(application, ContinuationMiddleware)
    r1 = ClientRunner(application, client, False)
    r2 = ClientRunner(application, client, False)
    r1.run()
    r2.run()
    s.run(shutdown=True)

    env1, res1 = r1.get_result()
    env2, res2 = r2.get_result()
    assert(res1.status_code == 500)
    assert(res2.status_code == 500)
    assert(env1.get(CONTINUATION_KEY))
    assert(env2.get(CONTINUATION_KEY))

def test_illigal_resume():
    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, IlligalResumeApp, ContinuationMiddleware)
    assert(res.status_code == 500)
    assert(env.get(CONTINUATION_KEY))

"""
def test_many_resume():
    
    def mk_client(i):
        def client():
            return requests.get("http://localhost:8000/%s" % i)
        return client

    application = ManyResumeApp()
    s = ServerRunner(application, ContinuationMiddleware)
    runners = []
    for i in range(10):
        r = ClientRunner(application, mk_client(i), False)
        r.run()
        runners.append(r)

    def _wakeup():
        r = ClientRunner(application, mk_client("wakeup"))
        r.run()
        runners.append(r)
    
    server.schedule_call(3, _wakeup)
    s.run()
    results = []
    for r in runners:
        env, res = r.get_result()
        results.append(res.content)
    results = sorted(results)
    print(results)
    assert(results == [b'/0', b'/1', b'/2', b'/3', b'/4', b'/5', b'/6', b'/7', b'/8', b'/9', b'/wakeup'])

test_many_resume()
"""
