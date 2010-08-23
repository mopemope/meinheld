import fapws._evwsgi as evwsgi
from fapws import base

def hello_world(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    return [res]

evwsgi.start("0.0.0.0", "8002")
evwsgi.set_base_module(base)
    
evwsgi.wsgi_cb(("/", hello_world))
evwsgi.set_debug(0)    
evwsgi.run()
