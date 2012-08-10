# -*- coding: utf-8 -*-

from base import *
import requests
import socket

ASSERT_RESPONSE = b"Hello world!"
RESPONSE = [b"Hello ", b"world!"]

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 8000
DEFAULT_METHOD = "GET"
DEFAULT_PATH = "/PATH?ket=value"
DEFAULT_VERSION = "HTTP/1.0"

DEFAULT_ADDR = (DEFAULT_HOST, DEFAULT_PORT)

DEFAULT_HEADER = [
            ("User-Agent", "Mozilla/5.0 (X11; U; Linux i686; ja; rv:1.9.2.7) Gecko/20100715 Ubuntu/10.04 (lucid) Firefox/3.6.7"),
            ("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"),
            ("Accept-Language", "ja,en-us;q=0.7,en;q=0.3"),
            ("Accept-Encoding", "gzip,deflate"),
            ("Accept-Charset", "Shift_JIS,utf-8;q=0.7,*;q=0.7"),
            ("Keep-Alive","115"),
            ("Connection", "keep-alive"),
            ("Cache-Control", "max-age=0"),
        ]

def send_data(addr=DEFAULT_ADDR, method=DEFAULT_METHOD, path=DEFAULT_PATH,
        version=DEFAULT_VERSION, headers=DEFAULT_HEADER, post_data=None):

    sock = socket.create_connection(addr)
    sock.send("%s %s %s\r\n" % (method, path, version))
    sock.send("Host: %s\r\n" % addr[0])
    for h in  headers:
        sock.send("%s: %s\r\n" % h)
    sock.send("\r\n")
    if post_data:
        sock.send(post_data)
        sock.send("\r\n")

    data = sock.recv(1024 * 2)
    return data

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

def test_bad_method1():

    def client():
        send_data(method="")

    env, res = run_client(client, App)
    assert(res == None)
