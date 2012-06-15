import threading
import time
import requests


running = False

def start_server(app, middleware=None):
    global running 
    if running:
        return
    from meinheld import server

    server.listen(("0.0.0.0", 8000))
    running = True
    if middleware:
        server.run(middleware(app))
    else:
        server.run(app)

    return app.environ


class ServerRunner(threading.Thread):


    def __init__(self, app, middleware=None):
        threading.Thread.__init__(self)
        self.app = app
        self.middleware = middleware
        self.running = False

    def run(self):
        if self.running:
            return
        from meinheld import server

        server.listen(("0.0.0.0", 8000))
        self.running = True
        if self.middleware:
            server.run(self.middleware(self.app))
        else:
            server.run(self.app)

    def shutdown(self):
        from meinheld import server
        server.shutdown()
        self.running = False
        # self.environ = self.app.environ
        self.join()

class ClientRunner(threading.Thread):


    def __init__(self, app, func):
        threading.Thread.__init__(self)
        self.func = func
        self.app = app

    def run(self):
        time.sleep(0.2)
        r = self.func()
        self.receive_data = r
        self.environ = self.app.environ
        

    def get_result(self):
        self.join()
        return (self.environ, self.receive_data)


def run_client(client=None, app=None, middleware=None):
    application = app()
    s = ServerRunner(application, middleware)
    s.start()
    r = ClientRunner(application, client)
    r.start()
    r.join()

    s.shutdown()
    return r.environ, r.receive_data

class BaseApp(object):

    environ = None

