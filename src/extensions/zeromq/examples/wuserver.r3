REBOL [
    title: "Weather update server in REBOL 3"
    author: "Andreas Bolka"
    note: {
        Binds PUB socket to tcp://*:5556
        Publishes random weather updates
    }
]

;import %extload.r3
;import %zmqext.rx

import %helpers.r3

;; Prepare our context and publisher
ctx: zmq-init 1
publisher: zmq-socket ctx 'pub
zmq-bind publisher tcp://*:5556
comment [zmq-bind publisher ipc://weather.ipc]

;; Initialise random number generator
random/seed now/precise

forever [
    ;; Get values that will fool the boss
;    zipcode: random 100'000
    zipcode: 10000 + random 100
    temperature: (random 215) - 80 ;; in °F
    rel-humidity: (random 50) + 10

    ;; Send message to all subscribers
    s-send publisher spaced [zipcode temperature rel-humidity]
]

zmq-close publisher
zmq-term ctx
