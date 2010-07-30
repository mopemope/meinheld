# -*- coding: utf-8 -
#
# This file is part of gunicorn released under the MIT license. 
# See the NOTICE for more information.
#
# Example code from Eventlet sources
# 
# gunicorn worker 
# gunicorn --workers=2 --worker-class="meinheld.gworker.MeinheldWorker" gunicorn_test:app 
#

def app(environ, start_response):
    """Simplest possible application object"""
    #print environ
    data = 'Hello, World!\n' 
    status = '200 OK'
    response_headers = [
        ('Content-type','text/plain'),
        ('Content-Length', str(len(data)))
    ]
    start_response(status, response_headers)
    return [data]
