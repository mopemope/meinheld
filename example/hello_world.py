from meinheld import server

def hello_world(environ, start_response):
    status = '200 OK'
    res = b"Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    print(environ)
    return [res]

server.listen(("0.0.0.0", 8000))
# server.access_log('stdout')
#server.error_log('/tmp/err.log')
server.run(hello_world)


