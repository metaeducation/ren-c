REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 HTTP protocol scheme"
    Rights: {
        Copyright 2012 Gabriele Santilli, Richard Smolak, and REBOL Technologies
        Copyright 2012-2021 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Type: module
    Name: HTTP-Protocol
    File: %prot-http.r
    Description: {
        This file defines a "Port Scheme" for reading and writing data via
        the HTTP protocol.  The protocol is built on top of a Generic
        "connection" which can be plain TCP or be layered with the transport
        layer security scheme (TLS), in which case the HTTP acts as HTTPS.

        The original code circa 2012 in the open source release of R3-Alpha
        attempted to be asynchronous, and had some rather convoluted logic
        pertaining to events and and wakeups.  Due to various weaknesses of
        that model it never enabled any interesting asynchronous scenarios,
        and was really just an extremely-difficult-to-debug way of writing
        synchronous reads and writes.  In 2021 it is being simplified:

        https://forum.rebol.info/t/1733

        So the goal is to stylize the protocol code synchronously, with the
        idea of doing something similar to Go's "goroutines" in order to
        achieve parallelism.  But for now the limitation is that the reads and
        writes are synchronous, with the benefit of increasing the clarity
        of the code.
    }
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
    return to date! unspaced [day "-" month "-" year "/" time zone]
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
            _ "HTTP/" (find headers [Host:] then ["1.1"] else ["1.0"]) CR LF
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
    return result
]

do-request: function [
    {Synchronously process an HTTP request on a port}

    return: "Result of the request (BLOCK! for HEAD requests, BINARY! read...)"
        [binary! block!]
    port [port!]
][
    spec: port.spec
    info: port.state.info
    spec.headers: body-of make make object! [
        Accept: "*/*"
        Accept-Charset: "utf-8"
        Host: if not find [80 443] spec.port-id [
            unspaced [spec.host ":" spec.port-id]
        ]
        else [
            form spec.host
        ]
        User-Agent: "REBOL"
    ] spec.headers

    port.state.mode: <doing-request>

    info.headers: info.response-line: info.response-parsed: port.data:
    info.size: info.date: info.name: blank
    req: (make-http-request spec.method any [spec.path %/]
        spec.headers spec.content)

    write port.state.connection req
    port.state.mode: <reading-headers>

    read port.state.connection  ; read some data from the TCP port
    until [
        check-response port  ; see if it was enough, if not it asks for more
        did any [
            port.state.mode = <ready>
            port.state.mode = <close>
        ]
    ]

    net-log/C as text! req  ; Note: may contain CR (can't use TO TEXT!)

    if port.state and (port.spec.method = 'HEAD) [
        return reduce in port.state.info [name size date]
    ]

    return copy port.data  ; data may be BLANK!, if so will return null
]

; if a no-redirect keyword is found in the write dialect after 'headers then
; 302 redirects will not be followed
;
parse-write-dialect: function [
    {Sets PORT.SPEC fields: DEBUG, FOLLOW, METHOD, PATH, HEADERS, CONTENT}

    return: <none>
    port [port!]
    block [block!]
][
    spec: port.spec
    parse block [
        opt ['headers (spec.debug: true)]
        opt ['no-redirect (spec.follow: 'ok)]
        [set temp: word! (spec.method: temp) | (spec.method: 'post)]
        opt [set temp: [file! | url!] (spec.path: temp)]
        [set temp: block! (spec.headers: temp) | (spec.headers: [])]
        [
            set temp: [any-string! | binary!] (spec.content: temp)
            | (spec.content: blank)
        ]
        end
    ]
]

check-response: function [
    return: <none>
    port [port!]
][
    state: port.state
    conn: state.connection
    info: state.info
    headers: info.headers
    line: info.response-line
    spec: port.spec

    loop [state.mode = <reading-headers>] [
        any [
            all [
                d1: find conn.data crlfbin
                d2: find/tail d1 crlf2bin
                net-log/C "server standard content separator of #{0D0A0D0A}"
            ]
            all [
                d1: find conn.data #{0A}
                d2: find/tail d1 #{0A0A}
                net-log/C "server malformed line separator of #{0A0A}"
            ]
        ] else [
            read conn
            continue
        ]

        info.response-line: line: to text! copy/part conn.data d1

        ; !!! In R3-Alpha, CONSTRUCT/WITH allowed passing in data that could
        ; be a STRING! or a BINARY! which would be interpreted as an HTTP/SMTP
        ; header.  The code that did it was in a function Scan_Net_Header(),
        ; that has been extracted into a completely separate native.  It
        ; should really be rewritten as user code with PARSE here.
        ;
        assert [binary? d1]
        d1: scan-net-header d1

        info.headers: headers: construct/with/only d1 http-response-headers
        info.name: to file! any [spec.path %/]
        if headers.content-length [
            info.size: (
                headers.content-length: to-integer headers.content-length
            )
        ]
        if headers.last-modified [
            info.date: try attempt [idate-to-date headers.last-modified]
        ]
        remove/part conn.data d2
        state.mode: <reading-data>
    ]

    info.response-parsed: default [
        ;
        ; We use a RETURN rule to end the parse abruptly after matching only
        ; the initial part to derive a value.
        ;
        uparse line [return [
            "HTTP/1." ["0" | "1"] some space [
                "100" ('continue)
                |
                "2" [
                    ["04" | "05"] ('no-content)
                    |
                    ('ok)
                ]
                |
                "3" [
                    :(spec.follow = 'ok) ('ok)
                    |
                    "02" (spec.follow)
                    |
                    "03" ('see-other)
                    |
                    "04" ('not-modified)
                    |
                    "05" ('use-proxy)
                    |
                    ('redirect)
                ]
                |
                "4" [
                    "01" ('unauthorized)
                    |
                    "07" ('proxy-auth)
                    |
                    ('client-error)
                ]
                |
                "5" ('server-error)
            ]
            | ('version-not-supported)
        ]]
    ]

    if spec.debug = true [
        spec.debug: info
    ]

    switch/all info.response-parsed [
        ;
        ; "The client will expect to receive a 100-Continue response from the
        ; server to indicate that the client should send the data to be posted.
        ; This mechanism allows clients to avoid sending large amounts of data
        ; over the network when the server, based on the request headers,
        ; intends to reject the request."
        ;
        'continue [
            info.headers: _
            info.response-line: _
            info.response-parsed: _
            port.data: _
            state.mode: <reading-headers>
            read conn
        ]

        'ok [
            if spec.method = 'HEAD [
                state.mode: <ready>
            ] else [
                check-data port
            ]
        ]

        'redirect
        'see-other [
            if spec.method = 'HEAD [
                state.mode: <ready>
            ] else [
                check-data port
                if not open? port [
                    ;
                    ; !!! comment said: "some servers(e.g. yahoo.com) don't
                    ; supply content-data in the redirect header so the
                    ; state.mode can be left in <reading-data> after
                    ; check-data call.  I think it is better to check if port
                    ; has been closed here and set the state so redirect
                    ; sequence can happen."
                    ;
                    state.mode: <ready>
                ]
            ]
            if state.mode = <ready> [
                all [
                    find [get head] ^spec.method else [all [
                        info.response-parsed = 'see-other
                        spec.method: 'get
                    ]]
                    in headers 'Location
                ] also [
                    do-redirect port headers.location headers
                ] else [
                    fail make error! [
                        type: 'Access
                        id: 'Protocol
                        arg1: "Redirect requires manual intervention"
                        arg2: info
                    ]
                ]
            ]
        ]
        'unauthorized
        'client-error
        'server-error
        'proxy-auth [
            if spec.method = 'HEAD [
                state.mode: <ready>
            ] else [
                check-data port
            ]
        ]
        'unauthorized [
            fail make-http-error "Authentication not supported yet"
        ]
        'client-error
        'server-error [
            fail make-http-error ["Server error: " line]
        ]
        'not-modified [
            state.mode: <ready>
        ]
        'use-proxy [
            fail make-http-error "Proxies not supported yet"
        ]
        'proxy-auth [
            fail (make-http-error
                "Authentication and proxies not supported yet")
        ]
        'no-content [
            state.mode: <ready>
        ]
        'version-not-supported [
            fail make-http-error "HTTP response version not supported"
        ]
    ]
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
    spec: port.spec
    state: port.state
    if #"/" = first new-uri [
        new-uri: as url! unspaced [spec.scheme "://" spec.host new-uri]
    ]

    new-uri: decode-url new-uri
    new-uri.port-id: default [
        switch new-uri.scheme [
            'https [443]
            'http [80]
            fail ["Unknown scheme:" new-uri.scheme]
        ]
    ]

    if not find [http https] new-uri.scheme [  ; !!! scheme is quoted
        fail make-http-error
            {Redirect to a protocol different from HTTP or HTTPS not supported}
    ]

    all [
        new-uri.host = spec.host
        new-uri.port-id = spec.port-id
    ]
    then [
        spec.path: new-uri.path

        ; We need to reset tcp connection here before doing a redirect
        ;
        close port.state.connection
        open port.state.connection
        do-request port
        false
    ]
    else [
        fail make error! [
            type: 'Access
            id: 'Protocol
            arg1: "Redirect to other host - requires custom handling"
            arg2: headers
            arg3: as url! unspaced [
                new-uri.scheme "://" new-uri.host new-uri.path
            ]
        ]
    ]
]

check-data: function [
    return: <none>
    port [port!]
][
    state: port.state
    headers: state.info.headers
    conn: state.connection

    case [
        headers.transfer-encoding = "chunked" [
            data: conn.data
            port.data: default [  ; only clear at request start
                make binary! length of data
            ]
            out: port.data

            loop [parse? data [
                copy chunk-size: some hex-digits, thru crlfbin
                mk1: here, to end
            ]][
                ; The chunk size is in the byte stream as ASCII chars
                ; forming a hex string.  DEBASE to get a BINARY! and then
                ; DEBIN to get an integer.
                ;
                ; It's not guaranteed that the chunk size is an even number
                ; of hex digits!  If it's not, insert a 0, since DEBASE 16
                ; would reject it otherwise.
                ;
                if odd? length of chunk-size [
                    insert chunk-size #0
                ]
                chunk-size: debin [be +] (debase/base as text! chunk-size 16)

                if chunk-size = 0 [
                    parse mk1 [
                        crlfbin (trailer: "") to end
                            |
                        copy trailer to crlf2bin to end
                    ] then [
                        trailer: scan-net-header as binary! trailer
                        append headers trailer
                        state.mode: <ready>
                        clear data
                    ]
                    break
                ]
                else [
                    parse mk1 [
                        repeat (chunk-size) skip, mk2: here, crlfbin, to end
                    ] else [
                        read conn  ; didn't get the whole chunk read more
                        break
                    ]

                    append/part out mk1 ((index of mk2) - (index of mk1))
                    remove/part data skip mk2 2
                    empty? data
                ]
            ]
        ]
        integer? headers.content-length [
            port.data: conn.data
            if headers.content-length <= length of port.data [
                state.mode: <ready>
                conn.data: make binary! 32000
            ] else [
                read conn
            ]
        ]
    ] else [
        port.data: conn.data
        if state.info.response-parsed <> 'ok [
            ;
            ; "On other response than OK read all data asynchronously"
            ; (Comment also said "assuming the data are small")
            ;
            read conn
        ]
    ]
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

    info: make system.standard.file-info [
        response-line:
        response-parsed:
        headers: _
    ]

    actor: [
        read: func [
            return: [binary!]
            port [port!]
            /lines
            /string
            <local> data
        ][
            let close?: false
            if port.state [
                if not open? port [
                    cause-error 'Access 'not-open port.spec.ref
                ]
                if port.state.mode <> <ready> [
                    fail make-http-error "Port not ready"
                ]
            ] else [
                open port
                close?: true
            ]

            data: do-request port
            assert [find [<ready> <close>] port.state.mode]

            if close? [
                close port
            ]

            if lines or (string) [
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
            <local> data
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
            let close?: false
            if port.state [
                if not open? port [
                    cause-error 'Access 'not-open port.spec.ref
                ]
                if port.state.mode <> <ready> [
                    fail make-http-error "Port not ready"
                ]
            ] else [
                open port
                close?: true
            ]

            parse-write-dialect port value
            data: do-request port
            assert [find [<ready> <close>] port.state.mode]

            if close? [
                close port
            ]

            return data
        ]

        open: func [
            port [port!]
            <local> conn
        ][
            if port.state [return port]
            if not port.spec.host [
                fail make-http-error "Missing host address"
            ]
            port.state: make object! [
                mode: ~inited~  ; original http confusingly called this "state"
                connection: ~
                info: make port.scheme.info [type: 'file]
            ]
            port.state.connection: conn: make port! compose [
                scheme: (
                    either port.spec.scheme = 'http [the 'tcp][the 'tls]
                )
                host: port.spec.host
                port-id: port.spec.port-id
                ref: join tcp:// reduce [host ":" port-id]
            ]
            conn.locals: port
            open conn
            connect conn
            port.state.mode: <ready>

            port
        ]

        reflect: func [port [port!] property [word!]] [
            switch property [
                'open? [
                    did all [port.state, open? port.state.connection]
                ]

                'length [
                    if port.data [length of port.data] else [0]
                ]
            ]
        ]

        close: func [
            port [port!]
        ][
            let state: port.state
            if not state [return port]

            switch state.mode [
                <ready> [
                    ; closing in okay when ready
                ]
                <doing-request> <reading-headers> [
                    fail make-http-error "Server closed connection"
                ]
                <reading-data> [
                    any [
                        integer? state.info.headers.content-length
                        state.info.headers.transfer-encoding = "chunked"
                    ] then [
                        fail make-http-error "Server closed connection"
                    ]
                    state.mode: <close>
                ]
            ]

            close state.connection
            port.state: _
            port
        ]

        query: func [
            port [port!]
            <local> error state
        ][
            if state: port.state [
                state.info
            ]
        ]
    ]
]

sys.make-scheme.with [
    name: 'https
    title: "Secure HyperText Transport Protocol v1.1"
    spec: make spec [
        port-id: 443
    ]
] 'http
