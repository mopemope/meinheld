# Copyright (c) 2009-2010 Denis Bilenko. See LICENSE for details.
import sys

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

def patch_all(socket=True, aggressive=True):
    """Do all of the default monkey patching (calls every other function in this module."""
    # order is important
    if socket:
        patch_socket(aggressive=aggressive)
    # if ssl:
        # patch_ssl()


