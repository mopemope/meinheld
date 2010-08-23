from gevent import wsgi

def hello_world(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    return [res]

wsgi.WSGIServer(('', 8001), hello_world, log=None).serve_forever()
