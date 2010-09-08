What's this
---------------------------------

this is a python wsgi web server.

Thus this is yet an another asynchronous web server like fapws3, tornado.

And meinheld is a WSGI compliant web server.

Require
---------------------------------

meinheld requires **Python 2.x >= 2.5**. and **greenlet >= 0.3.1**.

sorry meinheld supported linux only.

Installation
---------------------------------

Install from pypi::

  $ easy_install -ZU meinheld

Install from source:: 

  $ python setup.py install

meinheld support gunicorn .

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

meinheld provide simple continuation API (based on greenlet).

to enable continuation, use ContinuationMiddleware. get Continuation from wsgi environ.

Continuation Object has couple method, suspend and resume.


example ::

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

meinheld support Websocket. use WebSocketMiddleware. 

example ::

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



Performance
------------------------------

meinheld is used high performance http_parser.

(see http://github.com/ry/http-parser)

and useing high performance event library picoev.

(see http://developer.cybozu.co.jp/kazuho/2009/08/picoev-a-tiny-e.html)

`simple benchmark result here`_

sendfile
===========================

meinheld use sendfile(2), over wgsi.file_wrapper.



.. _simple benchmark result here: http://gist.github.com/544674



