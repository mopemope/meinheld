import collections
import string
import struct
from base64 import b64encode

import sys
def is_py3():
    return sys.hexversion >=  0x3000000

if is_py3():
    from itertools import cycle
    unicode = str
else:
    from itertools import cycle
    from itertools import imap as map
    from itertools import izip as zip
import random
import socket

try:
    from hashlib import md5, sha1
except ImportError: #pragma NO COVER
    from md5 import md5
    from sha import sha as sha1

from meinheld import server, patch
from meinheld.common import Continuation, CLIENT_KEY, CONTINUATION_KEY
patch.patch_socket()

import socket

def _wsgi_to_bytes(s):
    if isinstance(s, bytes):
        return s
    else:
        return s.encode('iso-8859-1')

def _extract_comma(value):
    return [x.strip() for x in value.split(',')]


class WebSocketMiddleware(object):

    def __init__(self, app):
        self.app = app

    def _extract_number(self, value):
        out = ""
        spaces = 0
        for char in value:
            if char in string.digits:
                out += char
            elif char == " ":
                spaces += 1
        return int(out) / spaces

    def setup(self, environ):
        protocol_version = None
        if not ("Upgrade" in _extract_comma(environ.get('HTTP_CONNECTION','')) and
                environ.get('HTTP_UPGRADE','').lower() == 'websocket'):
            return 
        if 'HTTP_SEC_WEBSOCKET_KEY' in environ:
            protocol_version = environ['HTTP_SEC_WEBSOCKET_VERSION']  # RFC 6455
            if protocol_version in ('13',):  #skip version 4,5,6,7,8
                protocol_version = int(protocol_version)
            else:
                # Unknown
                raise NotImplementedError("Not Supported")
        else:
            raise NotImplementedError("Not Supported")

        # Get the underlying socket and wrap a WebSocket class around it
        client = environ[CLIENT_KEY]
        sock = socket.fromfd(client.get_fd(), socket.AF_INET, socket.SOCK_STREAM)
        ws = WebSocket(sock, environ, protocol_version)
       
        # If it's new-version, we need to work out our challenge response
        key1 = _wsgi_to_bytes(environ['HTTP_SEC_WEBSOCKET_KEY'])
        key2 = _wsgi_to_bytes('258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
        digest = sha1(key1 + key2).digest()
        response = b64encode(digest).strip()
        if is_py3():
            response = response.decode("iso-8859-1")
       
        # Start building the response
        location = 'ws://%s%s%s' % (
            environ.get('HTTP_HOST'), 
            environ.get('SCRIPT_NAME'), 
            environ.get('PATH_INFO')
        )
        qs = environ.get('QUERY_STRING')
        if qs:
            location += '?' + qs
        if protocol_version == 13:
            handshake_reply = ("HTTP/1.1 101 Switching Protocols\r\n"
                               "Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Origin: %s\r\n"
                               "Sec-WebSocket-Accept: %s\r\n"
                               "\r\n"% (
                    environ.get('HTTP_ORIGIN'),
                    response))
            if 'HTTP_SEC_WEBSOCKET_PROTOCOL' in environ:
                handshake_reply += 'Sec-WebSocket-Protocol: %s\r\n' % environ.get('HTTP_SEC_WEBSOCKET_PROTOCOL')
        else: #pragma NO COVER
            raise ValueError("Unknown WebSocket protocol version.") 

        sock.sendall(_wsgi_to_bytes(handshake_reply))
        environ['wsgi.websocket'] = ws
        return True

    def spawn_call(self, environ, start_response):
        result = self.setup(environ)
        response = None
        try:
            response = self.app(environ, start_response)
            return response
        finally:
            if result and response != -1:
                ws = environ.pop('wsgi.websocket')
                ws._send_closing_frame(True)
                client = environ[CLIENT_KEY]
                client.set_closed(1)

    def __call__(self, environ, start_response):
        client = environ[CLIENT_KEY]
        c = Continuation(client)
        environ[CONTINUATION_KEY] = c

        return self.spawn_call(environ, start_response)

class WebSocketWSGI(object):

    def __init__(self, handler):
        self.handler = handler
        self.protocol_version = None

    def __call__(self, environ, start_response):
        if not ("Upgrade" in _extract_comma(environ.get('HTTP_CONNECTION','')) and
                environ.get('HTTP_UPGRADE','').lower() == 'websocket'):
            # need to check a few more things here for true compliance
            start_response('400 Bad Request', [('Connection','close')])
            return [""]
    
        if 'HTTP_SEC_WEBSOCKET_KEY' in environ:
            protocol_version = environ['HTTP_SEC_WEBSOCKET_VERSION']  # RFC 6455
            if protocol_version in ('13',):  #skip version 4,5,6,7,8
                protocol_version = int(protocol_version)
            else:
                # Unknown
                raise NotImplementedError("Not Supported")
        else:
            # Unknown
            raise NotImplementedError("Not Supported")

        # Get the underlying socket and wrap a WebSocket class around it
        client = environ[CLIENT_KEY]
        sock = server._get_socket_fromfd(client.get_fd(), socket.AF_INET,
                socket.SOCK_STREAM)
        ws = WebSocket(sock, environ, self.protocol_version)

        # If it's new-version, we need to work out our challenge response
        key1 = _wsgi_to_bytes(environ['HTTP_SEC_WEBSOCKET_KEY'])
        key2 = _wsgi_to_bytes('258EAFA5-E914-47DA-95CA-C5AB0DC85B11)')
        digest = sha1(key1 + key2).digest()
        response = b64encode(digest).strip()
        if is_py3():
            response = response.decode("iso-8859-1")

        # Start building the response
        location = 'ws://%s%s%s' % (
            environ.get('HTTP_HOST'), 
            environ.get('SCRIPT_NAME'), 
            environ.get('PATH_INFO')
        )
        qs = environ.get('QUERY_STRING')
        if qs:
            location += '?' + qs
        if protocol_version == 13:
            handshake_reply = ("HTTP/1.1 101 Switching Protocols\r\n"
                               "Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Origin: %s\r\n"
                               "Sec-WebSocket-Accept: %s\r\n"
                               "\r\n"% (
                    environ.get('HTTP_ORIGIN'),
                    response))
            if 'HTTP_SEC_WEBSOCKET_PROTOCOL' in environ:
                handshake_reply += 'Sec-WebSocket-Protocol: %s\r\n' % environ.get('HTTP_SEC_WEBSOCKET_PROTOCOL')
        else: #pragma NO COVER
            raise ValueError("Unknown WebSocket protocol version.") 
        
        r = sock.sendall(_wsgi_to_bytes(handshake_reply))
        self.handler(ws)
        # Make sure we send the closing frame
        ws._send_closing_frame(True)
        # use this undocumented feature of eventlet.wsgi to ensure that it
        # doesn't barf on the fact that we didn't call start_response
        return [""]

    def _extract_number(self, value):
        """
        Utility function which, given a string like 'g98sd  5[]221@1', will
        return 9852211. Used to parse the Sec-WebSocket-Key headers.
        """
        out = ""
        spaces = 0
        for char in value:
            if char in string.digits:
                out += char
            elif char == " ":
                spaces += 1
        return int(out) / spaces

class WebSocket(object):
    """A websocket object that handles the details of
    serialization/deserialization to the socket.
    
    The primary way to interact with a :class:`WebSocket` object is to
    call :meth:`send` and :meth:`wait` in order to pass messages back
    and forth with the browser.  Also available are the following
    properties:
    
    path
        The path value of the request.  This is the same as the WSGI PATH_INFO variable, but more convenient.
    protocol
        The value of the Websocket-Protocol header.
    origin
        The value of the 'Origin' header.
    environ
        The full WSGI environment for this request.

    """
    def __init__(self, sock, environ, version=76):
        """
        :param socket: The eventlet socket
        :type socket: :class:`eventlet.greenio.GreenSocket`
        :param environ: The wsgi environment
        :param version: The WebSocket spec version to follow (default is 76)
        """
        self.socket = sock
        self.origin = environ.get('HTTP_ORIGIN')
        self.protocol = environ.get('HTTP_WEBSOCKET_PROTOCOL')
        self.path = environ.get('PATH_INFO')
        self.environ = environ
        self.version = version
        self.websocket_closed = False
        self._buf = b""
        self._msgs = collections.deque()
        #self._sendlock = semaphore.Semaphore()

    def _pack_message(self, message):
        """Pack the message inside ``00`` and ``FF``

        As per the dataframing section (5.3) for the websocket spec
        """
        if self.version in (13,):
            # payload
            opcode = 2
            if isinstance(message, unicode):  # text
                opcode = 1
                payload = message.encode('utf-8')
            else:
                payload = message
            if not isinstance(payload, bytes):
                raise TypeError("message should be str, unicode or bytes.")

            # header(fin,maskflag,opcode,length)
            fin = 0x80  #0x80:fin, 0:continuation
            mask = 0  #0:unmasked, 0x80:masked
            length = len(payload)
            if length < 126:
                header = struct.pack(">BB", fin|opcode, mask|length)
            elif 126 <= length <= 0xffff:
                header = struct.pack(">BBH", fin|opcode, mask|126, length)
            elif 0xffff < length <= 0xffffffffffffffff:
                header = struct.pack(">BBQ", fin|opcode, mask|127, length)
            else:
                #TODO: partial packet
                raise ValueError("Can't send over 64bit length. (partial packet are not supported)") 

            # maskdata, masked-payload
            maskdata = b''
            if mask:
                maskdata = struct.pack(">I", random.randint(0,0xffffffff))
                masklist = cycle(ord(x) for x in maskdata)
                payload = b''.join(chr(ord(d)^m) for d,m in zip(payload, masklist))

            packed = header + maskdata + payload
        else:
            raise ValueError("Unknown WebSocket protocol version.") 

        return packed

    def _parse_messages(self):
        """ Parses for messages in the buffer *buf*.  It is assumed that
        the buffer contains the start character for a message, but that it
        may contain only part of the rest of the message.

        Returns an array of messages, and the buffer remainder that
        didn't contain any full messages.
        """
        if self.version not in (13,):
            raise ValueError("Unknown WebSocket protocol version.")

        msgs = []
        buf = self._buf
        msg = None
        is_text = False
        while True:
            idx = 0
            if len(buf) < idx+2:
                return msgs
            if is_py3():
                b1, b2 = buf[idx], buf[idx+1]
            else:
                b1, b2 = ord(buf[idx]), ord(buf[idx+1])

            idx += 2
            fin = bool(b1 & 0x80)  #TODO with opcode==0
            opcode = b1 & 0x0f
            mask = bool(b2 & 0x80)
            length = (b2 & 0x7f)
            if length == 126:
                if len(buf) < idx+2:
                    return msgs
                length = struct.unpack('>H', buf[idx:idx+2])[0]
                idx += 2
            elif length == 127:
                if len(buf) < idx+8:
                    return msgs
                length = struct.unpack('>Q', buf[idx:idx+8])[0]
                idx += 8

            if mask:
                if len(buf) < idx + 4:
                    return msgs
                maskdata = buf[idx:idx+4]
                idx += 4

            if len(buf) < idx + length:
                return msgs

            data = buf[idx:idx+length]
            idx += length

            if mask:
                if is_py3():
                    data = ''.join(chr(d^m) for d,m in zip(data, cycle(maskdata)))
                    data = data.encode('iso-8859-1')
                else:
                    data = b''.join(chr(d^m) for d,m in zip(
                                                    map(ord, data),
                                                    cycle(map(ord, maskdata))
                                                    ))
            print(opcode, length, data[:16])
            if opcode == 0:  #continuation
                if is_text:
                    msg += data.decode('utf-8')
                else:
                    msg += data
            elif opcode == 1:  #text
                is_text = True
                msg = data.decode('utf-8')
            elif opcode == 2:  #binary
                is_text = False
                msg = data
            elif opcode == 8:  #close
                self.websocket_closed = True
                #TODO process 2byte close status
                break
            elif opcode == 9:  #ping
                pass  #TODO
            elif opcode == 10: #pong
                pass  #TODO
            else:
                raise ValueError("Don't understand how to parse this type of message: %r, %s" % (buf, idx))
            self._buf = buf = buf[idx:]
            if fin:
                msgs.append(msg)
                msg = None
        return msgs
    
    def send(self, message):
        """Send a message to the browser.  *message* should be
        convertable to a string; unicode objects should be encodable
        as utf-8."""
        packed = self._pack_message(message)
        # if two greenthreads are trying to send at the same time
        # on the same socket, sendlock prevents interleaving and corruption
        
        #self._sendlock.acquire()
        #try:
        return self.socket.sendall(packed)
        #finally:
        #    self._sendlock.release()

    def wait(self):
        """Waits for and deserializes messages. Returns a single
        message; the oldest not yet processed."""
        while not self._msgs:
            # Websocket might be closed already.
            if self.websocket_closed:
                return None
            # no parsed messages, must mean buf needs more data
            delta = self.socket.recv(8096)
            if delta == b'':
                return None
            self._buf += delta
            msgs = self._parse_messages()
            self._msgs.extend(msgs)
        return self._msgs.popleft()

    def _send_closing_frame(self, ignore_send_errors=False):
        """Sends the closing frame to the client, if required."""
        if self.version == 76 and not self.websocket_closed:
            try:
                self.socket.send("\xff\x00")
            except IOError:
                # Sometimes, like when the remote side cuts off the connection,
                # we don't care about this.
                if not ignore_send_errors: #pragma NO COVER
                    raise
            self.websocket_closed = True

    def close(self):
        """Forcibly close the websocket; generally it is preferable to
        return from the handler method."""
        self._send_closing_frame()
        self.socket.shutdown(True)
        self.socket.close()

