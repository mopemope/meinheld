# -*- coding: utf-8 -*-

from util import *
import requests

ASSERT_RESPONSE = b"Hello world!"
RESPONSE = [b"Hello ", b"world!"]

class App(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        return RESPONSE


class ErrApp(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        environ["XXXX"]
        return SIMPLE_RESPONSE

class IterErrApp(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)

        return [1]

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
    assert(env.get("CONTENT_LENGTH") == None)
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

