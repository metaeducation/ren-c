REBOL [
    title: "0MQ helper functions for examples"
    author: ["Gregg Irwin" "Andreas Bolka"]
    name: helpers
    type: module
    exports: [s-recv s-send]
]

s-recv: function [
    "Receive message from socket and convert the message data to a text!"
    return: [text! binary!]
    socket [handle!] "0MQ socket"
    /binary "Return the binary! (don't convert to text!)"
][
    msg: zmq-msg-alloc
    zmq-msg-init msg

    zmq-recv socket msg ;-- defaults to blocking

    ;; Copy binary data from message.
    data: zmq-msg-data msg

    zmq-msg-close msg
    zmq-msg-free msg

    either binary [data] [as text! data]
]

s-send: function [
    "Copy a text! (or binary!) into a 0MQ message and send it to socket"
    return: [<opt> word!] "EINTR, EAGAIN"
    socket [handle!] "0MQ socket"
    data [text! binary!]
][
    msg: zmq-msg-alloc

    ; !!! This could just be AS BINARY! data after UTF-8 Everywhere
    if not binary? data [data: as binary! copy data]

    zmq-msg-init-data msg data

    e: try zmq-send socket msg ;-- defaults to blocking

    zmq-msg-close msg
    zmq-msg-free msg

    opt match word! e ;-- assume they don't care about the size?
]
