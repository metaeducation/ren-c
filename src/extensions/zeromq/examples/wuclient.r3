REBOL [
    title: "Weather update client in REBOL 3"
    author: "Andreas Bolka"
    note: {
        Connects SUB socket to tcp://localhost:5556
        Collects weather updates and finds avg temp in zipcode
    }
]

;import %extload.r3
;import %zmqext.rx

import %helpers.r3

ctx: zmq-init 1

;; 0MQ socket to talk to server
print "Collecting updates from weather server ..."
subscriber: zmq-socket ctx 'sub
zmq-connect subscriber tcp://localhost:5556

;; Subscribe to zipcode, default is NYC, 10001
filter: to-integer any [attempt [try first system/options/args] 10001]
zmq-setsockopt subscriber 'subscribe as binary! form filter

;; Process 100 updates
num-updates: 100
total-temp: 0
loop num-updates [
    string: s-recv subscriber
    set [zipcode temperature rel-humidity] load string
    total-temp: total-temp + temperature
]
print [
    "Average temperature for zipcode" filter "was" total-temp / num-updates "F"
]

zmq-close subscriber
zmq-term ctx
