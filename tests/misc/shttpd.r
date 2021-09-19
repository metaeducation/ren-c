REBOL [title: "A tiny static HTTP server" author: 'abolka date: 2009-11-04]

code-map: make map! [200 "OK" 400 "Forbidden" 404 "Not Found"]
mime-map: make map! [
    "html" "text/html" "css" "text/css" "js" "application/javascript"
    "gif" "image/gif" "jpg" "image/jpeg" "png" "image/png"
    "r" "text/plain" "r3" "text/plain" "reb" "text/plain"
]
error-template: trim/auto {
    <html><head><title>$code $text</title></head><body><h1>$text</h1>
    <p>Requested URI: <code>$uri</code></p><hr><i>shttpd.r</i> on
    <a href="http://www.rebol.com/rebol3/">REBOL 3</a> $r3</body></html>
}

error-response: func [code uri <local> values] [
    values: [code (code) text (code-map/:code) uri (uri) r3 (system.version)]
    reduce [code "text/html" reword error-template compose values]
]

start-response: func [port res <local> code text type body] [
    set [code type body] res
    write port unspaced [
        "HTTP/1.0" _ code _ code-map/:code CR LF
        "Content-type:" _ type CR LF
        "Content-length:" _ (length of body) CR LF
        CR LF
    ]
    ; Manual chunking is only necessary because of several bugs in R3's
    ; networking stack (mainly cc#2098 & cc#2160; in some constellations also
    ; cc#2103). Once those are fixed, we should directly use R3's internal
    ; chunking instead: `write port body`.
    port.locals: copy body
]

send-chunk: func [port] [
    ; Trying to send data >32'000 bytes at once will trigger R3's internal
    ; chunking (which is buggy, see above). So we cannot use chunks >32'000
    ; for our manual chunking.
    either empty? port.locals [
        _
    ][
        write port take/part port.locals 32'000
    ]
]

handle-request: function [config req] [
    parse to-text req ["get " ["/ " | copy uri: to " "]]
    uri: default ["index.html"]
    print ["URI:" uri]
    parse uri [some [thru "."] copy ext to end (type: mime-map/:ext)]
    type: default ["application/octet-stream"]
    if not exists? file: config/root/:uri [return error-response 404 uri]
    if error? trap [data: read file] [return error-response 400 uri]
    reduce [200 type data]
]

awake-client: function [event] [
    port: event/port
    switch event/type [
        'read [
            either find port.data to-binary unspaced [CR LF CR LF] [
                res: handle-request port.locals.config port.data
                start-response port res
            ] [
                read port
            ]
        ]
        'wrote [if not send-chunk port [close port]]
        'close [close port]
    ]
]

awake-server: func [event <local> client] [
    if event/type = 'accept [
        client: first event/port
        client/awake: :awake-client
        read client
    ]
]

serve: func [web-port web-root <local> listen-port] [
    listen-port: open join tcp://: web-port
    listen-port.locals: make object! compose/deep [
        config: [root: (web-root)]
    ]
    listen-port.awake: :awake-server
    wait listen-port
]

serve 8080 system.options.path
; vim: set syn=rebol sw=4 ts=4:
