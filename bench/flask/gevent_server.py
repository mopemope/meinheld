from flask import Flask, render_template
from gevent import wsgi

app = Flask(__name__)

@app.route('/')
def index():
    return render_template('hello.html')

wsgi.WSGIServer(('', 8001), app, log=None).serve_forever()

