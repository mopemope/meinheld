import util
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
        c.suspend(10)
        return [RESPONSE]

def test_middleware():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client, App, ContinuationMiddleware)
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
    
    env, res = util.run_client(client, SuspendApp, ContinuationMiddleware)
    assert(res.status_code == 500)
    assert(env[CONTINUATION_KEY])
