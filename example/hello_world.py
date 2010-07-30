from meinheld import server

def hello_world(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain'),('Content-Length',str(len(res)))]
    start_response(status, response_headers)
#    print environ
    return [res]

#meinheld.listen("/tmp/sock")
server.listen(("0.0.0.0", 8000))
#meinheld.access_log('/tmp/acc.log')
#meinheld.error_log('/tmp/err.log')
server.run(hello_world)

