import bjoern

def hello_world(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    return [res]


bjoern.run(hello_world, '0.0.0.0', 8080)
