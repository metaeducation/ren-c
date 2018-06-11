REBOL [
    title: "Task ventilator in REBOL 3"
    author: "Gregg Irwin"
    note: {
        Binds PUSH socket to tcp://*:5557
        Sends batch of tasks to workers via that socket
    }
]

;import %extload.r3
;import %zmqext.rx

import %helpers.r3

ctx: zmq-init 1

;; Socket to send messages on
sender: zmq-socket ctx 'push
zmq-bind sender tcp://*:5557

ask "Press Enter when the workers are ready."
print "Sending tasks to workers..."

;; The first message is "0" and signals start of batch
s-send sender "0"

;; Initialise random number generator
random/seed now/precise

;; Send 100 tasks
total-msec: 0 ;; Total expected cost in msecs
repeat i 100 [
    workload: random 100
    total-msec: total-msec + workload
    s-send sender form workload
]
print ["Total expected cost:" total-msec "msec"]
wait 1 ;; Give 0MQ time to deliver (should be no longer needed with 0MQ 2.1+)

zmq-close sender
zmq-term ctx
