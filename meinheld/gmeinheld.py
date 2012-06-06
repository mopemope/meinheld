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
        server.set_keepalive(self.cfg.keepalive)
        server.set_picoev_max_fd(self.cfg.worker_connections)

        server.set_fastwatchdog(self.tmp.fileno(), self.ppid)
        #server.set_watchdog(self.watchdog)

        server.set_listen_socket(fd)
        server.run(self.wsgi)

    def handle_quit(self, sig, frame):
        print(self.timeout)
        server.stop(int(self.timeout))

    def handle_exit(self, sig, frame):
        server.stop()
        sys.exit(0)

