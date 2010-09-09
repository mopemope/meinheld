from meinheld import server, middleware, patch
patch.patch_all()

from pymongo import Connection

def insert():
    
    from pymongo import Connection
    connection = Connection()

    db = connection.test_database
    test_collection = db.test_collection
    
    for i in xrange(10):
        name = "long name %s" % i
        data = "A" * 1024 * 1024
        if test_collection.insert(dict(id=i, index=i, name=name, data=data)):
            pass
    
    for t in test_collection.find():
        pass


def wsgi_app(environ, start_response):
    status = '200 OK'
    res = "Hello world!"
    response_headers = [('Content-type','text/plain')]
    start_response(status, response_headers)
    insert()
    return [res]

server.listen(("0.0.0.0", 8000))
server.run(wsgi_app)

