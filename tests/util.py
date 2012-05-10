import threading
import time
import requests

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

class Handler(object):

    def __call__(self, environ, start_response):
        status = '200 OK'
        res = "Hello world!"
        response_headers = [('Content-type','text/plain')]
        start_response(status, response_headers)
        self.environ = environ.copy()
        print(environ)
        return [res]

def app_factory(app=Handler):

    return app()


def start_server(app):

    from meinheld import server

    server.listen(("0.0.0.0", 8000))
    server.run(app)
    return app.environ


class ClientRunner(threading.Thread):


    def __init__(self, func):
        threading.Thread.__init__(self)
        self.func = func 

    def run(self):
        from meinheld import server
        time.sleep(1)
        r = self.func()
        self.receive_data = r
        server.shutdown()

def run_client(client=None, app_factory=app_factory):
    r = ClientRunner(client)
    r.start()
    env = start_server(app_factory())
    r.join()
    return env, r.receive_data


