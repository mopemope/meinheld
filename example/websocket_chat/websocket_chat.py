from flask import Flask, render_template, request
from meinheld import server, middleware

SECRET_KEY = 'development key'
DEBUG=True

app = Flask(__name__)
app.config.from_object(__name__)


participants = set()


@app.route('/')
def index():
    return render_template('websocket_chat.html')

@app.route('/chat')
def chat():
    ws = request.environ.get('wsgi.websocket')
    participants.add(ws)
    try:
        while True:
            print "ws.wait()..."
            m = ws.wait()
            print "recv msg %s" % m
            if m is None:
                break
            for p in participants:
                print "send message %s" % m
                p.send(m)
    finally:
        participants.remove(ws)
    return ""

        
if __name__ == "__main__":
    server.listen(("0.0.0.0", 8000))
    server.run(middleware.WebSocketMiddleware(app))



