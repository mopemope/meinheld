from meinheld import server
import greenlet

CLIENT_KEY = 'meinheld.client'
CONTINUATION_KEY = 'meinheld.continuation'

class Continuation(object):

    def __init__(self, client, g):
        self._greenlet = g
        self.client = client

    def suspend(self):
        parent = self._greenlet.parent
        return parent.switch(-1)
    
    def resume(self):
        server._resume_client(self.client)
        

class SpawnMiddleware(object):

    def __init__(self, app):
        self.app = app

    def __call__(self, environ, start_response):
        g = greenlet.greenlet(self.app)
        
        client = environ[CLIENT_KEY]
        client.set_greenlet(g)

        c = Continuation(client, g)
        environ[CONTINUATION_KEY] = c
        return g.switch(environ, start_response)

