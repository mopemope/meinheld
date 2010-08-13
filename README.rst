What's this
---------------------------------

this is a python wsgi web server.

Thus this is yet an another asynchronous web server like fapws3, tornado.

And meinheld is a WSGI compliant web server.

Require
---------------------------------

meinheld requires **Python 2.x >= 2.5**. **greenlet >= 0.3.1**.

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


with gunicorn. user worker class "meinheld.gmeinheld.MeinheldWorker"::
    
    $ gunicorn --workers=2 --worker-class="meinheld.gmeinheld.MeinheldWorker" gunicorn_test:app

Continuation
---------------------------------

meinheld provide simple continuation API (based on greenlet).

to enable continuation, use SpawnMiddleware. get Continuation from wsgi environ.

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
    server.run(middleware.SpawnMiddleware(hello_world))

For more info see http://github.com/mopemope/meinheld/tree/master/example/chat/

Performance
------------------------------

meinheld is used high performance http_parser.

(see http://github.com/ry/http-parser)

and useing high performance event library picoev.

(see http://developer.cybozu.co.jp/kazuho/2009/08/picoev-a-tiny-e.html)

simple benchmark 
================================

simple hello_world bench::

    def hello_world(environ, start_response):
        status = '200 OK'
        res = "Hello world!"
        response_headers = [('Content-type','text/plain'),('Content-Length',str(len(res)))]
        start_response(status, response_headers)
        return [res]

use apach bench::

  $ ab -c 100 -n 10000 http://127.0.0.1:8000/

spec

* CPU : Intel(R) Atom(TM) CPU N270   @ 1.60GHz 

* Memoy : 1G

* OS: Ubuntu 10.04

============== =====================
server         Requests per second
============== =====================
meinheld (0.1)  2927.62 [#/sec]
fapws3 (0.6)    1293.53 [#/sec] 
gevent (0.13)   1174.19 [#/sec]
============== =====================

sendfile
===========================

meinheld use sendfile(2), over wgsi.file_wrapper.




