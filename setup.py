
try:
    from setuptools import Extension, setup
except ImportError:
    from distutils.core import Extension, setup

import os
import sys
import os.path
import platform

def read(name):
    return open(os.path.join(os.path.dirname(__file__), name)).read()

if "posix" not in os.name:
    print "Are you really running a posix compliant OS ?"
    print "Be posix compliant is mandatory"
    sys.exit(1)

if "Linux" != platform.system():
    print "sorry support linux only."
    sys.exit(1)

library_dirs=['/usr/local/lib']
include_dirs=[]


setup(name='meinheld',
    version="0.4.2dev",
    description="High performance asynchronous Python WSGI Web Server",
    long_description=read('README.rst'),
    author='yutaka matsubara',
    author_email='yutaka.matsubara@gmail.com',
    url='http://github.com/mopemope/meinheld',
    license='BSD',
    platforms='Linux',
    packages= ['meinheld'],
    install_requires=[
        'greenlet>=0.3.1',
    ],
    
    entry_points="""

    [gunicorn.workers]
    gunicorn_worker=meinheld.gmeinheld:MeinheldWorker
    """,
    ext_modules = [
        Extension('meinheld.server',
            sources=['meinheld/server/server.c', 'meinheld/server/picoev_epoll.c',
                'meinheld/server/http_parser.c','meinheld/server/http_request_parser.c',
                'meinheld/server/response.c', 'meinheld/server/time_cache.c', 'meinheld/server/log.c',
                'meinheld/server/buffer.c', 'meinheld/server/request.c',
                'meinheld/server/client.c', 'meinheld/server/util.c'],
                include_dirs=include_dirs,
                library_dirs=library_dirs,
                #libraries=["profiler"],
                #extra_compile_args=["-DDEBUG"],
            )],

    classifiers=[
        'Development Status :: 4 - Beta',
        'Environment :: Web Environment',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: BSD License',
        'Operating System :: POSIX :: Linux',
        'Programming Language :: C',
        'Programming Language :: Python',
        'Topic :: Internet :: WWW/HTTP :: WSGI :: Server'
    ],
)


