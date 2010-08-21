import os
from meinheld import server, middleware, websocket

participants = set()

@websocket.WebSocketWSGI
def handle(ws):
    participants.add(ws)
    try:
        while True:
            print "ws.wait()..."
            m = ws.wait()
            print "recv msg %s" % m
            if m is None:
                break
            for p in participants:
                print "send message %s" % m
                a = p.send(m)
                print "%s" % a
    finally:
        participants.remove(ws)

def websocket_handle(environ, start_response):
    ws = environ.get('wsgi.websocket')
    participants.add(ws)
    try:
        while True:
            print "ws.wait()..."
            m = ws.wait()
            print "recv msg %s" % m
            if m is None:
                break
            for p in participants:
                print "send message %s" % m
                a = p.send(m)
                print "%s" % a
    finally:
        participants.remove(ws)
    return [""]

def dispatch(environ, start_response):
    """Resolves to the web page or the websocket depending on the path."""
    if environ['PATH_INFO'] == '/chat':
        return websocket_handle(environ, start_response)
    else:
        print "/"
        start_response('200 OK', [('Content-Type', 'text/html'), ('Content-Length', '814')])
        ret = [open(os.path.join(
                     os.path.dirname(__file__), 
                     'websocket_chat.html')).read()]
        return ret
        
if __name__ == "__main__":
    server.listen(("0.0.0.0", 8000))
    server.run(middleware.WebSocketMiddleware(dispatch))



