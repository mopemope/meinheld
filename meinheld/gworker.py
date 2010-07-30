from gunicorn.workers.base import Worker
from meinheld import server

class MeinheldWorker(Worker):
    
    """
    def watchdog(self):
        self.log.info("notify start")
        self.notify()
        self.log.info("notify end")
        if self.ppid != os.getppid():
            self.log.info("Parent changed, shutting down: %s" % self)
            server.stop()
    """

    def run(self):
        fd = self.socket.fileno()
        #server.set_watchdog(self.watchdog, (self,))
        server.set_listen_socket(fd)
        server.run(self.wsgi)

