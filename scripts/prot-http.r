REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 HTTP protocol scheme"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Name: http
    Type: module
    File: %prot-http.r
    Version: 0.1.48
    Purpose: {
        This program defines the HTTP protocol scheme for REBOL 3.
    }
    Author: ["Gabriele Santilli" "Richard Smolak"]
    Date: 26-Nov-2012
    History: [
        8-Oct-2015 {Modified by @GrahamChiu to return an error object with
        the info object when manual redirect required}
    ]
]

digit: charset [#"0" - #"9"]
alpha: charset [#"a" - #"z" #"A" - #"Z"]
idate-to-date: function [return: [date!] date [text!]] [
    parse date [
        5 skip
        copy day: 2 digit
        space
        copy month: 3 alpha
        space
        copy year: 4 digit
        space
        copy time: to space
        space
        copy zone: to end
    ] else [
        fail ["Invalid idate:" date]
    ]
    if zone = "GMT" [zone: copy "+0"]
    to date! unspaced [day "-" month "-" year "/" time zone]
]

sync-op: function [port body] [
    if not port/state [
        open port
        port/state/close?: yes
    ]

    state: port/state
    state/awake: :read-sync-awake

    do body

    if state/mode = 'ready [do-request port]

    ; Wait in a WHILE loop so the timeout cannot occur during 'reading-data
    ; state.  The timeout should be triggered only when the response from
    ; the other side exceeds the timeout value.
    ;
    while [not find [ready close] state/mode] [
        if not port? wait [state/connection port/spec/timeout] [
            fail make-http-error "Timeout"
        ]
        if state/mode = 'reading-data [
            read state/connection
        ]
    ]

    ; !!! Note that this dispatches to the "port actor", not the COPY generic
    ; action.  That has been overridden to copy PORT/DATA.  :-/
    ;
    body: copy port

    if state/close? [close port]

    either port/spec/debug [
        state/connection/locals
    ][
        body
    ]
]

read-sync-awake: function [return: [logic!] event [event!]] [
    switch event/type [
        'connect
        'ready [
            do-request event/port
            false
        ]
        'done [true]
        'close [true]
        'error [
            error: event/port/error
            event/port/error: _
            fail error
        ]
        default [false]
    ]
]

http-awake: function [return: [logic!] event [event!]] [
    port: event/port
    http-port: port/locals
    state: http-port/state
    if action? :http-port/awake [state/awake: :http-port/awake]
    awake: :state/awake

    switch event/type [
        'read [
            awake make event! [type: 'read port: http-port]
            check-response http-port
        ]
        'wrote [
            awake make event! [type: 'wrote port: http-port]
            state/mode: 'reading-headers
            read port
            false
        ]
        'lookup [
            open port
            false
        ]
        'connect [
            state/mode: 'ready
            awake make event! [type: 'connect port: http-port]
        ]
        'close [
            res: try switch state/mode [
                'ready [
                    awake make event! [type: 'close port: http-port]
                ]
                'doing-request 'reading-headers [
                    http-port/error: make-http-error "Server closed connection"
                    awake make event! [type: 'error port: http-port]
                ]
                'reading-data [
                    either any [
                        integer? state/info/headers/content-length
                        state/info/headers/transfer-encoding = "chunked"
                    ][
                        http-port/error: make-http-error "Server closed connection"
                        awake make event! [type: 'error port: http-port]
                    ] [
                        ; set mode to CLOSE so the WAIT loop in 'sync-op can
                        ; be interrupted
                        ;
                        state/mode: 'close
                        any [
                            awake make event! [type: 'done port: http-port]
                            awake make event! [type: 'close port: http-port]
                        ]
                    ]
                ]
            ]
            close http-port
            res
        ]
        default [true]
    ]
]

make-http-error: func [
    {Make an error for the HTTP protocol}

    message [text! block!]
][
    make error! compose [
        type: 'Access
        id: 'Protocol
        arg1: (unspaced message)  ; ERROR! has a `message` field, must COMPOSE
    ]
]

make-http-request: func [
    return: [binary!]
    method [word! text!] "E.g. GET, HEAD, POST etc."
    target [file! text!]
        {In case of text!, no escaping is performed.}
        {(eg. useful to override escaping etc.). Careful!}
    headers [block!] "Request headers (set-word! text! pairs)"
    content [text! binary! blank!]
        {Request contents (Content-Length is created automatically).}
        {Empty string not exactly like blank.}
    <local> result
][
    ; The HTTP 1.1 protocol requires a `Host:` header.  Simple logic used
    ; here is to fall back to requesting 1.0 only if there is no Host.
    ; (though apparently often speakers of the 1.0 protocol require it too)
    ;
    result: unspaced [
        uppercase form method _
            either file? target [next mold target] [target]
            _ either headers/host ["HTTP/1.1"] ["HTTP/1.0"] CR LF
    ]
    for-each [word string] headers [
        append result unspaced [mold word _ string CR LF]
    ]
    if content [
        content: as binary! content
        append result unspaced [
            "Content-Length:" _ length of content CR LF
        ]
    ]
    append result unspaced [CR LF]
    result: to binary! result  ; AS BINARY! would be UTF-8 constrained
    if content [append result content]  ; ...but content can be arbitrary
    result
]

do-request: function [
    {Queue an HTTP request to a port (response must be waited for)}

    return: <void>
    port [port!]
][
    spec: port/spec
    info: port/state/info
    spec/headers: body-of make make object! [
        Accept: "*/*"
        Accept-Charset: "utf-8"
        Host: if not find [80 443] spec/port-id [
            unspaced [spec/host ":" spec/port-id]
        ]
        else [
            form spec/host
        ]
        User-Agent: "REBOL"
    ] spec/headers
    port/state/mode: 'doing-request
    info/headers: info/response-line: info/response-parsed: port/data:
    info/size: info/date: info/name: blank
    write port/state/connection
    req: (make-http-request spec/method any [spec/path %/]
        spec/headers spec/content)

    net-log/C as text! req  ; Note: may contain CR (can't use TO TEXT!)
]

; if a no-redirect keyword is found in the write dialect after 'headers then
; 302 redirects will not be followed
;
parse-write-dialect: function [
    {Sets PORT/SPEC fields: DEBUG, FOLLOW, METHOD, PATH, HEADERS, CONTENT}

    return: <void>
    port [port!]
    block [block!]
][
    spec: port/spec
    parse block [
        opt ['headers (spec/debug: true)]
        opt ['no-redirect (spec/follow: 'ok)]
        [set temp: word! (spec/method: temp) | (spec/method: 'post)]
        opt [set temp: [file! | url!] (spec/path: temp)]
        [set temp: block! (spec/headers: temp) | (spec/headers: [])]
        [
            set temp: [any-string! | binary!] (spec/content: temp)
            | (spec/content: blank)
        ]
        end
    ]
]

check-response: function [port] [
    state: port/state
    conn: state/connection
    info: state/info
    headers: info/headers
    line: info/response-line
    awake: :state/awake
    spec: port/spec

    ; dump spec
    all [
        not headers
        any [
            all [
                d1: find conn/data crlfbin
                d2: find/tail d1 crlf2bin
                net-log/C "server standard content separator of #{0D0A0D0A}"
            ]
            all [
                d1: find conn/data #{0A}
                d2: find/tail d1 #{0A0A}
                net-log/C "server malformed line separator of #{0A0A}"
            ]
        ]
    ] then [
        info/response-line: line: to text! copy/part conn/data d1

        ; !!! In R3-Alpha, CONSTRUCT/WITH allowed passing in data that could
        ; be a STRING! or a BINARY! which would be interpreted as an HTTP/SMTP
        ; header.  The code that did it was in a function Scan_Net_Header(),
        ; that has been extracted into a completely separate native.  It
        ; should really be rewritten as user code with PARSE here.
        ;
        assert [binary? d1]
        d1: scan-net-header d1

        info/headers: headers: construct/with/only d1 http-response-headers
        info/name: to file! any [spec/path %/]
        if headers/content-length [
            info/size: (
                headers/content-length: to-integer headers/content-length
            )
        ]
        if headers/last-modified [
            info/date: try attempt [idate-to-date headers/last-modified]
        ]
        remove/part conn/data d2
        state/mode: 'reading-data
        if lit (txt) <> last body-of :net-log [ ; net-log is in active state
            print "Dumping Webserver headers and body"
            net-log/S info
            trap [
                body: to text! conn/data
                dump body
            ] then [
                print spaced [
                    "S:" length of conn/data "binary bytes in buffer ..."
                ]
            ]
        ]
    ]

    if not headers [
        read conn
        return false
    ]

    res: false

    info/response-parsed: default [
        catch [
            parse line [
                "HTTP/1." [#"0" | #"1"] some #" " [
                    #"1" (throw 'info)
                    |
                    #"2" [["04" | "05"] (throw 'no-content)
                        | (throw 'ok)
                    ]
                    |
                    #"3" [
                        (if spec/follow = 'ok [throw 'ok])

                        "02" (throw spec/follow)
                        |
                        "03" (throw 'see-other)
                        |
                        "04" (throw 'not-modified)
                        |
                        "05" (throw 'use-proxy)
                        | (throw 'redirect)
                    ]
                    |
                    #"4" [
                        "01" (throw 'unauthorized)
                        |
                        "07" (throw 'proxy-auth)
                        | (throw 'client-error)
                    ]
                    |
                    #"5" (throw 'server-error)
                ]
                | (throw 'version-not-supported)
            ]
            end
        ]
    ]

    if spec/debug = true [
        spec/debug: info
    ]

    switch/all info/response-parsed [
        'ok [
            if spec/method = 'HEAD [
                state/mode: 'ready
                res: any [
                    awake make event! [type: 'done port: port]
                    awake make event! [type: 'ready port: port]
                ]
            ] else [
                res: check-data port
                all [not res, state/mode = 'ready] then [
                    res: any [
                        awake make event! [type: 'done port: port]
                        awake make event! [type: 'ready port: port]
                    ]
                ]
            ]
        ]
        'redirect
        'see-other [
            if spec/method = 'HEAD [
                state/mode: 'ready
                res: awake make event! [type: 'custom port: port code: 0]
            ] else [
                res: check-data port
                if not open? port [
                    ;
                    ; !!! comment said: "some servers(e.g. yahoo.com) don't
                    ; supply content-data in the redirect header so the
                    ; state/mode can be left in 'reading-data after
                    ; check-data call.  I think it is better to check if port
                    ; has been closed here and set the state so redirect
                    ; sequence can happen."
                    ;
                    state/mode: 'ready
                ]
            ]
            all [not res, state/mode = 'ready] then [
                all [
                    find [get head] spec/method else [all [
                        info/response-parsed = 'see-other
                        spec/method: 'get
                    ]]
                    in headers 'Location
                ] also [
                    res: do-redirect port headers/location headers
                ] else [
                    port/error: make error! [
                        type: 'Access
                        id: 'Protocol
                        arg1: "Redirect requires manual intervention"
                        arg2: info
                    ]
                    res: awake make event! [type: 'error port: port]
                ]
            ]
        ]
        'unauthorized
        'client-error
        'server-error
        'proxy-auth [
            if spec/method = 'HEAD [
                state/mode: 'ready
            ] else [
                check-data port
            ]
        ]
        'unauthorized [
            port/error: make-http-error "Authentication not supported yet"
            res: awake make event! [type: 'error port: port]
        ]
        'client-error
        'server-error [
            port/error: make-http-error ["Server error: " line]
            res: awake make event! [type: 'error port: port]
        ]
        'not-modified [
            state/mode: 'ready
            res: any [
                awake make event! [type: 'done port: port]
                awake make event! [type: 'ready port: port]
            ]
        ]
        'use-proxy [
            state/mode: 'ready
            port/error: make-http-error "Proxies not supported yet"
            res: awake make event! [type: 'error port: port]
        ]
        'proxy-auth [
            port/error: (make-http-error
                "Authentication and proxies not supported yet")
            res: awake make event! [type: 'error port: port]
        ]
        'no-content [
            state/mode: 'ready
            res: any [
                awake make event! [type: 'done port: port]
                awake make event! [type: 'ready port: port]
            ]
        ]
        'info [
            info/headers: _
            info/response-line: _
            info/response-parsed: _
            port/data: _
            state/mode: 'reading-headers
            read conn
        ]
        'version-not-supported [
            port/error: make-http-error "HTTP response version not supported"
            res: awake make event! [type: 'error port: port]
            close port
        ]
    ]
    res
]
crlfbin: #{0D0A}
crlf2bin: #{0D0A0D0A}
crlf2: as text! crlf2bin
http-response-headers: context [
    Content-Length: _
    Transfer-Encoding: _
    Last-Modified: _
]

do-redirect: func [
    port [port!]
    new-uri [url! text! file!]
    headers
    <local> spec state
][
    spec: port/spec
    state: port/state
    if #"/" = first new-uri [
        new-uri: as url! unspaced [spec/scheme "://" spec/host new-uri]
    ]

    new-uri: decode-url new-uri
    if not find new-uri 'port-id [
        switch new-uri/scheme [
            'https [append new-uri [port-id: 443]]
            'http [append new-uri [port-id: 80]]
            fail ["Unknown scheme:" new-uri/scheme]
        ]
    ]

    new-uri: construct/with/only new-uri port/scheme/spec
    if not find [http https] new-uri/scheme [
        port/error: make-http-error
            {Redirect to a protocol different from HTTP or HTTPS not supported}
        return state/awake make event! [type: 'error port: port]
    ]

    all [
        new-uri/host = spec/host
        new-uri/port-id = spec/port-id
    ]
    then [
        spec/path: new-uri/path
        ;we need to reset tcp connection here before doing a redirect
        close port/state/connection
        open port/state/connection
        do-request port
        false
    ]
    else [
        port/error: make error! [
            type: 'Access
            id: 'Protocol
            arg1: "Redirect to other host - requires custom handling"
            arg2: headers
            arg3: as url! unspaced [
                new-uri/scheme "://" new-uri/host new-uri/path
            ]
        ]

        state/awake make event! [type: 'error port: port]
    ]
]

check-data: function [
    return: [logic! event!]
    port [port!]
][
    state: port/state
    headers: state/info/headers
    conn: state/connection

    res: false
    awaken-wait-loop: does [
        not res so res: true  ; prevent timeout when reading big data
    ]

    case [
        headers/transfer-encoding = "chunked" [
            data: conn/data
            port/data: default [  ; only clear at request start
                make binary! length of data
            ]
            out: port/data

            while [parse data [
                copy chunk-size some hex-digits thru crlfbin mk1: to end
            ]][
                ; The chunk size is in the byte stream as ASCII chars
                ; forming a hex string.  DEBASE to get a BINARY! and then
                ; DEBIN to get an integer.
                ;
                chunk-size: debin [be +] (debase/base as text! chunk-size 16)

                if chunk-size = 0 [
                    parse mk1 [
                        crlfbin (trailer: "") to end
                            |
                        copy trailer to crlf2bin to end
                    ] then [
                        trailer: construct/only trailer
                        append headers body-of trailer
                        state/mode: 'ready
                        res: state/awake make event! [
                            type: 'custom
                            port: port
                            code: 0
                        ]
                        clear data
                    ]
                    break
                ]
                else [
                    parse mk1 [chunk-size skip mk2: crlfbin to end] else [
                        break
                    ]

                    insert/part tail of out mk1 mk2
                    remove/part data skip mk2 2
                    empty? data
                ]
            ]

            if state/mode <> 'ready [
                awaken-wait-loop
            ]
        ]
        integer? headers/content-length [
            port/data: conn/data
            if headers/content-length <= length of port/data [
                state/mode: 'ready
                conn/data: make binary! 32000
                res: state/awake make event! [
                    type: 'custom
                    port: port
                    code: 0
                ]
            ] else [
                awaken-wait-loop
            ]
        ]
    ] else [
        port/data: conn/data
        if state/info/response-parsed = 'ok [
            awaken-wait-loop
        ] else [
            ; On other response than OK read all data asynchronously
            ; (assuming the data are small).
            ;
            read conn
        ]
    ]

    return res
]

hex-digits: charset "1234567890abcdefABCDEF"
sys/make-scheme [
    name: 'http
    title: "HyperText Transport Protocol v1.1"

    spec: make system/standard/port-spec-net [
        path: %/
        method: 'get
        headers: []
        content: _
        timeout: 15
        debug: _
        follow: 'redirect
    ]

    info: make system/standard/file-info [
        response-line:
        response-parsed:
        headers: _
    ]

    actor: [
        read: func [
            port [port!]
            /lines
            /string
            <local> data
        ][
            data: if action? :port/awake [
                if not open? port [
                    cause-error 'Access 'not-open port/spec/ref
                ]
                if port/state/mode <> 'ready [
                    fail make-http-error "Port not ready"
                ]
                port/state/awake: :port/awake
                do-request port
            ] else [
                sync-op port []
            ]
            if lines or 'string [
                ; !!! When READ is called on an http PORT! (directly or
                ; indirectly) it bounces its parameters to this routine.  To
                ; avoid making an error this tolerates the refinements but the
                ; actual work of breaking the buffer into lines is done in the
                ; generic code so it will apply to all ports.  The design
                ; from R3-Alpha for ports (and "actions" in general), was
                ; rather half-baked, so this should all be rethought.
            ]
            return data
        ]

        write: func [
            port [port!]
            value
        ][
            if not match [block! binary! text!] :value [
                value: form :value
            ]
            if not block? value [
                value: reduce [
                    [Content-Type:
                        "application/x-www-form-urlencoded; charset=utf-8"
                    ]
                    value
                ]
            ]
            if action? :port/awake [
                if not open? port [
                    cause-error 'Access 'not-open port/spec/ref
                ]
                if port/state/mode <> 'ready [
                    fail make-http-error "Port not ready"
                ]
                port/state/awake: :port/awake
                parse-write-dialect port value
                do-request port
                port
            ] else [
                sync-op port [parse-write-dialect port value]
            ]
        ]

        open: func [
            port [port!]
            <local> conn
        ][
            if port/state [return port]
            if not port/spec/host [
                fail make-http-error "Missing host address"
            ]
            port/state: make object! [
                ;
                ; !!! PORT!s in R3-Alpha were made to have a generic concept
                ; of "state", which is custom to each port.  Making matters
                ; confusing, the http port's "state" reused the name for an
                ; enumeration of what mode it was currently in.  To make the
                ; code easier to follow (for however long it remains relevant,
                ; which may not be long), Ren-C changed this to "mode".
                ;
                mode: 'inited

                ; Note: an `error` field which was specific to HTTP errors has
                ; been generalized to be located in the `port/error` field for
                ; every port using error events--hence not in this customized
                ; state object.

                connection: _
                close?: no
                info: make port/scheme/info [type: 'file]
                awake: ensure [action! blank!] :port/awake
            ]
            port/state/connection: conn: make port! compose [
                scheme: (
                    either port/spec/scheme = 'http [lit 'tcp][lit 'tls]
                )
                host: port/spec/host
                port-id: port/spec/port-id
                ref: join-all [tcp:// host ":" port-id]
            ]
            conn/awake: :http-awake
            conn/locals: port
            open conn
            port
        ]

        reflect: func [port [port!] property [word!]] [
            switch property [
                'open? [
                    did all [port/state, open? port/state/connection]
                ]

                'length [
                    if port/data [length of port/data] else [0]
                ]
            ]
        ]

        close: func [
            port [port!]
        ][
            if port/state [
                close port/state/connection
                port/state/connection/awake: _
                port/state: _
            ]
            port
        ]

        copy: func [
            {Overrides the ANY-OBJECT! COPY (that copies a PORT! itself)}
            ; !!! This R3-Alpha-ism seems like a questionable idea.  :-/
            port [port!]
        ][
            all [
                port/spec/method = 'HEAD
                port/state
            ]
            then [
                reduce bind [name size date] port/state/info
            ]
            else [
                copy port/data  ; may be BLANK!, returns null
            ]
        ]

        query: func [
            port [port!]
            <local> error state
        ][
            if state: port/state [
                either error? error: port/error [
                    port/error: _
                    error
                ][
                    state/info
                ]
            ]
        ]
    ]
]

sys/make-scheme/with [
    name: 'https
    title: "Secure HyperText Transport Protocol v1.1"
    spec: make spec [
        port-id: 443
    ]
] 'http
