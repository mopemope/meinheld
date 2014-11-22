# -*- coding: utf-8 -*-

from base import *
from collections import OrderedDict
import requests
import os

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


class ErrApp(BaseApp):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        environ["XXXX"]
        return SIMPLE_RESPONSE

class ErrAppEx(BaseApp):

    def __call__(self, environ, start_response):
        status = '500 InternalServerError'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers, ZeroDivisionError)
        self.environ = environ.copy()
        return RESPONSE

class IterErrApp(BaseApp):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)

        return [1]

class UpgradeApp(BaseApp):

    def __call__(self, environ, start_response):
        status = '101 Switching Protocols'
        response_headers = [('Upgrade', 'websocket')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)

        return []

def test_check_key():

    def client():
        return requests.get("http://localhost:8000/foo/bar")
    
    env, res = run_client(client, App)
    assert(res.content == ASSERT_RESPONSE)
    assert(env.get("REQUEST_METHOD") == "GET")
    assert(env.get("SCRIPT_NAME") == "")
    assert(env.get("PATH_INFO") == "/foo/bar")
    assert(env.get("QUERY_STRING") == None)
    assert(env.get("CONTENT_TYPE") == None)
    # assert(env.get("CONTENT_LENGTH") == "0")
    assert(env.get("SERVER_NAME") == "0.0.0.0")
    assert(env.get("SERVER_PORT") == "8000")
    assert(env.get("SERVER_PROTOCOL") == "HTTP/1.1")
    assert(env.get("HTTP_USER_AGENT") != None)

def test_simple():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, App)
    # print(res.content)
    assert(res.content == ASSERT_RESPONSE)
    assert(env.get("PATH_INFO") == "/")
    assert(env.get("QUERY_STRING") == None)

def test_encode():

    def client():
        return requests.get("http://localhost:8000/あいう")
    
    env, res = run_client(client, App)
    assert(res.content == ASSERT_RESPONSE)
    assert(env.get("PATH_INFO") == "/あいう")
    assert(env.get("QUERY_STRING") == None)


def test_query():

    def client():
        return requests.get("http://localhost:8000/ABCDEF?a=1234&bbbb=ccc")
    
    env, res = run_client(client, App)
    assert(res.content == ASSERT_RESPONSE)
    assert(env.get("PATH_INFO") == "/ABCDEF")
    assert(env.get("QUERY_STRING") == "a=1234&bbbb=ccc")

def test_chunk_response():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = run_client(client, App)
    headers = res.headers
    assert(res.content == ASSERT_RESPONSE)
    assert(headers["transfer-encoding"] == "chunked")
    assert(headers["connection"] == "close")

def test_err():

    def client():
        return requests.get("http://localhost:8000/")

    env, res = run_client(client, ErrApp)
    assert(res.status_code == 500)

def test_iter_err():

    def client():
        return requests.get("http://localhost:8000/")

    env, res = run_client(client, IterErrApp)
    assert(res.status_code == 500)

def test_headers():

    def client():
        headers = {"X-TEST":"123", "DNT":"1"}
        return requests.get("http://localhost:8000/", headers=headers)

    env, res = run_client(client, App)
    assert(res.status_code == 200)
    assert(res.content == ASSERT_RESPONSE)
    assert(env["HTTP_X_TEST"] == "123")
    assert(env["HTTP_DNT"] == "1")

def test_post():

    def client():
        payload = OrderedDict([('key1', 'value1'), ('key2', 'value2')])
        return requests.post("http://localhost:8000/", data=payload)

    env, res = run_client(client, App)
    assert(res.status_code == 200)
    assert(res.content == ASSERT_RESPONSE)
    assert(env.get("wsgi.input").read() == b"key1=value1&key2=value2")

def test_upload_file():

    def client():
        filepath = os.path.join(os.path.dirname(__file__), "wallpaper.jpg")
        files = {'wallpaper.jpg': open(filepath, 'rb')}
        return requests.post("http://localhost:8000/", files=files)

    env, res = run_client(client, App)
    assert(res.status_code == 200)
    assert(res.content == ASSERT_RESPONSE)
    length = env["CONTENT_LENGTH"]
    data = env.get("wsgi.input").read()
    assert(len(data) == int(length))

def test_error():
    def client():
        return requests.get("http://localhost:8000/foo/bar")

    env, res = run_client(client, ErrAppEx)
    assert(res.status_code == 500)
    assert(res.content == ASSERT_RESPONSE)
    assert(env.get("REQUEST_METHOD") == "GET")

def test_upgrade():
    """This server will assume the application will correctly set the
    "Upgrade" header, and automatically set the "Connection" header to
    "upgrade", instead of "keep-alive" or "close", for a response with
    status "101 Switching Protocols". That is likely to better conform to
    RFC 7230 (HTTP/1.1) and RFC 6455 (WebSocket).
    """

    def client():
        return requests.get("http://localhost:8000")

    env, res = run_client(client, UpgradeApp)
    headers = res.headers
    assert(res.status_code == 101)
    assert(headers["upgrade"] == "websocket")
    assert(headers["connection"] == "upgrade")
