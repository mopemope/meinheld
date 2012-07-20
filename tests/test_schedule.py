from base import *
import requests

ASSERT_RESPONSE = b"Hello world!"
RESPONSE = [b"Hello ", b"world!"]

class App(BaseApp):

    environ = None

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        return RESPONSE

def test_simple():

    def _call():
        assert(True)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(0, _call)
    server.run(App())

def test_args():
    def _call(arg):
        assert(arg == 1)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(0, _call, 1)
    server.run(App())


def test_args2():
    def _call(a, b):
        assert(a == 1)
        assert(b == "ABC")
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(0, _call, 1, "ABC")
    server.run(App())

def test_kwargs():
    def _call(a=0, b="test"):
        assert(a == 1)
        assert(b == "ABC")
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(0, _call, b="ABC", a=1)
    server.run(App())

def test_kwargs2():
    def _call(a, b="test", c=False):
        assert(a == 1)
        assert(b == "ABC")
        assert(c == True)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(0, _call, 1, c=True, b="ABC")
    server.run(App())

def test_kwargs3():
    def _call(a, b="test", c=False):
        assert(a == 1)
        assert(b == "test")
        assert(c == False)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(0, _call, 1)
    server.run(App())

def test_time():
    def _call(a, b):
        assert(a == 1)
        assert(b == "ABC")
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(5, _call, 1, "ABC")
    server.run(App())

def test_nested():
    def _schedule_call():
        server.shutdown()
    
    def _call():
        server.schedule_call(0, _schedule_call)

    server.listen(("0.0.0.0", 8000))
    server.schedule_call(0, _call)
    server.run(App())



