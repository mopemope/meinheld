from meinheld.patch import patch_werkzeug
patch_werkzeug()

from flask import Flask, render_template
import meinheld

app = Flask(__name__)

@app.route('/')
def index():
    return render_template('hello.html')


meinheld.listen(("0.0.0.0", 8000))
meinheld.run(app)
