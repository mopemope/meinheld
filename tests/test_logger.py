# -*- coding: utf-8 -*-

from base import *
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
        # print(environ)
        return RESPONSE

class ErrApp(BaseApp):

    def __call__(self, environ, start_response):
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        # print(environ)
        environ["XXXX"]
        return RESPONSE

def test_access_log():

    def client():
        return requests.get("http://localhost:8000/foo/bar")

    def access(environ):
        assert(environ != None)
        print(environ)

    from meinheld import server
    #server.set_access_logger(access)

    env, res = run_client(client, App)
    assert(res.content == ASSERT_RESPONSE)


def test_err_log():

    def client():
        return requests.get("http://localhost:8000/foo/bar")

    def err(exc, val, tb):
        from traceback import print_tb
        assert(exc != None)
        assert(val != None)
        assert(tb != None)
        print_tb(tb)

    from meinheld import server
    server.set_error_logger(err)

    env, res = run_client(client, ErrApp)
    assert(res.status_code == 500)

