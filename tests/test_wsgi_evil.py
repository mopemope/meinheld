# -*- coding: utf-8 -*-

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



def test_long_url1():

    def client():
        query = "A" * 8191
        return requests.get("http://localhost:8000/" + query)
    
    env, res = run_client(client, App)
    assert(res.status_code == 200)

def test_long_url2():

    def client():
        query = "A" * 8192
        return requests.get("http://localhost:8000/" + query)
    
    env, res = run_client(client, App)
    assert(res.status_code == 400)

