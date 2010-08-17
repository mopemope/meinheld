from meinheld import server
import greenlet

CLIENT_KEY = 'meinheld.client'
CONTINUATION_KEY = 'meinheld.continuation'
IO_KEY = 'wsgix.io'

class Continuation(object):

    def __init__(self, client):
        self.client = client

    def suspend(self):
        return server._suspend_client(self.client)
    
    def resume(self, *args, **kwargs):
        return server._resume_client(self.client, args, kwargs)
        

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
        s = server._get_socket_fromfd(client.get_fd())
        environ[IO_KEY] = s

        return g.switch(environ, start_response)


