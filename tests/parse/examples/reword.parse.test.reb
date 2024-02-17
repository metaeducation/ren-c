; %reword.parse.test.reb
;
; Test of REWORD written with UPARSE instead of parse.
; See comments on non-UPARSE REWORD implementation.

[(did uparse-reword: func [
    return: [any-string? binary!]
    source [any-string? binary!]
    values [map! object! block!]
    /case
    /escape [char?! any-string? word! binary! block!]

    <static>

    delimiter-types (
        [char?! &any-string? word! binary!]
    )
    keyword-types (
        [char?! &any-string? integer! word! binary!]
    )
][
    let case_REWORD: case
    case: :lib.case

    let out: make (kind of source) length of source

    let prefix: void  ; initialize with no-op rules
    let suffix: void
    case [
        null? escape [prefix: "$"]  ; refinement not used, so use default

        any [escape = "", escape = []] [
            ; pure search and replace, no prefix/suffix
        ]

        block? escape [
            parse escape [
                prefix: [_ (void) | any (delimiter-types)]
                suffix: [_ (void) | any (delimiter-types)]
            ] else [
                fail ["Invalid /ESCAPE delimiter block" escape]
            ]
        ]
    ] else [
        prefix: ensure delimiter-types escape
    ]

    if match [integer! word!] prefix [prefix: to-text prefix]
    if match [integer! word!] suffix [suffix: to-text suffix]

    if block? values [
        values: make map! values
    ]

    ; Build a block of rules like [keyword suffix result], looks like:
    ;
    ;    [["keyword" ">" ('keyword)] ["keyword2" ">" ('keyword2)] ...]
    ;
    ; This is used with UPARSE's ANY.  Note the prefix is matched before this
    ; (it doesn't need to be repeated) but the suffix is repeated in each
    ; rule, because otherwise "keyword" would match "keyword2".  So if the
    ; suffix was outside this rule block after it, noticing there was no ">"
    ; after the matched "keyword" would be at a time too late to go back and
    ; look for the keyword2 option.  :-/
    ;
    let keyword-suffix-rules: collect [
        for-each [keyword value] values [
            if not match keyword-types keyword [
                fail ["Invalid keyword type:" keyword]
            ]

            keep/line compose [
                (if match [integer! word!] keyword [
                    to-text keyword  ; `parse "a1" ['a '1]` illegal for now
                ] else [
                    keyword
                ])

                (suffix)  ; vaporize if suffix is void

                (engroup quote keyword)  ; make rule evaluate to actual keyword
            ]
        ]
    ]

    let rule: [
        let a: <here>  ; Begin marking text to copy verbatim to output
        try some [
            to prefix  ; seek to prefix (may be void, this could be a no-op)
            let b: <here>  ; End marking text to copy verbatim to output
            prefix  ; consume prefix (if no-op, may not be at start of match)
            ||
            [keyword-match: any (keyword-suffix-rules)] (
                append/part out a offset? a b  ; output before prefix

                let v: apply :select [values keyword-match, /case case_REWORD]
                append out switch/type v [
                    frame! [
                        apply/relax v [:keyword-match]  ; arity-0 ok
                    ]
                    block! [eval v]
                ] else [
                    v
                ]
            )
            a: <here>  ; Restart mark of text to copy verbatim to output
                |
            <any>  ; if wasn't at match, keep the SOME rule scanning ahead
        ]
        to <end>  ; Seek to end, just so rule succeeds
        (append out a)  ; finalize output - transfer any remainder verbatim
    ]

    apply :parse- [source rule, /case case_REWORD] else [fail]  ; why fail?
    return out
])

("Multiple Search and Replace" = uparse-reword/escape "Multiple Foo and Bar" [
    "Foo" "Search" "Bar" "Replace"
] "")

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
        "A" unrun lambda [] ["zero"]
        "B" unrun lambda [w] [join "one-" w]
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
