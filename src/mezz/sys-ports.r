REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Port and Scheme Functions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Context: sys
    Note: {
        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!
    }
]

; !!! UPARSE is not available in SYS because it is higher level.  We hack it
; up so that when %uparse.reb runs it pokes itself into sys.uparse
;
uparse: ~sys-uparse-not-set-yet~

make-port*: function [
    "SYS: Called by system on MAKE of PORT! port from a scheme."

    spec [file! url! block! object! word! port!]
        "port specification"
][
    ; The first job is to identify the scheme specified:

    switch type of spec [
        file! [
            name: pick [dir file] dir? spec
            spec: make object! [ref: spec]
        ]
        url! [
            spec: decode-url spec
            name: spec.scheme
        ]
        block! [
            spec: make object! spec
            name: select spec 'scheme
        ]
        object! [
            name: get in spec 'scheme
        ]
        word! [
            name: spec
            spec: _
        ]
        port! [
            name: port.scheme.name
            spec: port.spec
        ]

        fail
    ]

    ; Get the scheme definition:
    all [
        word? name
        scheme: get try in system/schemes name
    ] else [
        cause-error 'access 'no-scheme name
    ]

    ; Create the port with the correct scheme spec.
    ;
    ; !!! This used to use MAKE OBJECT! so it got a copy, but now that we are
    ; doing some hacky inheritance manually from object-to-object, there needs
    ; to be a COPY made.
    ;
    port: make system.standard.port []
    port.spec: copy any [scheme.spec, system.standard.port-spec-head]

    ; !!! Override any of the fields in port.spec with fields in spec.
    ; This used to be done with plain object derivation, because spec was
    ; a BLOCK!.  But DECODE-URL now returns an object, and you can't make
    ; an derived object via an object at this time.  Do it manually.
    ;
    ; !!! COLLECT is not available here.  This is all very old stuff and was
    ; organized terribly.  :-(
    ;
    overloads: copy []
    for-each [key val] spec [
        if not any [bad-word? ^val, nothing? :val] [
            append overloads :[to set-word! key get 'val]  ; override
        ]
    ]
    append port.spec overloads

    port/spec/scheme: name
    port/scheme: scheme

    ; Defaults:
    port/actor: try get in scheme 'actor ; avoid evaluation
    port/awake: try any [
        get try in port/spec 'awake
        get 'scheme/awake
    ]
    port/spec/ref: default [spec]
    port/spec/title: default [scheme/title]
    port: to port! port

    ; Call the scheme-specific port init. Note that if the
    ; scheme has not yet been initialized, it can be done
    ; at this time.
    if in scheme 'init [scheme/init port]
    port
]

*parse-url: make object! [
    digit:       make bitset! "0123456789"
    digits:      [repeat ([1 5]) digit]  ; 1 to 5 digits
    alpha-num:   make bitset! [#"a" - #"z" #"A" - #"Z" #"0" - #"9"]
    scheme-char: insert copy alpha-num "+-."
    path-char:   complement make bitset! "#"
    user-char:   complement make bitset! ":@"
    host-char:   complement make bitset! ":/?"

    rules: [
        ; Required scheme name, but "//" is optional (without it is a "URN")
        ; https://en.wikipedia.org/wiki/Uniform_Resource_Name
        ;
        emit scheme: [as/ (word!) across some scheme-char] ":" opt "//"

        ; optional user [:pass] @
        [
            emit user: across some user-char
            emit pass: opt [":", across to "@"]
            "@"
            |
            emit user: (null)
            emit pass: (~no-user~)  ; is this better than NULL?
        ]

        ; optional host [:port]
        ;
        ; Note: Historically this code tried to detect if the host was like
        ; an IP address, and if so return it as a TUPLE! instead of a TEXT!.
        ; This is used to cue IP address lookup behavior vs. a DNS lookup.
        ; A basis for believing you can discern comes from RFC-1738:
        ;
        ;    "The rightmost domain label will never start with a
        ;     digit, though, which syntactically distinguishes all
        ;     domain names from the IP addresses."
        [
            emit host: [
                ; IP-address style, make a TUPLE!
                ;
                to/ (tuple!) across [
                    while [some digit "."], some digit
                    not host-char  ; don't match "1.2.3.4a" as IP address
                ]
                    |
                ; Ordinary "foo.bar.com" style, just give it back as TEXT!
                ;
                across while host-char
            ]
            emit port-id: opt [":", to/ (integer!) across digits]
            |
            emit host: (null)
            emit port-id: (~no-host~)  ; is this better than NULL?
        ]

        emit path: opt [across some path-char]  ; optional path

        emit tag: opt ["#", across to <end>]  ; optional bookmark ("tag")

        emit ref: <input>  ; it's always saved the original URL for reference
    ]

    ; !!! Historically DECODE-URL returned a BLOCK!, but an object seems
    ; better.  Also, it seems useful to have NULL versions of the fields
    ; even for objects that don't have them to make it easy to check if those
    ; fields are present.
    ;
    ; !!! Red takes a similar approach of returning an object with a fixed
    ; list of fields but breaks them down differently and uses different names.
    ; That should be reviewed.
    ;
    decode-url: func [  ; this function is bound in sys/*parse-url
        {Decode a URL according to rules of sys/*parse-url}
        return: [object!]
        url [url! text!]
    ][
        uparse url [gather rules] else [
            fail ["Could not decode URL to an object:" url]
        ]
    ]
]

decode-url: :*parse-url.decode-url  ; wrapped in context, expose main function

;-- Native Schemes -----------------------------------------------------------

make-scheme: function [
    {Make a scheme from a specification and add it to the system}

    def "Scheme specification"
        [block!]
    /with "Scheme name to use as base"
        [word!]
][
    with: either with [get in system/schemes with][system/standard/scheme]
    if not with [cause-error 'access 'no-scheme with]

    scheme: make with def
    if not scheme/name [cause-error 'access 'no-scheme-name scheme]

    ; If actor is block build a non-contextual actor object:
    if block? :scheme/actor [
        actor: make object! (length of scheme/actor) / 4
        for-each [name func* args body] scheme/actor [
            ; !!! Comment here said "Maybe PARSE is better here", though
            ; knowing would depend on understanding precisely what the goal
            ; is in only allowing FUNC vs. alternative function generators.
            assert [
                set-word? name
                func* = 'func
                block? args
                block? body
            ]
            append actor reduce [
                name (func args body) ; add action! to object! w/name
            ]
        ]
        scheme/actor: actor
    ]

    match [object! handle!] :scheme/actor else [
        fail ["Scheme actor" :scheme/name "can't be" type of :scheme/actor]
    ]

    append system/schemes reduce [scheme/name scheme]
]
