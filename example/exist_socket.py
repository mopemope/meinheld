from meinheld import server
import socket

def hello_world(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    print environ
    return [res]

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", 8000))
s.listen(50)
fd = s.fileno()

# server.set_listen_socket(fd)
server.listen(socket_fd=fd)

server.run(hello_world)


