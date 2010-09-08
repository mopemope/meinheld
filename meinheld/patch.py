# Copyright (c) 2009-2010 Denis Bilenko. See LICENSE for details.
"""Make the standard library cooperative.

The functions in this module patch parts of the standard library with compatible cooperative counterparts
from :mod:`gevent` package.

To patch an individual module call the corresponding ``patch_*`` function. For example, to patch
socket module only, call :func:`patch_socket`. To patch all default modules, call ``gevent.monkey.patch_all()``.

Monkey can also patch thread and threading to become greenlet-based. So :func:`thread.start_new_thread`
starts a new greenlet instead and :class:`threading.local` becomes a greenlet-local storage.

Monkey patches:

* :mod:`socket` module -- :func:`patch_socket`

  - :class:`socket`
  - :class:`SocketType`
  - :func:`socketpair`
  - :func:`fromfd`
  - :func:`ssl` and :class:`sslerror`
  - :func:`socket.getaddrinfo`
  - :func:`socket.gethostbyname`
  - It is possible to disable dns patching by passing ``dns=False`` to :func:`patch_socket` of :func:`patch_all`
  - If ssl is not available (Python < 2.6 without ``ssl`` and ``PyOpenSSL`` packages installed) then :func:`ssl` is removed from the target :mod:`socket` module.

* :mod:`ssl` module -- :func:`patch_ssl`

  - :class:`SSLSocket`
  - :func:`wrap_socket`
  - :func:`get_server_certificate`
  - :func:`sslwrap_simple`

* :mod:`os` module -- :func:`patch_os`

  - :func:`fork`

* :mod:`time` module -- :func:`patch_time`

  - :func:`time`

* :mod:`select` module -- :func:`patch_select`

  - :func:`select`
  - Removes polling mechanisms that :mod:`gevent.select` does not simulate: poll, epoll, kqueue, kevent

* :mod:`thread` and :mod:`threading` modules -- :func:`patch_thread`

  - Become greenlet-based.
  - :func:`get_ident`
  - :func:`start_new_thread`
  - :class:`LockType`
  - :func:`allocate_lock`
  - :func:`exit`
  - :func:`stack_size`
  - thread-local storage becomes greenlet-local storage
"""

import sys

noisy = True



"""
def patch_time():
    from gevent.hub import sleep
    _time = __import__('time')
    _time.sleep = sleep
"""

"""
def patch_thread(threading=True, _threading_local=True):
    from gevent import thread as green_thread
    thread = __import__('thread')
    if thread.exit is not green_thread.exit:
        thread.get_ident = green_thread.get_ident
        thread.start_new_thread = green_thread.start_new_thread
        thread.LockType = green_thread.LockType
        thread.allocate_lock = green_thread.allocate_lock
        thread.exit = green_thread.exit
        if hasattr(green_thread, 'stack_size'):
            thread.stack_size = green_thread.stack_size
        from gevent.local import local
        thread._local = local
        if threading:
            if noisy and 'threading' in sys.modules:
                sys.stderr.write("gevent.monkey's warning: 'threading' is already imported\n\n")
            threading = __import__('threading')
            threading.local = local
        if _threading_local:
            _threading_local = __import__('_threading_local')
            _threading_local.local = local
"""

def patch_socket(aggressive=True):
    """Replace the standard socket object with gevent's cooperative sockets.
    
    If *dns* is true, also patch dns functions in :mod:`socket`.
    """
    from meinheld import socket
    _socket = __import__('socket')
    _socket.socket = socket.socket
    _socket.SocketType = socket.SocketType
    if hasattr(socket, 'socketpair'):
        _socket.socketpair = socket.socketpair
    if hasattr(socket, 'fromfd'):
        _socket.fromfd = socket.fromfd
    try:
        from meinheld.socket import ssl, sslerror
        _socket.ssl = ssl
        _socket.sslerror = sslerror
    except ImportError:
        if aggressive:
            try:
                del _socket.ssl
            except AttributeError:
                pass

"""
def patch_ssl():
    try:
        _ssl = __import__('ssl')
    except ImportError:
        return
    from gevent.ssl import SSLSocket, wrap_socket, get_server_certificate, sslwrap_simple
    _ssl.SSLSocket = SSLSocket
    _ssl.wrap_socket = wrap_socket
    _ssl.get_server_certificate = get_server_certificate
    _ssl.sslwrap_simple = sslwrap_simple
"""



def patch_all(socket=True, ssl=True, aggressive=True):
    """Do all of the default monkey patching (calls every other function in this module."""
    # order is important
    if socket:
        patch_socket(aggressive=aggressive)
    if ssl:
        patch_ssl()


