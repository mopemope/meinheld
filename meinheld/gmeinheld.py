import os
import sys

from gunicorn.workers.base import Worker
from gunicorn.glogging import Logger

from meinheld import server
from meinheld.mlogging import _access, _error


class MeinheldWorker(Worker):

    def __init__(self, *args, **kwargs):
        Worker.__init__(self, *args, **kwargs)
        Logger.access = _access
        Logger.error = _error

        if self.cfg.accesslog is self.cfg.logconfig is None:
            server.set_access_logger(None)
        else:
            server.set_access_logger(self.log)

        server.set_error_logger(self.log)

    def watchdog(self):
        self.notify()

        if self.ppid != os.getppid():
            self.log.info("Parent changed, shutting down: %s" % self)
            server.stop(int(self.timeout))

    def run(self):

        if hasattr(self, "sockets"):
            fds = [s.fileno() for s in self.sockets]
        else:
            fds = [self.socket.fileno()]

        server.set_keepalive(self.cfg.keepalive)
        server.set_picoev_max_fd(self.cfg.worker_connections)

        server.set_fastwatchdog(self.tmp.fileno(), self.ppid, int(self.timeout))
        #server.set_watchdog(self.watchdog)

        server.set_listen_socket(fds)
        server.run(self.wsgi)

    def handle_quit(self, sig, frame):
        server.stop(int(self.timeout))

    def handle_exit(self, sig, frame):
        server.stop()
        sys.exit(0)
