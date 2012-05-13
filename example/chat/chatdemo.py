# from meinheld.patch import patch_werkzeug
# patch_werkzeug()

from flask import Flask, render_template, request, session, jsonify
import uuid
from meinheld import server, middleware


SECRET_KEY = 'development key'
DEBUG=True

app = Flask(__name__)
app.config.from_object(__name__)

def create_message(from_, body):
    data = {'id': str(uuid.uuid4()), 'from': from_, 'body': body}
    data['html'] = render_template('message.html', message=data)
    return data

cache = []
cache_size = 200
waiters = []

@app.route('/')
def index():
    return render_template('index.html', messages=cache)

@app.route('/a/message/updates', methods=['POST'])
def message_update():
    global cache, waiters
    c = request.environ.get(middleware.CONTINUATION_KEY, None)
    cursor = session.get('cursor')
    if not cache or cursor == cache[-1]['id']:
        waiters.append(c)
        try:
            c.suspend(60)
        except:
            waiters.remove(c)
            raise

    print("suspend->resume %s" % c)
    assert cursor != cache[-1]['id'], cursor
    try:
        for index, m in enumerate(cache):
            if m['id'] == cursor:
                return jsonify({'messages': cache[index+1:]})
        return jsonify({'messages': cache})
    finally:
        if cache:
            session['cursor'] = cache[-1]['id']
        else:
            session.pop('cursor', None)
    
    
@app.route('/a/message/new', methods=['POST'])
def message_new():
    global cache, cache_size, waiters
    name = request.environ.get('REMOTE_ADDR') or 'Anonymous'
    forwarded_for = request.environ.get('HTTP_X_FORWARDED_FOR')
    if forwarded_for and name == '127.0.0.1':
        name = forwarded_for
    msg = create_message(name, request.form['body'])
    cache.append(msg)
    if len(cache) > cache_size:
        cache = cache[-cache_size:]

    for c in waiters:
        c.resume()
        print("resume %s" % c)
    waiters = []
    return jsonify(msg)
    


if __name__ == "__main__":
    server.listen(("0.0.0.0", 8000))
    server.run(middleware.ContinuationMiddleware(app))
