[(
urldecoder: make object! [
    digit:       make bitset! "0123456789"
    digits:      [repeat ([1 5]) digit]  ; 1 to 5 digits
    alpha-num:   make bitset! [#"a" - #"z" #"A" - #"Z" #"0" - #"9"]
    scheme-char: insert copy alpha-num "+-."
    path-char:   complement make bitset! "#"
    user-char:   complement make bitset! ":@"
    host-char:   complement make bitset! ":/?"
    s1: s2: _ ; in R3, input datatype is preserved - these are now URL strings
    out: []
    emit: func ['w v] [
        append out reduce [
            to set-word! w (either :v [to text! :v] [_])
        ]
    ]

    rules: [
        ; Scheme://user-host-part
        [
            ; scheme name: [//]
            copy s1 some scheme-char ":" opt "//" ( ; "//" is optional ("URN")
                append out compose [
                    scheme: '(as word! s1)
                ]
            )

            ; optional user [:pass]
            opt [
                copy s1 some user-char
                opt [":" copy s2 to "@" (emit pass s2)]
                "@" (emit user s1)
            ]

            ; optional host [:port]
            opt [
                copy s1 while host-char
                opt [
                    ":" copy s2 digits (
                        append out compose [
                            port-id: (to integer! s2)
                        ]
                    )
                ] (
                    ; Note: This code has historically attempted to convert
                    ; the host name into a TUPLE!, and if it succeeded it
                    ; considers this to represent an IP address lookup vs.
                    ; a DNS lookup.  A basis for believing this will work can
                    ; come from RFC-1738:
                    ;
                    ; "The rightmost domain label will never start with a
                    ;  digit, though, which syntactically distinguishes all
                    ;  domain names from the IP addresses."
                    ;
                    ; This suggests that as long as a TUPLE! conversion will
                    ; never allow non-numeric characters it can work, though
                    ; giving a confusing response to looking up "1" to come
                    ; back and say "1.0.0 cannot be found", because that is
                    ; the result of `make tuple! "1"`.
                    ;
                    ; !!! This code was also broken in R3-Alpha, because the
                    ; captured content in PARSE of a URL! was a URL! and not
                    ; a STRING!, and so the attempt to convert `s1` to TUPLE!
                    ; would always fail.

                    if not empty? trim s1 [
                        use [tup] [
                            ;
                            ; !!! In R3-Alpha this TO conversion was wrapped
                            ; in a TRAP as it wasn't expected for non-numeric
                            ; tuples to work.  But now they do...most of the
                            ; time (to tuple "localhost" is a WORD! and can't
                            ; be a TUPLE!)  In the interests of preserving
                            ; the experiment, use LOAD and test to see if
                            ; it made a tuple with an integer as last value.
                            ;
                            tup: load as text! s1  ; was "textlike" URL!
                            if all [tuple? tup, integer? last tup] [
                                s1: tup
                            ]
                        ]
                        emit host s1
                    ]
                )
            ]
        ]

        ; optional path
        opt [copy s1 some path-char (emit path s1)]

        ; optional bookmark
        opt ["#" copy s1 to end (emit tag s1)]

        end
    ]

    decode: func ["Decode a URL according to rules of sys/*parse-url." url] [
        out: make block! 8
        parse url rules
        out
    ]
]
true
)]

[#2380 (
    url: urldecoder.decode http://example.com/get?q=ščř#kovtička
    did all [
        url.scheme == the 'http  ; Note: DECODE-URL returns BLOCK! with 'http
        url.host == "example.com"
        url.path == "/get?q=ščř"
        url.tag == "kovtička"
    ]
)(
    url: urldecoder.decode http://švéd:břéťa@example.com:8080/get?q=ščř#kovtička
    did all [
        url.scheme == the 'http
        url.user == "švéd"
        url.pass == "břéťa"
        url.host == "example.com"
        url.port-id == 8080
        url.path == "/get?q=ščř"
        url.tag == "kovtička"
    ]
)(
    url: urldecoder.decode http://host?query
    did all [
        url.scheme == the 'http
        url.host == "host"
        url.path == "?query"
    ]
)]
