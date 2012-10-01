from werkzeug import BaseRequest, BaseResponse, run_simple, wrap_file
import meinheld

def view_file(req):
    if not 'uploaded_file' in req.files:
        return BaseResponse('no file uploaded')
    f = req.files['uploaded_file']
    return BaseResponse(wrap_file(req.environ, f), mimetype=f.content_type,
                        direct_passthrough=True)


def upload_file(req):
    return BaseResponse('''
    <h1>Upload File</h1>
    <form action="" method="post" enctype="multipart/form-data">
        <input type="file" name="uploaded_file">
        <input type="submit" value="Upload">
    </form>
    ''', mimetype='text/html')


def application(environ, start_response):
    req = BaseRequest(environ)
    if req.method == 'POST':
        resp = view_file(req)
    else:
        resp = upload_file(req)
    return resp(environ, start_response)


meinheld.set_max_content_length(1024 * 1024 * 1024)
meinheld.listen(("0.0.0.0", 8000))
meinheld.run(application)

