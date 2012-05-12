# -*- coding: utf-8 -*-

import util
import requests

RESPONSE = b"Hello world!"

class App(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        return [RESPONSE]


class ErrApp(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        environ["XXXX"]
        return [RESPONSE]

class IterErrApp(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)

        return [1]

def test_simple():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client, App)
    # print(res.content)
    assert(RESPONSE == res.content)
    assert("/" == env["PATH_INFO"])
    assert(None == env.get("QUERY_STRING"))

def test_encode():

    def client():
        return requests.get("http://localhost:8000/あいう")
    
    env, res = util.run_client(client, App)
    assert(RESPONSE == res.content)
    assert("/あいう" == env["PATH_INFO"])
    assert(None == env.get("QUERY_STRING"))


def test_query():

    def client():
        return requests.get("http://localhost:8000/?a=1234&bbbb=ccc")
    
    env, res = util.run_client(client, App)
    assert(RESPONSE == res.content)
    assert("/" == env["PATH_INFO"])
    assert("a=1234&bbbb=ccc" == env["QUERY_STRING"])

def test_chunk_response():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client, App)
    headers = res.headers
    assert(RESPONSE == res.content)
    assert(headers["transfer-encoding"] == "chunked")
    assert(headers["connection"] == "close")

def test_err():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client, ErrApp)
    assert(500 == res.status_code)

def test_iter_err():

    def client():
        return requests.get("http://localhost:8000/")
    
    env, res = util.run_client(client, IterErrApp)
    assert(500 == res.status_code)

