# -*- coding: utf-8 -*-

from base import *
from meinheld import server
from meinheld import patch
patch.patch_all()

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

def test_socket():


    def client():
        print(requests.get("http://localhost:8000/"))
        server.shutdown()

    server.listen(("0.0.0.0", 8000))
    server.spawn(client)
    server.run(App())



