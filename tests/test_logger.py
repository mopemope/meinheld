# -*- coding: utf-8 -*-

from base import *
import requests
import os

ASSERT_RESPONSE = b"Hello world!"
RESPONSE = [b"Hello ", b"world!"]

class TestLogger(object):

    def access(self, environ):
        assert(environ != None)
        print(environ)

    def error(self, exc, val, tb):
        assert(exc != None)
        assert(val != None)
        assert(tb != None)
        

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

    from meinheld import server

    env, res = run_client(client, App)
    assert(res.content == ASSERT_RESPONSE)


def test_err_log():

    def client():
        return requests.get("http://localhost:8000/foo/bar")


    from meinheld import server

    env, res = run_client(client, ErrApp)
    assert(res.status_code == 500)

def test_custom_access_log():

    def client():
        return requests.get("http://localhost:8000/foo/bar")

    from meinheld import server
    logger = TestLogger()
    server.set_access_logger(logger)

    env, res = run_client(client, App)
    assert(res.content == ASSERT_RESPONSE)


def test_custom_err_log():

    def client():
        return requests.get("http://localhost:8000/foo/bar")


    from meinheld import server
    logger = TestLogger()
    server.set_error_logger(logger)

    env, res = run_client(client, ErrApp)
    assert(res.status_code == 500)


