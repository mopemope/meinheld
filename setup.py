
try:
    from setuptools import Extension, setup
except ImportError:
    from distutils.core import Extension, setup

import os
import sys
import os.path
import platform
import fnmatch

develop = False
if os.environ.get("MEINHELD_DEVELOP") == "1":
    develop = True
# develop = True

nogreen = False
if os.environ.get("MEINHELD_NOGREEN") == "1":
    nogreen = True


def read(name):
    return open(os.path.join(os.path.dirname(__file__), name)).read()

def check_platform():
    if "posix" not in os.name:
        print("Are you really running a posix compliant OS ?")
        print("Be posix compliant is mandatory")
        sys.exit(1)

def check_pypy():
    return "PyPy" in sys.version

def get_picoev_file():
    poller_file = None

    if "Linux" == platform.system():
        poller_file = 'meinheld/server/picoev_epoll.c'
    elif "Darwin" == platform.system():
        poller_file = 'meinheld/server/picoev_kqueue.c'
    elif "FreeBSD" == platform.system():
        poller_file = 'meinheld/server/picoev_kqueue.c'
    elif "NetBSD" == platform.system():
        poller_file = 'meinheld/server/picoev_kqueue.c'
    elif "SunOS" == platform.system():
        poller_file = 'meinheld/server/picoev_select.c'
    else:
        print("Sorry, not support .")
        sys.exit(1)
    return poller_file

def get_sources(path, ignore_files):
    src = []
    for root, dirs , files in os.walk(path):
        for file in files:
            src_path = os.path.join(root, file)
            #ignore = reduce(lambda x, y: x or y, [fnmatch.fnmatch(src_path, i) for i in ignore_files])
            ignore = [i for i in ignore_files if  fnmatch.fnmatch(src_path, i)]
            if not ignore and src_path.endswith(".c"):
                src.append(src_path)
    return src

check_platform()
pypy = check_pypy()
if pypy:
    nogreen = True

if nogreen:
    define_macros=[
            ("HTTP_PARSER_DEBUG", "0") ]
    install_requires=[]
else:
    define_macros=[
            ("WITH_GREENLET",None),
            ("HTTP_PARSER_DEBUG", "0") ]
    install_requires=['greenlet>=0.4.5,<0.5']

if develop:
    define_macros.append(("DEVELOP",None))

sources = get_sources("meinheld", ["*picoev_*"])
sources.append(get_picoev_file())

library_dirs=[]
#TODO set python include dirs
include_dirs=[]

setup(name='meinheld',
    version="1.0.1",
    description="High performance asynchronous Python WSGI Web Server",
    long_description=read('README.rst'),
    author='yutaka matsubara',
    author_email='yutaka.matsubara@gmail.com',
    url='http://meinheld.org',
    license='BSD',
    platforms='Linux, BSD, Darwin, SunOS',
    packages= ['meinheld'],
    install_requires=install_requires,

    entry_points="""

    [gunicorn.workers]
    gunicorn_worker=meinheld.gmeinheld:MeinheldWorker
    """,
    ext_modules = [
        Extension('meinheld.server',
            sources=sources,
            include_dirs=include_dirs,
            library_dirs=library_dirs,
            # libraries=["profiler"],
            # extra_compile_args=[""],
            define_macros=define_macros
        )],

    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Environment :: Web Environment',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: BSD License',
        'Operating System :: POSIX :: Linux',
        'Operating System :: MacOS :: MacOS X',
        'Operating System :: POSIX :: BSD :: FreeBSD',
        'Programming Language :: C',
        'Programming Language :: Python',
        'Programming Language :: Python :: 3',
        'Topic :: Internet :: WWW/HTTP :: WSGI :: Server'
    ],
)
