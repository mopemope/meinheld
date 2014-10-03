import sys

from meinheld import server
from meinheld.middleware import WebSocketMiddleware

def application(env, start_response):
    if env.get('wsgi.websocket') is not None:
        ws = env.get('wsgi.websocket')
        while True:
            message = ws.wait()
            if message is None:
                return []
            ws.send(message)
    else:
        start_response('404 Not Found', [])
        return []

if __name__ == '__main__':
    port = int(sys.argv[1] or '8000')
    server.listen(('127.0.0.1', port))
    server.run(WebSocketMiddleware(application))

