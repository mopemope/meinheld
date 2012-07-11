from meinheld import server
from meinheld import patch
patch.patch_all()
import time
import requests


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

    def run(self):
        if self.running:
            return

        server.listen(("0.0.0.0", 8000))
        self.running = True
        if self.middleware:
            server.run(self.middleware(self.app))
        else:
            server.run(self.app)

    def shutdown(self):
        server.shutdown()
        self.running = False

class ClientRunner(object):


    def __init__(self, app, func):
        self.func = func
        self.app = app

    def run(self):

        def _call():
            r = self.func()
            self.receive_data = r
            self.environ = self.app.environ
            server.shutdown()

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

