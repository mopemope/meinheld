# Copyright (c) 2005-2006, Bob Ippolito
# Copyright (c) 2007, Linden Research, Inc.
# Copyright (c) 2009-2010 Denis Bilenko
# Copyright (c) 2010 Yutaka Matsubara
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

"""Cooperative socket module.

This module provides socket operations and some related functions.
The API of the functions and classes matches the API of the corresponding
items in standard :mod:`socket` module exactly, but the synchronous functions
in this module only block the current greenlet and let the others run.

For convenience, exceptions (like :class:`error <socket.error>` and :class:`timeout <socket.timeout>`)
as well as the constants from :mod:`socket` module are imported into this module.
"""
import sys
def is_py3():
    return sys.hexversion >=  0x03000000

def is_py34():
    return sys.hexversion >=  0x03040000

__implements__ = ['getaddrinfo',
                  'gethostbyname',
                  'socket',
                  'SocketType',
                  'fromfd',
                  'socketpair']

# non-standard functions that this module provides:
__extensions__ = ['wait_read',
                  'wait_write',
                  'wait_readwrite']

# standard functions and classes that this module re-imports
__imports__ = ['error',
               'gaierror',
               'getfqdn',
               'herror',
               'htonl',
               'htons',
               'ntohl',
               'ntohs',
               'inet_aton',
               'inet_ntoa',
               'inet_pton',
               'inet_ntop',
               'timeout',
               'gethostname',
               'getprotobyname',
               'getservbyname',
               'getservbyport',
               'getdefaulttimeout',
               'setdefaulttimeout',
               # Python 2.5 and older:
               'RAND_add',
               'RAND_egd',
               'RAND_status',
               ]

patched = True

import sys
import time
import random
import re
import platform

from errno import EINVAL
from errno import EWOULDBLOCK
from errno import EINPROGRESS
from errno import EALREADY
from errno import EAGAIN
from errno import EISCONN
from os import strerror

try:
    from errno import EBADF
except ImportError:
    EBADF = 9

import _socket
error = _socket.error
timeout = _socket.timeout
__socket__ = __import__('socket')
if is_py3():
    _realsocket = _socket.socket
else:
    _realsocket = _socket.socket
    _fileobject = __socket__._fileobject
gaierror = _socket.gaierror

gethostbyname = _socket.gethostbyname
getaddrinfo = _socket.getaddrinfo


for name in __imports__[:]:
    try:
        value = getattr(__socket__, name)
        globals()[name] = value
    except AttributeError:
        __imports__.remove(name)

if is_py3():
    for name in __socket__.__all__:
        value = getattr(__socket__, name)
        if isinstance(value, (int, str)):
            globals()[name] = value
            __imports__.append(name)
else:
    for name in __socket__.__all__:
        value = getattr(__socket__, name)
        if isinstance(value, (int, long, basestring)):
            globals()[name] = value
            __imports__.append(name)

del name, value

SOCKETMETHODS = ('bind', 'fileno', 'listen', 'getpeername', 'getsockname', 'getsockopt', 'setsockopt')

if 'inet_ntop' not in globals():
    # inet_ntop is required by our implementation of getaddrinfo

    def inet_ntop(address_family, packed_ip):
        if address_family == AF_INET:
            return inet_ntoa(packed_ip)
        # XXX: ipv6 won't work on windows
        raise NotImplementedError('inet_ntop() is not available on this platform')

from meinheld import server, cancel_wait


def wait_read(fileno, timeout=None):
    if not timeout:
        timeout = 0
    server.trampoline(fileno, read=True, timeout=int(timeout))

def wait_write(fileno, timeout=None):
    if not timeout:
        timeout = 0
    server.trampoline(fileno, write=True, timeout=int(timeout))

def wait_readwrite(fileno, timeout=None):
    if not timeout:
        timeout = 0
    server.trampoline(fileno, read=True, write=True, timeout=int(timeout))



if sys.version_info[:2] < (2, 7):
    _get_memory = buffer
else:
    def _get_memory(string, offset):
        return memoryview(string)[offset:]


class _closedsocket(object):
    __slots__ = []

    def _dummy(*args):
        raise error(EBADF, 'Bad file descriptor')
    # All _delegate_methods must also be initialized here.
    send = recv = recv_into = sendto = recvfrom = recvfrom_into = _dummy
    __getattr__ = _dummy


_delegate_methods = ("recv", "recvfrom", "recv_into", "recvfrom_into", "send", "sendto", 'sendall')

timeout_default = object()

use_kqueue = platform.system() in ("Darwin", "FreeBSD")

def internal_accept(s):
    sock = s._sock
    while True:
        try:
            client_socket, address = sock.accept()
            break
        except error as ex:
            if ex.errno != EWOULDBLOCK or s.timeout == 0.0:
                raise
            #sys.exc_clear()
        wait_read(sock.fileno(), timeout=self.timeout)
    return socket(_sock=client_socket), address

def internal_close(s):
    cancel_wait(s._sock.fileno())
    s._sock = _closedsocket()
    dummy = s._sock._dummy
    for method in _delegate_methods:
        setattr(s, method, dummy)

def internal_connect(s, address):
    if s.timeout == 0.0:
        return s._sock.connect(address)

    # When poll for connect() with kqueue, poll for EVFILT_WRITE.
    wait = wait_write if use_kqueue else wait_readwrite

    sock = s._sock
    if s.timeout is None:
        while True:
            err = sock.getsockopt(SOL_SOCKET, SO_ERROR)
            if err:
                raise error(err, strerror(err))
            result = sock.connect_ex(address)
            if not result or result == EISCONN:
                break
            elif (result in (EWOULDBLOCK, EINPROGRESS, EALREADY)) or (result == EINVAL and is_windows):
                wait(sock.fileno())
            else:
                raise error(result, strerror(result))
    else:
        end = time.time() + s.timeout
        while True:
            err = sock.getsockopt(SOL_SOCKET, SO_ERROR)
            if err:
                raise error(err, strerror(err))
            result = sock.connect_ex(address)
            if not result or result == EISCONN:
                break
            elif (result in (EWOULDBLOCK, EINPROGRESS, EALREADY)) or (result == EINVAL and is_windows):
                timeleft = end - time.time()
                if timeleft <= 0:
                    raise timeout('timed out')
                wait(sock.fileno(), timeout=timeleft)
            else:
                raise error(result, strerror(result))

def internal_connect_ex(s, address):
    try:
        return s.connect(address) or 0
    except timeout:
        return EAGAIN
    except error as ex:
        if type(ex) is error:
            return ex.errno
        else:
            raise # gaierror is not silented by connect_ex

def internal_recv(s, *args):
    # print("internal_recv")
    sock = s._sock # keeping the reference so that fd is not closed during waiting
    while True:
        try:
            return sock.recv(*args)
        except error as ex:
            if ex.errno == EBADF:
                raise 
            if ex.errno != EWOULDBLOCK or s.timeout == 0.0:
                raise
            # QQQ without clearing exc_info test__refcount.test_clean_exit fails
            #sys.exc_clear()
        try:
            wait_read(sock.fileno(), timeout=s.timeout)
        except error as ex:
            if ex.errno == EBADF:
                return ''
            raise

def internal_recvfrom(s, *args):
    # print("internal_recvfrom")
    sock = s._sock
    while True:
        try:
            return sock.recvfrom(*args)
        except error as ex:
            if ex.errno != EWOULDBLOCK or s.timeout == 0.0:
                raise
            #sys.exc_clear()
        wait_read(sock.fileno(), timeout=s.timeout)

def internal_recvfrom_into(s, *args):
    # print("internal_recvfrom_into")
    sock = s._sock
    while True:
        try:
            return sock.recvfrom_into(*args)
        except error as ex:
            if ex.errno != EWOULDBLOCK or s.timeout == 0.0:
                raise
            #sys.exc_clear()
        wait_read(sock.fileno(), timeout=s.timeout)

def internal_recv_into(s, *args):
    # print("internal_recv_into")
    sock = s._sock
    while True:
        try:
            return sock.recv_into(*args)
        except error as ex:
            if ex.errno == EBADF:
                return 0
            if ex.errno != EWOULDBLOCK or s.timeout == 0.0:
                raise
            #sys.exc_clear()
        try:
            wait_read(sock.fileno(), timeout=s.timeout)
        except error as ex:
            if ex.errno == EBADF:
                return 0
            raise

def internal_send(s, data, flags=0, timeout=timeout_default):
    sock = s._sock
    if timeout is timeout_default:
        timeout = s.timeout
    try:
        return sock.send(data, flags)
    except error as ex:
        if ex.errno != EWOULDBLOCK or timeout == 0.0:
            raise
        #sys.exc_clear()
        try:
            wait_write(sock.fileno(), timeout=timeout)
        except error as ex:
            if ex.errno == EBADF:
                return 0
            raise
        try:
            return sock.send(data, flags)
        except error as ex2:
            if ex2.errno == EWOULDBLOCK:
                return 0
            raise

if is_py3():
    def internal_sendall(s, data, flags=0):
        # this sendall is also reused by SSL subclasses (both from ssl and sslold modules),
        # so it should not call s._sock methods directly
        if s.timeout is None:
            data_sent = 0
            while data_sent < len(data):
                data_sent += s.send(_get_memory(data, data_sent), flags)
        else:
            timeleft = s.timeout
            end = time.time() + timeleft
            data_sent = 0
            while True:
                data_sent += s.send(_get_memory(data, data_sent), flags, timeout=timeleft)
                if data_sent >= len(data):
                    break
                timeleft = end - time.time()
                if timeleft <= 0:
                    raise timeout('timed out')
else:
    def internal_sendall(s, data, flags=0):
        if isinstance(data, unicode):
            data = data.encode()
        # this sendall is also reused by SSL subclasses (both from ssl and sslold modules),
        # so it should not call s._sock methods directly
        if s.timeout is None:
            data_sent = 0
            while data_sent < len(data):
                data_sent += s.send(_get_memory(data, data_sent), flags)
        else:
            timeleft = s.timeout
            end = time.time() + timeleft
            data_sent = 0
            while True:
                data_sent += s.send(_get_memory(data, data_sent), flags, timeout=timeleft)
                if data_sent >= len(data):
                    break
                timeleft = end - time.time()
                if timeleft <= 0:
                    raise timeout('timed out')

def internal_sendto(s, *args):
    sock = s._sock
    try:
        return sock.sendto(*args)
    except error as ex:
        if ex.errno != EWOULDBLOCK or timeout == 0.0:
            raise
        #sys.exc_clear()
        wait_write(sock.fileno(), timeout=s.timeout)
        try:
            return sock.sendto(*args)
        except error as ex2:
            if ex2.errno == EWOULDBLOCK:
                return 0
            raise

def internal_settimeout(s, howlong):
    if howlong is not None:
        try:
            f = howlong.__float__
        except AttributeError:
            raise TypeError('a float is required')
        howlong = f()
        if howlong < 0.0:
            raise ValueError('Timeout value out of range')
    s.timeout = howlong

def internal_gettimeout(s):
    return s.timeout

def internal_shutdown(s, how):
    cancel_wait(s._sock.fileno())
    s._sock.shutdown(how)

if is_py3():
    class socket(object):

        patched = True
        #__slots__ = ["__weakref__", "_io_refs", "_closed", "_sock", "timeout"]

        def __init__(self, family=AF_INET, type=SOCK_STREAM, proto=0, fileno=None):

            self._sock = _socket.socket(family, type, proto, fileno)

            self._io_refs = 0
            self._closed = False
            self._sock.setblocking(0)
            self.setsockopt(IPPROTO_TCP, TCP_NODELAY, 1)
            self.timeout = _socket.getdefaulttimeout()

        def __enter__(self):
            return self

        def __exit__(self, *args):
            if not self._closed:
                self.close()

        def dup(self):
            """dup() -> socket object

            Return a new socket object connected to the same system resource.
            Note, that the new socket does not inherit the timeout."""
            fd = dup(self.fileno())
            sock = self.__class__(self.family, self.type, self.proto, fileno=fd)
            sock.settimeout(self.gettimeout())
            return sock
        
        def _decref_socketios(self):
            if self._io_refs > 0:
                self._io_refs -= 1
            if self._closed:
                self.close()
        
        def _real_close(self, _ss=_socket.socket):
            # This function should not reference any globals. See issue #808164.
            _ss.close(self._sock)

        def close(self):
            # This function should not reference any globals. See issue #808164.
            self._closed = True
            if self._io_refs <= 0:
                self._real_close()

        def detach(self):
            """detach() -> file descriptor

            Close the socket object without closing the underlying file descriptor.
            The object cannot be used after this call, but the file descriptor
            can be reused for other purposes.  The file descriptor is returned.
            """
            self._closed = True
            return self._sock.detach()
        
        def accept(self):
            raise NotImplementedError()

        makefile = __socket__.socket.makefile
        connect = internal_connect
        connect_ex = internal_connect_ex
        recv = internal_recv
        recvfrom = internal_recvfrom
        recvfrom_into = internal_recvfrom_into
        recv_into = internal_recv_into
        send = internal_send
        sendall = internal_sendall
        sendto = internal_sendto
        settimeout = internal_settimeout
        gettimeout = internal_gettimeout
        shutdown = internal_shutdown

        family = property(lambda self: self._sock.family, doc="the socket family")
        type = property(lambda self: self._sock.type, doc="the socket type")
        proto = property(lambda self: self._sock.proto, doc="the socket protocol")
        _s = ("def %s(self, *args): return self._sock.%s(*args)\n\n"
              "%s.__doc__ = _realsocket.%s.__doc__\n")

        for _m in SOCKETMETHODS:
            exec(_s % (_m, _m, _m, _m))
        del _m, _s
else:
    class socket(object):
        
        patched = True

        def __init__(self, family=AF_INET, type=SOCK_STREAM, proto=0, _sock=None):
            if _sock is None:
                self._sock = _realsocket(family, type, proto)
                self.timeout = _socket.getdefaulttimeout()
            else:
                if hasattr(_sock, '_sock'):
                    self._sock = _sock._sock
                    self.timeout = getattr(_sock, 'timeout', False)
                    if self.timeout is False:
                        self.timeout = _socket.getdefaulttimeout()
                else:
                    self._sock = _sock
                    self.timeout = _socket.getdefaulttimeout()
            self._sock.setblocking(0)
            self.setsockopt(IPPROTO_TCP, TCP_NODELAY, 1)

        def __repr__(self):
            return '<%s at %s %s>' % (type(self).__name__, hex(id(self)), self._formatinfo())

        def __str__(self):
            return '<%s %s>' % (type(self).__name__, self._formatinfo())

        def _formatinfo(self):
            try:
                fileno = self.fileno()
            except Exception as ex:
                fileno = str(ex)
            try:
                sockname = self.getsockname()
                sockname = '%s:%s' % sockname
            except Exception:
                sockname = None
            try:
                peername = self.getpeername()
                peername = '%s:%s' % peername
            except Exception:
                peername = None
            result = 'fileno=%s' % fileno
            if sockname is not None:
                result += ' sock=' + str(sockname)
            if peername is not None:
                result += ' peer=' + str(peername)
            if self.timeout is not None:
                result += ' timeout=' + str(self.timeout)
            return result

        def __enter__(self):
            return self

        def __exit__(self, *args):
            if not self._closed:
                self.close()

        def dup(self):
            """dup() -> socket object

            Return a new socket object connected to the same system resource.
            Note, that the new socket does not inherit the timeout."""
            return socket(_sock=self._sock)

        def makefile(self, mode='r', bufsize=-1):
            # note that this does not inherit timeout either (intentionally, because that's
            # how the standard socket behaves)
            return _fileobject(self.dup(), mode, bufsize)
        
        def accept(self):
            raise NotImplementedError()
        close = internal_close
        connect = internal_connect
        connect_ex = internal_connect_ex
        recv = internal_recv
        recvfrom = internal_recvfrom
        recvfrom_into = internal_recvfrom_into
        recv_into = internal_recv_into
        send = internal_send
        sendall = internal_sendall
        sendto = internal_sendto
        settimeout = internal_settimeout
        gettimeout = internal_gettimeout
        shutdown = internal_shutdown

        family = property(lambda self: self._sock.family, doc="the socket family")
        type = property(lambda self: self._sock.type, doc="the socket type")
        proto = property(lambda self: self._sock.proto, doc="the socket protocol")

        # delegate the functions that we haven't implemented to the real socket object

        _s = ("def %s(self, *args): return self._sock.%s(*args)\n\n"
              "%s.__doc__ = _realsocket.%s.__doc__\n")

        for _m in SOCKETMETHODS:
            exec(_s % (_m, _m, _m, _m))
        del _m, _s

if is_py34():
    SocketType = __socket__.SocketType
else:
    SocketType = socket

if hasattr(_socket, 'socketpair'):

    def socketpair(*args):
        one, two = _socket.socketpair(*args)
        return socket(_sock=one), socket(_sock=two)
else:
    __all__.remove('socketpair')

if is_py3():
    pass
else:
    if hasattr(_socket, 'fromfd'):

        def fromfd(*args):
            return socket(_sock=_socket.fromfd(*args))
    else:
        __all__.remove('fromfd')




try:
    _GLOBAL_DEFAULT_TIMEOUT = __socket__._GLOBAL_DEFAULT_TIMEOUT
except AttributeError:
    _GLOBAL_DEFAULT_TIMEOUT = object()

_have_ssl = False

# if is_py3():
    # pass
# else:
    # try:
        # from meinheld.ssl import sslwrap_simple as ssl, SSLError as sslerror
        # _have_ssl = True
    # except ImportError:
        # try:
            # from meinheld.sslold import ssl, sslerror
            # _have_ssl = True
        # except ImportError:
            # pass

    # if sys.version_info[:2] <= (2, 5) and _have_ssl:
        # __implements__.extend(['ssl', 'sslerror', 'SSLType'])


__all__ = __implements__ + __extensions__ + __imports__
