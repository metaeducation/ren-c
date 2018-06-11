REBOL [
    title: "Hello World client in REBOL 3"
    author: "Andreas Bolka"
    note: {
        Connects REQ socket to tcp://localhost:5555
        Sends "Hello" to server, expects "World" back
    }
]

;import %extload.r3
;import %zmqext.rx

import %helpers.r3

ctx: zmq-init 1

;; Socket to talk to server.
print "Connecting to hello world server ..."
socket: zmq-socket ctx 'req
zmq-connect socket tcp://localhost:5555

repeat i 10 [
    print ["Sending Hello" i "..."]
    s-send socket "Hello"

    res: s-recv socket
    print ["Received World" i]
]

zmq-close socket
zmq-term ctx
