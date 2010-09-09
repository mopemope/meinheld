from meinheld import server
from meinheld import patch
patch.patch_all()

import urllib2

def print_head(url):
    print 'Starting %s' % url
    data = urllib2.urlopen(url).read()
    print '%s: %s bytes: %r' % (url, len(data), data[:50])

def wsgi_app(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    print_head("http://google.com")
    return [res]

server.listen(("0.0.0.0", 8000))
server.run(wsgi_app)

