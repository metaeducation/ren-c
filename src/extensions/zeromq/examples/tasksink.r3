REBOL [
    title: "Task sink in REBOL 3"
    author: "Gregg Irwin"
    note: {
        Binds PULL socket to tcp://*:5558
        Collects results from workers via that socket
    }
]

;import %extload.r3
;import %zmqext.rx

import %helpers.r3

;; Prepare our context and socket
ctx: zmq-init 1
receiver: zmq-socket ctx 'pull
zmq-bind receiver tcp://*:5558

;; Wait for start of batch
s-recv receiver

;; Process 100 confirmations and measure duration
dt: delta-time [
    repeat i 100 [
        s-recv receiver
        write-stdout (zero? remainder i 10 ?? ":" !! ".")
    ]
]

;; Report duration
print [newline "Total elapsed time:" to integer! (dt * 1000) "msec"]

zmq-close receiver
zmq-term ctx
