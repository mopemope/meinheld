import socket
import urllib
import threading
import time


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

def app_factory():

    class Handler(object):

        def __call__(self, environ, start_response):
            status = '200 OK'
            res = "Hello world!"
            response_headers = [('Content-type','text/plain')]
            start_response(status, response_headers)
            self.environ = environ.copy()
            print(environ)
            return [res]

    return Handler()

def send_data(addr=DEFAULT_ADDR, method=DEFAULT_METHOD, path=DEFAULT_PATH,
        version=DEFAULT_VERSION, headers=DEFAULT_HEADER, post_data=None):

    sock = socket.create_connection(addr)
    sock.send("%s %s %s\r\n" % (method, urllib.quote(path), version))
    sock.send("Host: %s\r\n" % addr[0])
    for h in  headers:
        sock.send("%s: %s\r\n" % h)
    sock.send("\r\n")
    if post_data:
        sock.send(post_data)
        sock.send("\r\n")

    data = sock.recv(1024 * 2)
    return data

def start_server(app):

    from meinheld import server

    server.listen(("0.0.0.0", 8000))
    server.run(app)
    return app.environ


class ClientRunner(threading.Thread):


    def __init__(self, *args, **kwargs):
        threading.Thread.__init__(self)
        self.args = args
        self.kwargs = kwargs

    def run(self):
        from meinheld import server
        time.sleep(1)
        args = self.args
        kwargs = self.kwargs
        self.receive_data = send_data(*args, **kwargs)
        server.shutdown()

def run_client(app_factory=app_factory, *args, **kwargs):
    r = ClientRunner(*args, **kwargs)
    r.start()
    env = start_server(app_factory())
    r.join()
    return env, r.receive_data


