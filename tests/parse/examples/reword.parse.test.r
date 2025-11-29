; %reword.parse.test.r
;
; Test of REWORD written with UPARSE instead of parse.
; See comments on non-UPARSE REWORD implementation.

[(uparse-reword: func [
    return: [any-string? blob!]
    source [any-string? blob!]
    values [map! object! block!]
    :case
    :escape [word! blob! block! any-string? char?]
] bind construct [
    delimiter-types: [word! blob! char? any-string?]
    keyword-types: [integer! word! blob! char? any-string?]
][
    let case_REWORD: case
    case: lib.case/

    let out: make (type of source) length of source

    let prefix: null
    let suffix: null
    case [
        null? escape [prefix: "$"]  ; refinement not used, so use default

        any [escape = "", escape = []] [
            ; pure search and replace, no prefix/suffix
        ]

        block? escape [
            parse escape [
                prefix: [_ (null) | match (delimiter-types)]
                suffix: [_ (null) | match (delimiter-types)]
            ] except [
                panic ["Invalid :ESCAPE delimiter block" escape]
            ]
        ]
    ] else [
        prefix: ensure delimiter-types escape
    ]

    if match [integer! word!] opt prefix [prefix: to-text prefix]
    if match [integer! word!] opt suffix [suffix: to-text suffix]

    if block? values [
        values: to map! values
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
                panic ["Invalid keyword type:" keyword]
            ]

            keep:line compose [
                (if match [integer! word!] keyword [
                    to-text keyword  ; `parse "a1" ['a '1]` illegal for now
                ] else [
                    keyword
                ])

                (opt suffix)  ; vaporize if suffix is null

                just (keyword)  ; make rule synthesize keyword
            ]
        ]
    ]

    prefix: default [[]]  ; default to no-op

    let rule: [
        let a: <here>  ; Begin marking text to copy verbatim to output
        opt some [
            to prefix  ; seek to prefix (may be [], this could be a no-op)
            let b: <here>  ; End marking text to copy verbatim to output
            prefix  ; consume prefix (if [], may not be at start of match)
            ||
            [let keyword-match: any (keyword-suffix-rules), (
                append:part out a measure a b  ; output before prefix

                let v: select // [values keyword-match, case: case_REWORD]
                append out switch:type v [
                    frame! [
                        apply:relax v [keyword-match]  ; arity-0 ok
                    ]
                    block! [eval v]
                ] else [
                    v
                ]
            )]
            a: <here>  ; Restart mark of text to copy verbatim to output
                |
            next  ; if wasn't at match, keep the SOME rule scanning ahead
        ]
        to <end>  ; Seek to end, just so rule succeeds
        (append out a)  ; finalize output - transfer any remainder verbatim
    ]

    parse-thru // [source rule case: case_REWORD] else [panic]  ; why panic?
    return out
]
ok)

("Multiple Search and Replace" = uparse-reword:escape "Multiple Foo and Bar" [
    "Foo" "Search" "Bar" "Replace"
] "")

("This is that." = uparse-reword "$1 is $2." [1 "This" 2 "that"])

("A fox is brown." = uparse-reword:escape "A %%a is %%b." [a "fox" b "brown"] "%%")

(
    "BrianH is answering Adrian." = uparse-reword:escape "I am answering you." [
        "I am" "BrianH is"
        you "Adrian"
    ] ""
)(
    "Hello is Goodbye" = uparse-reword:escape "$$$a$$$ is $$$b$$$" [
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
    "bar" = uparse-reword:escape text subs ["$<" ">"]
)]
