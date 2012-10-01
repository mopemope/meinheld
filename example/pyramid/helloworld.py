from pyramid.config import Configurator
from pyramid.response import Response

def hello_world(request):
    return Response('Hello %(name)s!' % request.matchdict)

config = Configurator()
config.add_route('hello', '/hello/{name}')
config.add_view(hello_world, route_name='hello')
app = config.make_wsgi_app()
from meinheld import server
server.listen(("0.0.0.0", 8000))
server.set_access_logger(None)
server.set_error_logger(None)
server.run(app)

