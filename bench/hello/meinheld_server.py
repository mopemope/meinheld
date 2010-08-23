from meinheld import server

def hello_world(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    return [res]

server.listen(("0.0.0.0", 8000))
server.run(hello_world)

