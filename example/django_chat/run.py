#!/usr/bin/python
from meinheld import patch; patch.patch_all()
from meinheld import server, middleware
import sys
import os
import traceback
from django.core.handlers.wsgi import WSGIHandler
from django.core.management import call_command
from django.core.signals import got_request_exception

sys.path.append('..')
os.environ['DJANGO_SETTINGS_MODULE'] = 'django_chat.settings'

def exception_printer(sender, **kwargs):
    traceback.print_exc()

got_request_exception.connect(exception_printer)

call_command('syncdb')
print 'Serving on 8088...'
server.listen(('0.0.0.0', 8088))
server.run(middleware.ContinuationMiddleware(WSGIHandler()))

