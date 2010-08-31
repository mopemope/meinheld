import collections
import string
import struct

try:
    from hashlib import md5
except ImportError: #pragma NO COVER
    from md5 import md5

from meinheld import server
from meinheld.common import Continuation, CLIENT_KEY, CONTINUATION_KEY
import greenlet

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
        if not (environ.get('HTTP_CONNECTION') == 'Upgrade' and
                environ.get('HTTP_UPGRADE') == 'WebSocket'):
            return 
    
        # See if they sent the new-format headers
        if 'HTTP_SEC_WEBSOCKET_KEY1' in environ:
            protocol_version = 76
            if 'HTTP_SEC_WEBSOCKET_KEY2' not in environ:
                # That's bad.
                return 
        else:
            protocol_version = 75

        # Get the underlying socket and wrap a WebSocket class around it
        client = environ[CLIENT_KEY]
        sock = server._get_socket_fromfd(client.get_fd())
        ws = WebSocket(sock, environ, protocol_version)
        
        # If it's new-version, we need to work out our challenge response
        if protocol_version == 76:
            key1 = self._extract_number(environ['HTTP_SEC_WEBSOCKET_KEY1'])
            key2 = self._extract_number(environ['HTTP_SEC_WEBSOCKET_KEY2'])
            # There's no content-length header in the request, but it has 8
            # bytes of data.
            key3 = environ['wsgi.input'].read(8)
            key = struct.pack(">II", key1, key2) + key3
            response = md5(key).digest()
        
        # Start building the response
        location = 'ws://%s%s%s' % (
            environ.get('HTTP_HOST'), 
            environ.get('SCRIPT_NAME'), 
            environ.get('PATH_INFO')
        )
        qs = environ.get('QUERY_STRING')
        if qs:
            location += '?' + qs
        if protocol_version == 75:
            handshake_reply = ("HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
                               "Upgrade: WebSocket\r\n"
                               "Connection: Upgrade\r\n"
                               "WebSocket-Origin: %s\r\n"
                               "WebSocket-Location: %s\r\n\r\n" % (
                    environ.get('HTTP_ORIGIN'),
                    location))
        elif protocol_version == 76:
            handshake_reply = ("HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
                               "Upgrade: WebSocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Origin: %s\r\n"
                               "Sec-WebSocket-Protocol: %s\r\n"
                               "Sec-WebSocket-Location: %s\r\n"
                               "\r\n%s"% (
                    environ.get('HTTP_ORIGIN'),
                    environ.get('HTTP_SEC_WEBSOCKET_PROTOCOL', 'default'),
                    location,
                    response))
        else: #pragma NO COVER
            raise ValueError("Unknown WebSocket protocol version.") 
        
        sock.sendall(handshake_reply)
        environ['wsgi.websocket'] = ws
        return True

    def __call__(self, environ, start_response):
        client = environ[CLIENT_KEY]
        g = client.get_greenlet()
        if not g:
            # new greenlet
            g = greenlet.greenlet(self.app)
            client.set_greenlet(g)
            c = Continuation(client)
            environ[CONTINUATION_KEY] = c

        result = self.setup(environ)
        response = None
        try:
            response = g.switch(environ, start_response)
            return response
        finally:
            if result and response != -1:
                ws = environ.pop('wsgi.websocket')
                ws._send_closing_frame(True)

class WebSocketWSGI(object):

    def __init__(self, handler):
        self.handler = handler
        self.protocol_version = None

    def __call__(self, environ, start_response):
        if not (environ.get('HTTP_CONNECTION') == 'Upgrade' and
                environ.get('HTTP_UPGRADE') == 'WebSocket'):
            # need to check a few more things here for true compliance
            start_response('400 Bad Request', [('Connection','close')])
            return [""]
    
        # See if they sent the new-format headers
        if 'HTTP_SEC_WEBSOCKET_KEY1' in environ:
            self.protocol_version = 76
            if 'HTTP_SEC_WEBSOCKET_KEY2' not in environ:
                # That's bad.
                start_response('400 Bad Request', [('Connection','close')])
                return [""]
        else:
            self.protocol_version = 75

        # Get the underlying socket and wrap a WebSocket class around it
        client = environ[CLIENT_KEY]
        sock = server._get_socket_fromfd(client.get_fd(), client)
        ws = WebSocket(sock, environ, self.protocol_version)
        
        # If it's new-version, we need to work out our challenge response
        if self.protocol_version == 76:
            key1 = self._extract_number(environ['HTTP_SEC_WEBSOCKET_KEY1'])
            key2 = self._extract_number(environ['HTTP_SEC_WEBSOCKET_KEY2'])
            # There's no content-length header in the request, but it has 8
            # bytes of data.
            key3 = environ['wsgi.input'].read(8)
            key = struct.pack(">II", key1, key2) + key3
            response = md5(key).digest()
        
        # Start building the response
        location = 'ws://%s%s%s' % (
            environ.get('HTTP_HOST'), 
            environ.get('SCRIPT_NAME'), 
            environ.get('PATH_INFO')
        )
        qs = environ.get('QUERY_STRING')
        if qs:
            location += '?' + qs
        if self.protocol_version == 75:
            handshake_reply = ("HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
                               "Upgrade: WebSocket\r\n"
                               "Connection: Upgrade\r\n"
                               "WebSocket-Origin: %s\r\n"
                               "WebSocket-Location: %s\r\n\r\n" % (
                    environ.get('HTTP_ORIGIN'),
                    location))
        elif self.protocol_version == 76:
            handshake_reply = ("HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
                               "Upgrade: WebSocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Origin: %s\r\n"
                               "Sec-WebSocket-Protocol: %s\r\n"
                               "Sec-WebSocket-Location: %s\r\n"
                               "\r\n%s"% (
                    environ.get('HTTP_ORIGIN'),
                    environ.get('HTTP_SEC_WEBSOCKET_PROTOCOL', 'default'),
                    location,
                    response))
        else: #pragma NO COVER
            raise ValueError("Unknown WebSocket protocol version.") 
        
        r = sock.sendall(handshake_reply)
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
        self._buf = ""
        self._msgs = collections.deque()
        #self._sendlock = semaphore.Semaphore()

    @staticmethod
    def _pack_message(message):
        """Pack the message inside ``00`` and ``FF``

        As per the dataframing section (5.3) for the websocket spec
        """
        if isinstance(message, unicode):
            message = message.encode('utf-8')
        elif not isinstance(message, str):
            message = str(message)
        packed = "\x00%s\xFF" % message
        return packed

    def _parse_messages(self):
        """ Parses for messages in the buffer *buf*.  It is assumed that
        the buffer contains the start character for a message, but that it
        may contain only part of the rest of the message.

        Returns an array of messages, and the buffer remainder that
        didn't contain any full messages."""
        msgs = []
        end_idx = 0
        buf = self._buf
        while buf:
            frame_type = ord(buf[0])
            if frame_type == 0:
                # Normal message.
                end_idx = buf.find("\xFF")
                if end_idx == -1: #pragma NO COVER
                    break
                msgs.append(buf[1:end_idx].decode('utf-8', 'replace'))
                buf = buf[end_idx+1:]
            elif frame_type == 255:
                # Closing handshake.
                assert ord(buf[1]) == 0, "Unexpected closing handshake: %r" % buf
                self.websocket_closed = True
                break
            else:
                raise ValueError("Don't understand how to parse this type of message: %r" % buf)
        self._buf = buf
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
            if delta == '':
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
            except IOErr:
                # Sometimes, like when the remote side cuts off the connection,
                # we don't care about this.
                if not ignore_send_errors: #pragma NO COVER
                    raise
            self.websocket_closed = True

    def close(self):
        """Forcibly close the websocket; generally it is preferable to
        return from the handler method."""
        self._send_closing_frame()
        #self.socket.shutdown(True)
        self.socket.close()

