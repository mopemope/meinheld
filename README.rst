What's this ?
---------------------------------

.. image:: https://travis-ci.org/mopemope/meinheld.svg
    :target: https://travis-ci.org/mopemope/meinheld

This is a high performance and simple python wsgi web server.

And Meinheld is a WSGI compliant web server. (PEP333 and PEP3333 supported)

You can also join us in `meinheld mailing list`_.

Requirements
---------------------------------

Meinheld requires **Python 3.x >= 3.5** .

Meinheld supports Linux, FreeBSD, and macOS.

Installation
---------------------------------

Install from pypi::

  $ pip install -U meinheld

Install from source::

  $ python setup.py install

Meinheld also supports working as a gunicorn worker.

To install gunicorn::

  $ pip install -U gunicorn


Basic Usage
---------------------------------

simple wsgi app:

.. code:: python

    from meinheld import server

    def hello_world(environ, start_response):
        status = b'200 OK'
        res = b"Hello world!"
        response_headers = [('Content-type', 'text/plain'), ('Content-Length', str(len(res)))]
        start_response(status, response_headers)
        return [res]

    server.listen(("0.0.0.0", 8000))
    server.run(hello_world)


with gunicorn. user worker class "egg:meinheld#gunicorn_worker" or "meinheld.gmeinheld.MeinheldWorker"::

    $ gunicorn --workers=2 --worker-class="egg:meinheld#gunicorn_worker" gunicorn_test:app


Performance
------------------------------

For parsing HTTP requests, Meinheld uses Ryan Dahl's http-parser library.

(see https://github.com/joyent/http-parser)

It is built around the high performance event library picoev.

(see http://developer.cybozu.co.jp/kazuho/2009/08/picoev-a-tiny-e.html)

Sendfile
===========================

Meinheld uses sendfile(2), over wgsi.file_wrapper.

.. _meinheld mailing list: http://groups.google.com/group/meinheld
