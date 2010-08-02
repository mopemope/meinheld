from gunicorn.workers.base import Worker
from meinheld import server
import os

class MeinheldWorker(Worker):
    
    def watchdog(self):
        self.notify()

        if self.ppid != os.getppid():
            self.log.info("Parent changed, shutting down: %s" % self)
            server.stop()

    def run(self):
        fd = self.socket.fileno()
        server.set_watchdog(self.watchdog)
        server.set_listen_socket(fd)
        server.run(self.wsgi)

    def handle_quit(self, sig, frame):
        server.stop()

    def handle_exit(self, sig, frame):
        server.stop()
        sys.exit(0)

