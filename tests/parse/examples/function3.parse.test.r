; %function3.parse.test.r
;
; See explanation here:
;
; https://forum.rebol.info/t/dropping-with-in-static/2335
;
; Preserved as a test.  (But switched to use UPARSE instead of PARSE3.)

[
(function3: func [
    "Augment action with <static>, <in>, <with> features"

    return: [action!]
    spec "Help string (opt) followed by arg words (and opt type and string)"
        [block!]
    body "The body block of the function"
        [<const> block!]
    {
        new-spec var loc other
        new-body defaulters statics
    }
][
    ; The lower-level FUNC is implemented as a native, and this wrapper
    ; does a fast shortcut to check to see if the spec has no tags...and if
    ; not, it quickly falls through to that fast implementation.
    ;
    all [
        not find spec tag?/
        return func spec body
    ]

    ; Rather than MAKE BLOCK! LENGTH OF SPEC here, we copy the spec and clear
    ; it.  This costs slightly more, but it means we inherit the file and line
    ; number of the original spec...so when we pass NEW-SPEC to FUNC or PROC
    ; it uses that to give the FILE OF and LINE OF the function itself.
    ;
    ; !!! General API control to set the file and line on blocks is another
    ; possibility, but since it's so new, we'd rather get experience first.
    ;
    new-spec: clear copy spec  ; also inherits binding

    new-body: null
    statics: null
    defaulters: null
    var: #dummy  ; enter PARSE with truthy state (gets overwritten)
    loc: null

    parse spec [opt some [
        inline (if var '[  ; so long as we haven't reached any <local> or <with> etc.
            var: match [
                set-word? get-word? any-word? refinement?
                quoted!
                @group!  ; new soft-literal format
            ] (
                append new-spec var
            )
            |
            other: block! (
                append new-spec other  ; data type blocks
            )
            |
            other: across some text! (
                append new-spec spaced other  ; spec notes
            )
        ] else [
            'veto
        ])
    |
        other: group! (
            if not var [
                panic [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other)
                ]
            ]
            defaulters: default [inside body copy '[]]
            append defaulters spread compose [
                (var): default (lift eval inside spec other)
            ]
        )
    |
        (var: null)  ; everything below this line resets var
        veto  ; failing here means rolling over to next rule
    |
        '<local> (append new-spec <local>)
        opt some [var: word! other: try group! (
            append new-spec var
            if other [
                defaulters: default [inside body copy '[]]
                append defaulters spread compose [  ; always sets
                    (var): (lift eval inside spec other)
                ]
            ]
        )]
        (var: null)  ; don't consider further GROUP!s or variables
    |
        '<in> (
            new-body: default [
                copy:deep body
            ]
        )
        opt some [
            other: [object! | word! | tuple!] (
                if not object? other [
                    other: ensure [any-context?] get inside spec other
                ]
                other: bind other new-body
            )
        ]
    |
        '<with> opt some [
            other: [word! | path!]  ; !!! check if bound?
        |
            text!  ; skip over as commentary
        ]
    |
        ; For static variables to see each other, the GROUP!s can't have an
        ; hardened context.  We ignore their binding here for now.
        ;
        ; https://forum.rebol.info/t/2132
        ;
        '<static> (
            statics: default [copy inside spec '[]]
            new-body: default [
                copy:deep body
            ]
        )
        opt some [
            var: word!, other: try group! (
                append statics setify var
                append statics any [
                    bindable opt other  ; !!! ignore binding on group
                    '~
                ]
            )
        ]
        (var: null)
    |
        <end> accept (~)
    |
        other: <here> (
            panic [
                ; <where> spec
                ; <near> other
                "Invalid spec item:" @(other.1)
                "in spec" @spec
            ]
        )
    ]]

    if statics [
        statics: make object! statics
        new-body: bind statics new-body
    ]

    ; The constness of the body parameter influences whether FUNC* will allow
    ; mutations of the created function body or not.  It's disallowed by
    ; default, but TWEAK can be used to create variations e.g. a compatible
    ; implementation with Rebol2's FUNC.
    ;
    if const? body [new-body: const new-body]

    return func new-spec either defaulters [
        append defaulters as group! bindable any [new-body body]
    ][
        any [new-body body]
    ]
], ok)

    (
        accumulator: function3 [x [integer!] <static> state (0)] [
            return state: state + x
        ]

        all [
            1 = accumulator 1
            2 = accumulator 1
            10 = accumulator 8
        ]
    )
]
