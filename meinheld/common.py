from meinheld import server

CLIENT_KEY = 'meinheld.client'
CONTINUATION_KEY = 'meinheld.continuation'

class Continuation(object):

    def __init__(self, client):
        self.client = client

    def suspend(self, timeout=0):
        return server._suspend_client(self.client, timeout)
    
    def resume(self, *args, **kwargs):
        return server._resume_client(self.client, args, kwargs)
