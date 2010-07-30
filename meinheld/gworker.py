from gunicorn.workers.base import Worker
from meinheld import server

class MeinheldWorker(Worker):

    def run(self):
        fd = self.socket.fileno()
        server.set_listen_socket(fd)
        server.run(self.wsgi)

