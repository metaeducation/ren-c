REBOL [
    Title: "FFI Extension"
    Name: FFI
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Notes: {
        The FFI was not initially implemented with any usermode code.  But
        just as with the routines in the SYS context, there's opportunity for
        replacing some of the non-performance-critical C that does parsing and
        processing into Rebol.  This is especially true since FFI was changed
        to use fewer specialized structures to represent ROUTINE! and
        STRUCT!, instead using arrays...to permit it to be factored into an
        extension.
    }
]

; !!! Should call UNREGISTER-STRUCT-HOOKS at some point (module finalizer?)
;
register-struct-hooks [
    change: generic [
        return: [struct!]
        series [struct!]
        value [<opt> any-value!]
    ]
]

ffi-type-mappings: [
    void [<opt>]

    uint8 [integer!]
    int8 [integer!]
    uint16 [integer!]
    int16 [integer!]
    uint32 [integer!]
    int32 [integer!]
    uint64 [integer!]

    float [decimal!]
    double [decimal!]

    ; Note: ACTION! is only legal to pass to pointer arguments if it is was
    ; created with MAKE-ROUTINE or WRAP-CALLBACK
    ;
    pointer [integer! text! binary! vector! action!]

    rebval [any-value!]

    ; ...struct...
]


make-callback: function [
    {Helper for WRAP-CALLBACK that auto-generates the action to be wrapped}

    return: [action!]
    args [block!]
    body [block!]
    /fallback "If untrapped failure occurs during callback, return value"
        [any-value!]
][
    r-args: copy []

    ; !!! TBD: Use type mappings to mark up the types of the Rebol arguments,
    ; so that HELP will show useful types.
    ;
    arg-rule: [
        copy a word! (append r-args a)
        block!
        opt text!
    ]

    ; !!! TBD: Should check fallback value for compatibility here, e.g.
    ; make sure [return: [pointer]] has a fallback value that's an INTEGER!.
    ; Because if an improper type is given as the reaction to an error, that
    ; just creates *another* error...so you'll still get a panic() anyway.
    ; Better to just FAIL during the MAKE-CALLBACK call so the interpreter
    ; does not crash.
    ;
    attr-rule: [
        set-word! block!
            |
        word!
            |
        copy a [tag! some word!] (append r-args a)
    ]

    parse args [
        opt text!
        any [arg-rule | attr-rule]
        end
    ] else [
        fail ["Unrecognized pattern in MAKE-CALLBACK function spec" args]
    ]

    ; print ["args:" mold args]

    safe: function r-args
        (if fallback [
            compose/deep <$> [
                trap [return ((<$> as group! body))] then (error -> [
                    print "** TRAPPED CRITICAL ERROR DURING FFI CALLBACK:"
                    print mold error
                    ((<$> fallback))
                ])
            ]
        ] else [
            body
        ])

    parse args [
        while [
            remove [tag! some word!]
            | skip
        ]
        end
    ]

    wrap-callback :safe args
]

sys/export [make-callback]
