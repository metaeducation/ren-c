; %reword.parse.test.reb
;
; Test of REWORD written with UPARSE instead of parse.
; See comments on non-UPARSE REWORD implementation.

[(did uparse-reword: function [
    return: [any-string! binary!]
    source [any-string! binary!]
    values [map! object! block!]
    /case
    /escape [char! any-string! word! binary! block!]

    <static>

    delimiter-types (
        make typeset! [char! any-string! word! binary!]
    )
    keyword-types (
        make typeset! [char! any-string! integer! word! binary!]
    )
][
    case_REWORD: if case [/case]
    case: :lib/case

    out: make (type of source) length of source

    prefix: ~unset~
    suffix: ~unset~
    case [
        null? escape [prefix: "$"]  ; refinement not used, so use default

        any [
            escape = ""
            escape = []
        ][
            ; !!! Review: NULL not supported by UPARSE at the moment
            prefix: ""  ; pure search and replace, no prefix/suffix
        ]

        block? escape [
            uparse escape [
                prefix: delimiter-types
                suffix: opt delimiter-types
            ] else [
                fail ["Invalid /ESCAPE delimiter block" escape]
            ]
        ]
    ] else [
        prefix: ensure delimiter-types escape
    ]

    ; !!! UPARSE doesn't allow fetched blank or fetched NULL at the moment.
    ; Use an empty string.
    ;
    suffix: default [""]  ; default to no suffix

    if match [integer! word!] prefix [prefix: to-text prefix]
    if match [integer! word!] suffix [suffix: to-text suffix]

    if block? values [
        values: make map! values
    ]

    keyword-match: null  ; variable that gets set by rule
    any-keyword-suffix-rule: collect [
        for-each [keyword value] values [
            if not match keyword-types keyword [
                fail ["Invalid keyword type:" keyword]
            ]

            keep compose/deep <*> [
                (<*> if match [integer! word!] keyword [
                    to-text keyword  ; `parse "a1" ['a '1]` illegal for now
                ] else [
                    keyword
                ])

                (<*> suffix)

                (keyword-match: '(<*> keyword))
            ]

            keep/line [|]
        ]
        keep [false]  ; add failure if no match, instead of removing last |
    ]

    rule: [
        a: <here>  ; Begin marking text to copy verbatim to output
        while [
            to prefix  ; seek to prefix (may be blank!, this could be a no-op)
            b: <here>  ; End marking text to copy verbatim to output
            prefix  ; consume prefix (if no-op, may not be at start of match)
            [
                [
                    any-keyword-suffix-rule (
                        append/part out a offset? a b  ; output before prefix

                        v: select/(case_REWORD) values keyword-match
                        append out switch type of :v [
                            action! [
                                ; Give v the option of taking an argument, but
                                ; if it does not, evaluate to arity-0 result.
                                ;
                                (result: v :keyword-match)
                                :result
                            ]
                            block! [do :v]
                        ] else [
                            :v
                        ]
                    )
                    a: <here>  ; Restart mark of text to copy verbatim to output
                ]
                    |
                <any>  ; if wasn't at match, keep the WHILE rule scanning ahead
            ]
        ]
        to <end>  ; Seek to end, just so rule succeeds
        (append out a)  ; finalize output - transfer any remainder verbatim
    ]

    uparse*/(case_REWORD) source rule else [fail]  ; should succeed
    return out
])

("This is that." = uparse-reword "$1 is $2." [1 "This" 2 "that"])

("A fox is brown." = uparse-reword/escape "A %%a is %%b." [a "fox" b "brown"] "%%")

(
    "BrianH is answering Adrian." = uparse-reword/escape "I am answering you." [
        "I am" "BrianH is"
        you "Adrian"
    ] ""
)(
    "Hello is Goodbye" = uparse-reword/escape "$$$a$$$ is $$$b$$$" [
       a Hello
       b Goodbye
    ] ["$$$" "$$$"]
)

; Functions can optionally take the keyword being replaced
(
    "zero is one-B" = uparse-reword "$A is $B" reduce [
        "A" func [] ["zero"]
        "B" func [w] [join "one-" w]
    ]
)
(
    https://github.com/metaeducation/ren-c/issues/1005
    ("ò" = uparse-reword "ò$a" reduce ['a ""])
)
    ;#2333
(
    subs: ["1" "foo" "10" "bar"]
    text: "$<10>"
    "bar" = uparse-reword/escape text subs ["$<" ">"]
)]
