What's this
---------------------------------

this is a high performance python wsgi web server.

Thus this is yet an another asynchronous web server like gevent.

And meinheld is a WSGI compliant web server. (HTTP 1.1 supported)

You can also join us in `meinheld mailing list`_ and `#meinheld`_ on freenode_

Requirements
---------------------------------

meinheld requires **Python 2.x >= 2.5**. and **greenlet >= 0.3.1**.

meinheld supports Linux, FreeBSD, Mac OS X.

Installation
---------------------------------

Install from pypi::

  $ easy_install -ZU meinheld

Install from source:: 

  $ python setup.py install

meinheld supports gunicorn.

To install gunicorn::

  $ easy_install -ZU gunicorn


Basic Usage
---------------------------------

simple wsgi app::

    from meinheld import server

    def hello_world(environ, start_response):
        status = '200 OK'
        res = "Hello world!"
        response_headers = [('Content-type','text/plain'),('Content-Length',str(len(res)))]
        start_response(status, response_headers)
        return [res]

    server.listen(("0.0.0.0", 8000))
    server.run(hello_world)


with gunicorn. user worker class "egg:meinheld#gunicorn_worker" or "meinheld.gmeinheld.MeinheldWorker"::
    
    $ gunicorn --workers=2 --worker-class="egg:meinheld#gunicorn_worker" gunicorn_test:app

Continuation
---------------------------------

meinheld provides a simple continuation API (based on greenlet).

To enable continuations, use ContinuationMiddleware. get Continuation from wsgi environ.

Continuation objects have two very interesting methods, `suspend` and `resume`.

For example::

    from meinheld import server
    from meinheld import middleware

    def app(environ, start_response):
        ...
        
        #get Continuation
        c = environ.get(middleware.CONTINUATION_KEY, None)
        
        ...

        if condtion:
            waiters.append(c)
            #suspend 
            c.suspend()
        else:
            for c in waiters:
                # resume suspend function
                c.resume()

        ...


    server.listen(("0.0.0.0", 8000))
    server.run(middleware.ContinuationMiddleware(hello_world))

For more info see http://github.com/mopemope/meinheld/tree/master/example/chat/

Websocket 
---------------------------------

meinheld support Websockets. use WebSocketMiddleware. 

For example::

    from flask import Flask, render_template, request
    from meinheld import server, middleware

    SECRET_KEY = 'development key'
    DEBUG=True

    app = Flask(__name__)
    app.config.from_object(__name__)


    participants = set()


    @app.route('/')
    def index():
        return render_template('websocket_chat.html')

    @app.route('/chat')
    def chat():
        print request.environ
        ws = request.environ.get('wsgi.websocket')
        participants.add(ws)
        try:
            while True:
                print "ws.wait()..."
                m = ws.wait()
                print "recv msg %s" % m
                if m is None:
                    break
                for p in participants:
                    print "send message %s" % m
                    p.send(m)
        finally:
            participants.remove(ws)
        return ""

            
    if __name__ == "__main__":
        server.listen(("0.0.0.0", 8000))
        server.run(middleware.WebSocketMiddleware(app))


Patching 
---------------------------------

meinheld provides a few monkeypatches.

Socket 
==========================================

This patch replaces the standard socket module.

For Example::
    
    from meinheld import patch
    patch.patch_all()

For more info see http://github.com/mopemope/meinheld/tree/master/example/patch/


Werkzeug 
==========================================

This patch replaces Werkzeug's local get_ident function.

If you use Werkzeug, and you don't want to leak memory, you must call this patch function.

For example::
    
    from meinheld import patch
    patch.patch_werkzeug()

For more info see http://github.com/mopemope/meinheld/tree/master/example/flask_sample.py

Performance
------------------------------

For parsing HTTP requests, meinheld uses Ryan Dahl's http-parser library.

(see https://github.com/joyent/http-parser)

It is built around the high performance event library picoev.

(see http://developer.cybozu.co.jp/kazuho/2009/08/picoev-a-tiny-e.html)

`simple benchmark result here`_

sendfile
===========================

meinheld uses sendfile(2), over wgsi.file_wrapper.



.. _simple benchmark result here: http://gist.github.com/544674
.. _meinheld mailing list: http://groups.google.com/group/meinheld
.. _`#meinheld`: http://webchat.freenode.net/?channels=meinheld
.. _freenode: http://freenode.net



