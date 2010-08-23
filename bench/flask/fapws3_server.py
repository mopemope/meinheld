from flask import Flask, render_template
import fapws._evwsgi as evwsgi
from fapws import base

app = Flask(__name__)

@app.route('/')
def index():
    return render_template('hello.html')

evwsgi.start("0.0.0.0", "8002")
evwsgi.set_base_module(base)
    
evwsgi.wsgi_cb(("/", app))
evwsgi.set_debug(0)    
evwsgi.run()
