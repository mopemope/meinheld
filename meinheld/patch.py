# Copyright (c) 2009-2010 Denis Bilenko. See LICENSE for details.
import sys

noisy = True

__all__ = [
            'patch_all',
            'patch_socket',
          ]

def patch_socket(aggressive=False):
    """Replace the standard socket object with meinheld's cooperative sockets.
    """
    from meinheld import msocket
    _socket = __import__('socket')
    _socket.patched = True
    _socket.socket = msocket.socket
    _socket.SocketType = msocket.SocketType
    if hasattr(msocket, 'socketpair'):
        _socket.socketpair = msocket.socketpair
    if hasattr(msocket, 'fromfd'):
        _socket.fromfd = msocket.fromfd

# def patch_ssl():
    # try:
        # _ssl = __import__('ssl')
    # except ImportError:
        # return
    # from meinheld.ssl import SSLSocket, wrap_socket, get_server_certificate, sslwrap_simple
    # _ssl.SSLSocket = SSLSocket
    # _ssl.wrap_socket = wrap_socket
    # _ssl.get_server_certificate = get_server_certificate
    # _ssl.sslwrap_simple = sslwrap_simple



def patch_all(socket=True, aggressive=True):
    """Do all of the default monkey patching (calls every other function in this module."""
    # order is important
    if socket:
        patch_socket(aggressive=aggressive)
    # if ssl:
        # patch_ssl()


