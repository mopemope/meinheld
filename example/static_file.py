import meinheld

class FileWrapper(object):

    def __init__(self, file, buffer_size=8192):
        self.file = file
        self.buffer_size = buffer_size

    def close(self):
        if hasattr(self.file, 'close'):
            self.file.close()

    def __iter__(self):
        return self

    def next(self):
        data = self.file.read(self.buffer_size)
        if data:
            return data
        raise StopIteration()


def simple_app(environ, start_response):
    status = '200 OK'
    #response_headers = [('Content-type','image/jpeg')]
    response_headers = [('Content-type','image/jpeg'), ('Content-Length', '137823')]
    start_response(status, response_headers)
    # use sendfile(2)
    return environ.get('wsgi.file_wrapper', FileWrapper)(open('wallpaper.jpg', 'rb'))


meinheld.listen(("0.0.0.0", 8000))
#meinheld.access_log('/tmp/acc.log')
#meinheld.error_log('/tmp/err.log')
meinheld.run(simple_app)

