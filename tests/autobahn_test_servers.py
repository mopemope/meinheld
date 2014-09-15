import sys

from meinheld import server
from meinheld.middleware import WebSocketMiddleware

def application(env, start_response):
    start_response('404 Not Found', [])
    return []

if __name__ == '__main__':
    port = int(sys.argv[1] or '8000')
    server.listen(('127.0.0.1', port))
    server.run(WebSocketMiddleware(application))

