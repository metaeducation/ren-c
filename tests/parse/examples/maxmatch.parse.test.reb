; %maxmatch.parse.test.reb
;
; MAXMATCH was a thought experiment proposed as a way of illustrating that
; there can be multiple ideas for the "rollback" semantics of certain
; constructs--so there has to be a way for the combinator to decide vs.
; just assuming a simple rule.  We try to offer the best of both worlds
; with a simple automatic rollback mechanism that can be overridden if
; the combinator expresses an intent to be directly involved.
;
; It's not intended to be particularly useful--it doesn't have an obvious
; right answer if the matches are both equal length.  But it's the kind of
; combinator that a user could write and use off the cuff.


; In the first variation of the combinator we will use the default
; "rollback" mechanism.  This happens when you do not mention the
; pending list of accrued items at all.
[(
    maxmatch: combinator [
        {Match maximum of two rules, keeping side effects of both if match}
        return: "Result of the longest match (favors first parser if equal)"
           [<opt> any-value!]
        parser1 [action!]
        parser2 [action!]
        <local> result1' result2' remainder1 remainder2
    ][
        ([result1' remainder1]: ^ parser1 input)
        ([result2' remainder2]: ^ parser2 input)
        if null? result2'[  ; parser2 failed
            if null? result1' [return null]  ; they both failed
        ] else [  ; parser2 succeeded
            any [
                null? result1'
                (index of remainder1) < (index of remainder2)
            ] then [
                set remainder remainder2
                return unmeta result2'
            ]
        ]
        set remainder remainder1
        return unmeta result1'
    ]
)

    ; NON COLLECT VARIATIONS

    ; eight "a", so it's possible to get 4 matches of 2 "a" in but only
    ; 2 matches of 3 "a".
    ;
    (4 = uparse "aaaaaaaa" [maxmatch [tally 2 "a"] [tally 3 "a"]])
    (4 = uparse "aaaaaaaa" [maxmatch [tally 3 "a"] [tally 2 "a"]])

    ; With 9, it's the 3 "a" rule that gets further than the 2 "a" rule
    ; Just for fun show different bracketing options.  :-)
    ;
    (3 = uparse "aaaaaaaaa" [maxmatch tally 2 "a" tally 3 "a"])
    (3 = uparse "aaaaaaaaa" [maxmatch tally [3 "a"] tally [2 "a"]])

    ; As long as one rule succeeds, it's the longest match
    ;
    (4 = uparse "aaaaaaaa" [maxmatch [tally 2 "a"] [some "a" some "b"]])
    (4 = uparse "aaaaaaaa" [maxmatch [some "a" some "b"] [tally 2 "a"]])

    ; If neither rule succeeds the maxmatch fails
    ;
    (null = uparse "aaaaaaaa" [maxmatch [100 "a"] [some "a" some "b"]])
    (null = uparse "aaaaaaaa" [maxmatch [some "a" some "b"] [100 "a"]])

    ; COLLECT VARIATIONS - DEMONSTRATE THE AUTOMATIC ROLLBACK VARIANT
    ;
    ; If a parser is successful its results are kept, if it fails then
    ; not.  This does not account for the potential subtlety that the
    ; cretor of MAXMATCH might have wanted the less matching combinator
    ; to have its accrued results disregarded.  That requires using the
    ; manual rollback interface.

    ; eight "a", so it's possible to get 4 matches of 2 "a" in but only
    ; 2 matches of 3 "a".
    ;
    (["aa" "aa" "aa" "aa" "aaa" "aaa"] = uparse "aaaaaaaa" [
        collect [
            maxmatch [while keep across 2 "a"] [while keep across 3 "a"]
        ]
    ])
    (["aaa" "aaa" "aa" "aa" "aa" "aa"] = uparse "aaaaaaaa" [
        collect [
            maxmatch [while keep across 3 "a"] [while keep across 2 "a"]
        ]
    ])

    ; With 9, it's the 3 "a" rule that gets further than the 2 "a" rule
    ; Just for fun show different bracketing options.  :-)
    ;
    (["aa" "aa" "aa" "aa" "aaa" "aaa" "aaa"] = uparse "aaaaaaaaa" [
        collect [
            maxmatch while [keep across 2 "a"] while [keep across 3 "a"]
        ]
    ])
    (["aaa" "aaa" "aaa" "aa" "aa" "aa" "aa"] = uparse "aaaaaaaaa" [
        collect [
            maxmatch [while keep [across 3 "a"]] [while keep [across 2 "a"]]
        ]
    ])

    ; As long as one rule succeeds, it's the longest match
    ; (Failing rule has its results discarded, automatically)
    ;
    (["aa" "aa" "aa" "aa"] = uparse "aaaaaaaa" [
        collect [
            maxmatch [while keep across 2 "a"] [some keep "a" some keep "b"]
        ]
    ])
    (["aa" "aa" "aa" "aa"] = uparse "aaaaaaaa" [
        collect [
            maxmatch [some keep "a" some keep "b"] [while keep across 2 "a"]
        ]
    ])

    ; If neither rule succeeds the maxmatch fails
    ; (Nothing is collected, returns null)
    ;
    (null = uparse "aaaaaaaa" [
        collect [
            maxmatch [100 keep "a"] [some keep "a" some keep "b"]
        ]
    ])
    (null = uparse "aaaaaaaa" [
        collect [
            maxmatch [some keep "a" some keep "b"] [100 keep "a"]
        ]
    ])
]
