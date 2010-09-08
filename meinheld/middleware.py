from meinheld import server
from meinheld.common import Continuation, CLIENT_KEY, CONTINUATION_KEY
from meinheld.websocket import WebSocketMiddleware


class ContinuationMiddleware(object):

    def __init__(self, app):
        self.app = app

    def __call__(self, environ, start_response):
        client = environ[CLIENT_KEY]
        c = Continuation(client)
        environ[CONTINUATION_KEY] = c

        return self.app(environ, start_response)

