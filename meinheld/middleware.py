import greenlet

CONTINUATION_KEY = 'meinheld.continuation'

class Continuation(object):

    def __init__(self, g):
        self._greenlet = g

    def suspend(self):
        parent = self._greenlet.parent
        return parent.switch(-1)
    
    def resume(self):
        self._greenlet

class SpawnMiddleware(object):

    def __init__(self, app):
        self.app = app

    def __call__(self, environ, start_response):
        g = greenlet.greenlet(self.app)
        c = Continuation(g)
        environ[CONTINUATION_KEY] = c
        return g.switch(environ, start_response)

