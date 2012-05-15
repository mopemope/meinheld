import time
from util import *
import requests
from meinheld.middleware import ContinuationMiddleware, CONTINUATION_KEY

RESPONSE = b"Hello world!"

class App(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        return [RESPONSE]

class SuspendApp(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        c = environ[CONTINUATION_KEY]
        c.suspend(1)
        return [RESPONSE]

class ResumeApp(object):
    witer = True
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
            c.suspend(15)
            return [b"RESUMED"]
        else:
            self.waiter.resume()

        return [RESPONSE]

class DoubleSuspendApp(object):
    witer = True
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

class IllicalResumeApp(object):
    witer = True
    suspend = True
    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        c = environ[CONTINUATION_KEY]
        c.resume()

        return [RESPONSE]

def test_middleware():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, App, ContinuationMiddleware)
    assert(RESPONSE == res.content)
    assert("/" == env["PATH_INFO"])
    assert(None == env.get("QUERY_STRING"))
    assert(env[CONTINUATION_KEY])

def test_suspend():
    """
    Timeout error test
    """
    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, SuspendApp, ContinuationMiddleware)
    assert(res.status_code == 500)
    assert(env[CONTINUATION_KEY])

def test_resume():
    """
    Resume Test
    """
    def client1():
        return requests.get("http://localhost:8000/1")
    def client2():
        return requests.get("http://localhost:8000/2")
    
    application = ResumeApp()
    s = ServerRunner(application, ContinuationMiddleware)
    s.start()
    r1 = ClientRunner(application, client1)
    r1.start()
    time.sleep(1)
    r2 = ClientRunner(application, client2)
    r2.start()

    env1, res1 = r1.get_result()
    env2, res2 = r2.get_result()
    s.shutdown()
    assert(res1.status_code == 200)
    assert(res2.status_code == 200)
    assert(res1.content == b"RESUMED")
    assert(res2.content == RESPONSE)
    assert(env1[CONTINUATION_KEY])

def test_double_suspend():
    """
    Double Suspend Test
    """
    def client():
        return requests.get("http://localhost:8000/")
    
    application = DoubleSuspendApp()
    s = ServerRunner(application, ContinuationMiddleware)
    s.start()
    r1 = ClientRunner(application, client)
    r1.start()
    r2 = ClientRunner(application, client)
    r2.start()

    env1, res1 = r1.get_result()
    env2, res2 = r2.get_result()
    s.shutdown()
    assert(res1.status_code == 500)
    assert(res2.status_code == 500)

def test_illigal_resume():
    """
    """
    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, IllicalResumeApp, ContinuationMiddleware)
    assert(res.status_code == 500)
    assert(env[CONTINUATION_KEY])
