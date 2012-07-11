# Copyright (c) 2009-2010 Denis Bilenko. See LICENSE for details.
import sys

noisy = True

__all__ = [
            'patch_all',
            'patch_werkzeug',
            'patch_socket',
            'patch_ssl',
          ]

def patch_werkzeug():
    """ Replace werkzeug local get_ident """
    try:
        from werkzeug import local
        from meinheld import get_ident
        local.get_ident = get_ident
    except ImportError:
        pass

def patch_socket(aggressive=False):
    """Replace the standard socket object with meinheld's cooperative sockets.
    """
    from meinheld import socket
    _socket = __import__('socket')
    _socket.patched = True
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

def patch_ssl():
    try:
        _ssl = __import__('ssl')
    except ImportError:
        return
    from meinheld.ssl import SSLSocket, wrap_socket, get_server_certificate, sslwrap_simple
    _ssl.SSLSocket = SSLSocket
    _ssl.wrap_socket = wrap_socket
    _ssl.get_server_certificate = get_server_certificate
    _ssl.sslwrap_simple = sslwrap_simple



def patch_all(werkzeug=True, socket=True, ssl=True, aggressive=True):
    """Do all of the default monkey patching (calls every other function in this module."""
    # order is important
    if werkzeug:
        patch_werkzeug()
    if socket:
        patch_socket(aggressive=aggressive)
    # if ssl:
        # patch_ssl()


