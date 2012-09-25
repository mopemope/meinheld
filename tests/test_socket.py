# -*- coding: utf-8 -*-

import sys
from pytest import *
from base import *
from meinheld import server
from meinheld import msocket
import socket
import traceback

ASSERT_RESPONSE = b"Hello world!"
RESPONSE = [b"Hello ", b"world!"]

class App(BaseApp):

    environ = None

    def __call__(self, environ, start_response):
        from meinheld import patch
        patch.patch_all()
        status = '200 OK'
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        return RESPONSE

def test_sock():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        assert(type(s) == msocket.socket)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_connect():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        assert(s)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_connect_fail():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        with raises(msocket.gaierror):
            s.connect(("google.comaaa", 80))
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_connect_ex():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect_ex(("localhost", 8000))
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())


@mark.skipif('sys.hexversion < 0x3000000')
def test_detach():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.detach()
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())


def test_fileno():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        assert(s.fileno() > 2)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())


def test_getpeername():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        assert(s)
        host, port = s.getpeername()
        assert(host == "127.0.0.1")
        assert(port == 8000)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_sockname():

    def _test():
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        shost, sport = s.getsockname()
        ms = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        host, port = ms.getsockname()
        assert(host == shost)
        assert(port == sport)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())



def test_getsockopt():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        tr = s.getsockopt(msocket.IPPROTO_TCP, msocket.TCP_NODELAY)
        assert(tr > 0)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_timeout():
    def _test():
        val = 5
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.settimeout(val)
        assert(val == s.gettimeout())
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())


def test_send():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        s.send(b"GET / HTTP/1.0\r\n")
        s.send(b"\r\n")
        assert(len(s.recv(1024)) == 138)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_sendall():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        s.sendall(b"GET / HTTP/1.0\r\n\r\n")
        assert(len(s.recv(1024)) == 138)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_sendto():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        s.sendto(b"GET / HTTP/1.0\r\n\r\n", (b'localhost',8000))
        assert(len(s.recv(1024)) == 138)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_recv():

    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        s.send(b"GET / HTTP/1.0\r\n")
        s.send(b"\r\n")
        c = s.recv(1024)
        assert(len(c) == 138)

        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_makefile():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        f = s.makefile()
        assert(f)

        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_makefile_write_read():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        f = s.makefile(mode="rwb")
        assert(f)
        f.write(b"GET / HTTP/1.0\r\n")
        f.write(b"\r\n")
        f.flush()
        c = f.read(1024)
        assert(len(c) == 138)
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())


def test_close():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        s.close()
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())

def test_close_duble():
    def _test():
        s = msocket.socket(msocket.AF_INET, msocket.SOCK_STREAM)
        s.connect(("localhost", 8000))
        s.close()
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(_test)
    server.run(App())


