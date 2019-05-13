1.0.1
=======
(Bug fix release 2019-05-14)

* Fix: broken environment

1.0.0
=======
(Bug fix release 2019-05-13)

* Fix: support wsgi.input_terminated flag
* Fix: chunked request fails with 411 length required

0.6.1
=======
(Bug fix release 2016-11-02)

* Fix: decode PATH_INFO as latin1

0.6.0
=======
(Release 2016-10-19)

* Improve: Use EPOLLEXCLUSIVE, Improve multi process performance (Linux Kernel >= 4.5)
* Improve: Improve performance gunicorn worker

0.5.9
=======
(Bug fix Release, Release 2016-03-04)

* Fix: segfault when bad request

0.5.8
=======
(Bug fix Release, Release 2015-09-16)

* Fix: Read temporary file binary mode

0.5.7
=======
(Bug fix Release, Release 2014-12-17)

* Fix: Support Only Greenlet 0.4.5

0.5.6
=======
(Bug fix Release, Release 2014-03-26)

* Fix: Support Python 3.4

0.5.4
=======
(Bug fix Release, Release 2013-03-11)

* Fix: Invalid signal callback

0.5.3
=======
(New Future Release, Release 2012-12-30)

* Support new gunicorn future (multiple socket)

0.5.2
=======
(bug fix release, release 2012-10-17)

* Fix: some memory leak
* Fix: add sleep API

0.5.1
=======
(bug fix release, release 2012-10-11)

* Fix: ignore setsocket error
* Fix: request timeout not written access log
* Improve: support gunicorn logging


0.5
=======
(New Feature release. rerelease 2012-10-09)

* Support PEP3333
* Support custom access logger and errror logger
* Support Server Side Event
* Not support SSL socket patch


0.4.15
=======
(bug fix release, release 2012-6-10)

* Fix: Stop server silent

0.4.14
=======
(bug fix release, release 2012-6-6)

* Fix: Fix greenlet version 0.3.4
* Fix: graceful reload for gunicorn
* Improve: Enabled set existing socket to listen function. (use keyword args "socket_fd")

0.4.13
=======
(bug fix release, release 2011-4-19)

* Fix release GIL

0.4.12
=======
(bug fix release, release 2011-3-22)

* Fix Mac OS X build error(not use SO_ACCEPTFILTER)

0.4.11
=======
(bug fix release, release 2011-3-21)

* Support Mac OS X and FreeBSD

0.4.10
=======
(bug fix release, release 2011-2-7)

* Add werkzeug support patch

0.4.9
=======
(bug fix release, release 2011-2-3)

* Fix missing last-chunk's CRLF

0.4.8
=======
(bug fix release, release 2011-1-19)

* Fix missing last-chunk's CRLF

0.4.7
=======
(bug fix release, release 2010-10-23)

* Fix conver to wsgi input's cStringIO slow
* Add new StringIO
* Improve performance optimize(use new StringIO)

0.4.6
=======
(bug fix release, release 2010-10-20)

* Add get_ident. instead of werkzeug.local.get_ident
* Change read timeout value(30sec)
* Add client_body_buffer_size

0.4.5
=======
(bug fix release, release 2010-10-17)

* Fix don't set Transfer-Encoding when body length zero

0.4.4
=======
(bug fix release, release 2010-10-16)

* Fix write_bucket leak
* Fix sendfile bug
* HTTP 1.1 Support (keep-alive and piplining)

0.4.3
=======
(bug fix release, release 2010-10-08)

* Fix "PATH_INFO" is now decoded value
* Improve performance optimize (use object pool)

0.4.2
=======
(bug fix release, release 2010-09-23)

* Fix spell miss
* Add version info(meinheld.__version__)


0.4.1
=======
(bug fix release, release 2010-09-18)

* Improve performance optimize (re-use object)
* Add response header check(':' and status code range)
* Change some parameter(watchdog interval, timeout)
* Check socket with gevent's socket
* Fix FileWrapper bugs

0.4
=======
(New feature release. rerelease 2010-09-09)

* add io trampoline
* support greening socket
* monkeypatchi utility
* embed greenlet
* rename SpawnMiddleware -> ContinuationMiddleware


0.3.3
=======
(bug fix release, release 2010-09-06)

* change _get_socket_fromfd arg. (del client)
* add timeout parameter to Continuation suspend method
* detect closed socket (use SO_KEEPALIVE)
* fix leak of spawned method


0.3.2
=======
(bug fix release, release 2010-08-30)

* check max_content_length negative
* add set_backlog (default 8192)
* add set_picoev_max_fd (default 8192)
* support keep-alive timeout (use set_keepalive method, set timeout value)
* fix websocket closed bug
* enable --keep-alive and --worker-connections option
* various bug fixes


0.3.1
=======

* fix python2.5 build error


0.3
=======

* support keep-alive (use set_keepalive)
* support websocket(experimental)
* various bug fixes


0.2.1
=======

* use TCP_DEFER_ACCEPT
* update http parser
* change max header num and size
* fix finally call response close
* add gunicorn worker entry point
* various bug fixes

0.2
=======

* support greenlet continuation (use greenlet C/API. suspend and resume support)
* add client object to wsgi environ
