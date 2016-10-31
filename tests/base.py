from meinheld import server
import requests  # requests should not be pached
from meinheld import patch
patch.patch_all()
import time


running = False

def start_server(app, middleware=None):
    global running 
    if running:
        return

    server.listen(("0.0.0.0", 8000))
    running = True
    if middleware:
        server.run(middleware(app))
    else:
        server.run(app)

    return app.environ


class ServerRunner(object):


    def __init__(self, app, middleware=None):
        self.app = app
        self.middleware = middleware
        self.running = False

    def run(self, shutdown=False):
        if self.running:
            return

        server.listen(("0.0.0.0", 8000))
        self.running = True
        if shutdown:
            server.schedule_call(1, server.shutdown, 3)
        if self.middleware:
            server.run(self.middleware(self.app))
        else:
            server.run(self.app)

class ClientRunner(object):


    def __init__(self, app, func, shutdown=True):
        self.func = func
        self.app = app
        self.shutdown = shutdown

    def run(self):

        def _call():
            try:
                r = self.func()
                self.receive_data = r
                self.environ = self.app.environ
            finally:
                if self.shutdown:
                    server.shutdown(1)

        server.spawn(_call)

    def get_result(self):
        return (self.environ, self.receive_data)


def run_client(client=None, app=None, middleware=None):
    application = app()
    s = ServerRunner(application, middleware)
    r = ClientRunner(application, client)
    r.run()
    s.run()
    return r.environ, r.receive_data

class BaseApp(object):

    environ = None

