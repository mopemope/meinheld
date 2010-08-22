from meinheld import server
from meinheld.common import Continuation, CLIENT_KEY, CONTINUATION_KEY
from meinheld.websocket import WebSocketMiddleware
import greenlet


class SpawnMiddleware(object):

    def __init__(self, app):
        self.app = app

    def __call__(self, environ, start_response):
        client = environ[CLIENT_KEY]
        g = client.get_greenlet()
        if not g:
            # new greenlet
            g = greenlet.greenlet(self.app)
            client.set_greenlet(g) 
            c = Continuation(client)
            environ[CONTINUATION_KEY] = c

        return g.switch(environ, start_response)

