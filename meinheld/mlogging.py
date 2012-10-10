import datetime
import logging
logging.Logger.manager.emittedNoHandlerWarning = 1
import os
import sys
import traceback

import fcntl

def _close_on_exec(fd):
    flags = fcntl.fcntl(fd, fcntl.F_GETFD)
    flags |= fcntl.FD_CLOEXEC
    fcntl.fcntl(fd, fcntl.F_SETFD, flags)

class SafeAtoms(dict):

    def __init__(self, atoms):
        dict.__init__(self)
        for key, value in atoms.items():
            self[key] = value.replace('"', '\\"')

    def __getitem__(self, k):
        if k.startswith("{"):
            kl = k.lower()
            if kl in self:
                return super(SafeAtoms, self).__getitem__(kl)
            else:
                return "-"
        if k in self:
            return super(SafeAtoms, self).__getitem__(k)
        else:
            return '-'

class Logger(object):

    LOG_LEVELS = {
        "critical": logging.CRITICAL,
        "error": logging.ERROR,
        "warning": logging.WARNING,
        "info": logging.INFO,
        "debug": logging.DEBUG
    }

    error_fmt = r"%(asctime)s [%(process)d] [%(levelname)s] %(message)s"
    datefmt = r"%Y-%m-%d %H:%M:%S"

    access_log_format = '"%(h)s %(l)s %(u)s [%(t)s] "%(r)s" %(s)s %(b)s "%(f)s" "%(a)s"'
    access_fmt = "%(message)s"

    def __init__(self, cfg=None):
        self.error_log = logging.getLogger("meinheld.error")
        self.access_log = logging.getLogger("meinheld.access")
        self.error_handlers = []
        self.access_handlers = []
        self.setup(cfg)

    def setup(self, cfg):
        self.error_log.setLevel(logging.ERROR)
        self.access_log.setLevel(logging.INFO)

        self._set_handler(self.error_log, logging.Formatter(self.error_fmt, self.datefmt))
        self._set_handler(self.access_log, fmt=logging.Formatter(self.access_fmt))

    def critical(self, msg, *args, **kwargs):
        self.error_log.critical(msg, *args, **kwargs)

    def error(self, exc, val, tb):
        from traceback import format_exception
        msg = ''.join(format_exception(exc, val, tb))
        self.error_log.error(msg)

    def warning(self, msg, *args, **kwargs):
        self.error_log.warning(msg, *args, **kwargs)

    def info(self, msg, *args, **kwargs):
        self.error_log.info(msg, *args, **kwargs)

    def debug(self, msg, *args, **kwargs):
        self.error_log.debug(msg, *args, **kwargs)

    def exception(self, msg, *args):
        self.error_log.exception(msg, *args)

    def log(self, lvl, msg, *args, **kwargs):
        if isinstance(lvl, basestring):
            lvl = self.LOG_LEVELS.get(lvl.lower(), logging.INFO)
        self.error_log.log(lvl, msg, *args, **kwargs)

    def access(self, environ):
        """ Seee http://httpd.apache.org/docs/2.0/logs.html#combined
        for format details
        """

        # status = resp.status.split(None, 1)[0]
        atoms = {
                'h': environ.get('REMOTE_ADDR', '-'),
                'l': '-',
                'u': '-', # would be cool to get username from basic auth header
                't': environ.get('LOCAL_TIME', '-'),
                'r': "%s %s %s" % (environ.get('REQUEST_METHOD', '-'), environ.get('PATH_INFO', '-'), environ.get('SERVER_PROTOCOL', 'HTTP/1.0')),
                's': str(environ.get('STATUS_CODE', '-')),
                'b': str(environ.get('SEND_BYTES', '-')),
                'f': environ.get('HTTP_REFERER', '-'),
                'a': environ.get('HTTP_USER_AGENT', '-'),
                'T': str(environ.get('REQUEST_TIME', 1) /100),
                'D': str(environ.get('REQUEST_TIME', 1)),
                'p': "<%s>" % os.getpid()
                }
        # add request headers
        
        for k, v in environ.items():
            if k.startswith('HTTP_'):
                #header
                header = "{%s}i" % k[5:].lower()
                atoms[header] = v

        # atoms.update(dict([("{%s}i" % k.lower(),v) for k, v in req_headers]))

        # # add response headers
        # atoms.update(dict([("{%s}o" % k.lower(),v) for k, v in resp.headers]))

        # # wrap atoms:
        # # - make sure atoms will be test case insensitively
        # # - if atom doesn't exist replace it by '-'
        
        safe_atoms = SafeAtoms(atoms)

        try:
            # self.access_log.info(self.access_log_format % safe_atoms)
            self.access_log.info(self.access_log_format % safe_atoms)
        except:
            self.error(*sys.exc_info())


    def reopen_files(self):
        for log in (self.error_log, self.access_log):
            for handler in log.handlers:
                if isinstance(handler, logging.FileHandler):
                    handler.acquire()
                    try:
                        if handler.stream:
                            handler.stream.close()
                            handler.stream = open(handler.baseFilename,
                                    handler.mode)
                    finally:
                        handler.release()

    def close_on_exec(self):
        for log in (self.error_log, self.access_log):
            for handler in log.handlers:
                if isinstance(handler, logging.FileHandler):
                    handler.acquire()
                    try:
                        if handler.stream:
                            _close_on_exec(handler.stream.fileno())
                    finally:
                        handler.release()


    def _get_handler(self, log):
        for h in log.handlers:
            if getattr(h, "_meinheld", False) == True:
                return h

    def _set_handler(self, log, fmt):
        h = self._get_handler(log)
        if h:
            log.handlers.remove(h)

        h = logging.StreamHandler()
        h._meinheld = True
        h.setFormatter(fmt)
        log.addHandler(h)

def _error(self, exc, val, tb):
    from traceback import format_exception
    msg = ''.join(format_exception(exc, val, tb))
    self.error_log.error(msg)

def _access(self, environ):
    """ Seee http://httpd.apache.org/docs/2.0/logs.html#combined
    for format details
    """

    # status = resp.status.split(None, 1)[0]
    atoms = {
            'h': environ.get('REMOTE_ADDR', '-'),
            'l': '-',
            'u': '-', # would be cool to get username from basic auth header
            't': "[%s]" % environ.get('LOCAL_TIME', '-'),
            'r': "%s %s %s" % (environ.get('REQUEST_METHOD', '-'), environ.get('PATH_INFO', '-'), environ.get('SERVER_PROTOCOL', 'HTTP/1.0')),
            's': str(environ.get('STATUS_CODE', '-')),
            'b': str(environ.get('SEND_BYTES', '-')),
            'f': environ.get('HTTP_REFERER', '-'),
            'a': environ.get('HTTP_USER_AGENT', '-'),
            'T': str(environ.get('REQUEST_TIME', 1) /100),
            'D': str(environ.get('REQUEST_TIME', 1)),
            'p': "<%s>" % os.getpid()
            }
    # add request headers
    
    for k, v in environ.items():
        if k.startswith('HTTP_'):
            #header
            header = "{%s}i" % k[5:].lower()
            atoms[header] = v

    # atoms.update(dict([("{%s}i" % k.lower(),v) for k, v in req_headers]))

    # # add response headers
    # atoms.update(dict([("{%s}o" % k.lower(),v) for k, v in resp.headers]))

    # # wrap atoms:
    # # - make sure atoms will be test case insensitively
    # # - if atom doesn't exist replace it by '-'
    
    safe_atoms = SafeAtoms(atoms)

    try:
        msg = self.cfg.access_log_format % safe_atoms
        self.access_log.info(msg)
    except:
        self.error(*sys.exc_info())

logger = Logger()
from meinheld import server
server.set_access_logger(logger)
server.set_error_logger(logger)

