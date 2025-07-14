; %maxmatch.parse.test.r
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
    maxmatch-D: combinator [  ; "(D)efault"
        "Match maximum of two rules, keeping side effects of both if match"

        return: "Result of the longest match (favors first parser if equal)"
           [any-stable? pack!]
        pos [any-series?]
        parser1 [action!]
        parser2 [action!]
        <local> error1 error2 result1' result2' remainder1 remainder2
    ][
        error1: rescue [[^result1' remainder1]: parser1 pos]
        error2: rescue [[^result2' remainder2]: parser2 pos]
        if error2 [  ; parser2 didn't succeed
            if error1 [
                return fail error1  ; neither succeeded
            ]
        ] else [  ; parser2 succeeded
            any [
                error1
                (index of remainder1) < (index of remainder2)
            ] then [
                pos: remainder2
                return ^result2'
            ]
        ]
        pos: remainder1
        return ^result1'
    ]
    ok
)

    ; NON COLLECT VARIATIONS

    ; eight "a", so it's possible to get 4 matches of 2 "a" in but only
    ; 2 matches of 3 "a".
    ;
    (4 = parse "aaaaaaaa" [
        maxmatch-D [tally repeat 2 "a"] [tally repeat 3 "a"]
    ])
    (4 = parse "aaaaaaaa" [
        maxmatch-D [tally repeat 3 "a"] [tally repeat 2 "a"]
    ])

    ; With 9, it's the 3 "a" rule that gets further than the 2 "a" rule
    ; Just for fun show different bracketing options.  :-)
    ;
    (3 = parse "aaaaaaaaa" [
        maxmatch-D tally repeat 2 "a" tally repeat 3 "a"
    ])
    (3 = parse "aaaaaaaaa" [
        maxmatch-D tally [repeat 3 "a"] tally [repeat 2 "a"]
    ])

    ; As long as one rule succeeds, it's the longest match
    ;
    (4 = parse "aaaaaaaa" [maxmatch-D [tally repeat 2 "a"] [some "a" some "b"]])
    (4 = parse "aaaaaaaa" [maxmatch-D [some "a" some "b"] [tally repeat 2 "a"]])

    ; If neither rule succeeds the maxmatch fails
    ;
    ~parse-mismatch~ !! (
        parse "aaaaaaaa" [maxmatch-D [repeat 100 "a"] [some "a" some "b"]]
    )
    ~parse-mismatch~ !! (
        parse "aaaaaaaa" [maxmatch-D [some "a" some "b"] [repeat 100 "a"]]
    )

    ; COLLECT VARIATIONS - DEMONSTRATE THE AUTOMATIC ROLLBACK VARIANT
    ;
    ; If a parser is successful its results are kept, if it fails then
    ; not.  This does not account for the potential subtlety that the
    ; creator of MAXMATCH might have wanted the less matching combinator
    ; to have its accrued results disregarded.  That requires using the
    ; manual rollback interface.

    ; eight "a", so it's possible to get 4 matches of 2 "a" in but only
    ; 2 matches of 3 "a".
    ; (Both rules are successful, so by default both results are kept)
    ;
    (["aa" "aa" "aa" "aa" "aaa" "aaa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-D
                [some keep across repeat 2 "a"]
                [some keep across repeat 3 "a"]
        ]
    ])
    (["aaa" "aaa" "aa" "aa" "aa" "aa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-D
                [some keep across repeat 3 "a"]
                [some keep across repeat 2 "a"]
        ]
    ])

    ; With 9, it's the 3 "a" rule that gets further than the 2 "a" rule
    ; Just for fun show different bracketing options.  :-)
    ; (Both rules are successful, so by default both results are kept)
    ;
    (["aa" "aa" "aa" "aa" "aaa" "aaa" "aaa"] = parse "aaaaaaaaa" [
        collect [
            maxmatch-D
                some [keep across repeat 2 "a"]
                some [keep across repeat 3 "a"]
        ]
    ])
    (["aaa" "aaa" "aaa" "aa" "aa" "aa" "aa"] = parse "aaaaaaaaa" [
        collect [
            maxmatch-D
                [some keep [across repeat 3 "a"]]
                [some keep [across repeat 2 "a"]]
        ]
    ])

    ; As long as one rule succeeds, it's the longest match
    ; (Failing rule has its results discarded, automatically)
    ;
    (["aa" "aa" "aa" "aa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-D
                [some keep across repeat 2 "a"]
                [some keep "a" some keep "b"]
        ]
    ])
    (["aa" "aa" "aa" "aa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-D
                [some keep "a" some keep "b"]
                [some keep across repeat 2 "a"]
        ]
    ])

    ; If neither rule succeeds the maxmatch fails
    ; (Nothing is collected, returns null)
    ;
    ~parse-mismatch~ !! (parse "aaaaaaaa" [
        collect [
            maxmatch-D [repeat 100 keep "a"] [some keep "a" some keep "b"]
        ]
    ])
    ~parse-mismatch~ !! (parse "aaaaaaaa" [
        collect [
            maxmatch-D [some keep "a" some keep "b"] [repeat 100 keep "a"]
        ]
    ])

    ; Mix it up with both a collect and a gather in effect...
    ; (The z and "aaa" triples from shorter match are still in final result)
    (
        g: ~
        all [
            ["aa" "aa" "aa" "aaa" "aaa"] = parse "aaaaaaaa" [collect [
                g: gather [
                    maxmatch-D [
                        repeat 3 keep across repeat 2 "a"
                        emit x: ["a" (10)] emit y: ["a" (20)]
                    ] [
                        emit z: (304)
                        some keep across repeat 3 "a"
                    ]
                ]
            ]]
            g = make object! [
                x: 10
                y: 20
                z: 304
            ]
        ]
    )
]


; In the second variation of the combinator we will get involved directly with
; "rollback" and ask to not use the contributions from a successful parser
; if it was not the maximum match.  This involves becoming manually involved
; with `pending`, both as a return result and as a parameter to the parsers
; that are called.
[(
    maxmatch-C: combinator [  ; "(C)ustom"
        "Match maximum of two rules, keeping side effects of both if match"

        return: "Result of the longest match (favors first parser if equal)"
           [any-stable? pack!]
        pos [any-series?]
        :pending [blank? block!]
        parser1 [action!]
        parser2 [action!]
        <local>
            error1 error2 result1' result2'
            remainder1 remainder2 pending1 pending2
    ][
        error1: rescue [[^result1' remainder1 pending1]: parser1 pos]
        error2: rescue [[^result2' remainder2 pending2]: parser2 pos]
        if error2 [  ; parser2 didn't succeed
            if error1 [
                return fail error1  ; neither succeeded
            ]
        ] else [  ; parser2 succeeded
            any [
                error1
                (index of remainder1) < (index of remainder2)
            ] then [
                pos: remainder2
                pending: pending2
                return ^result2'
            ]
        ]
        pos: remainder1
        pending: pending1
        return ^result1'
    ]
    ok
)

    ; NON COLLECT VARIATIONS

    ; eight "a", so it's possible to get 4 matches of 2 "a" in but only
    ; 2 matches of 3 "a".
    ;
    (4 = parse "aaaaaaaa" [
        maxmatch-C [tally repeat 2 "a"] [tally repeat 3 "a"]
    ])
    (4 = parse "aaaaaaaa" [
        maxmatch-C [tally repeat 3 "a"] [tally repeat 2 "a"]
    ])

    ; With 9, it's the 3 "a" rule that gets further than the 2 "a" rule
    ; Just for fun show different bracketing options.  :-)
    ;
    (3 = parse "aaaaaaaaa" [
        maxmatch-C tally repeat 2 "a" tally repeat 3 "a"
    ])
    (3 = parse "aaaaaaaaa" [
        maxmatch-C tally [repeat 3 "a"] tally [repeat 2 "a"]
    ])

    ; As long as one rule succeeds, it's the longest match
    ;
    (4 = parse "aaaaaaaa" [maxmatch-C [tally repeat 2 "a"] [some "a" some "b"]])
    (4 = parse "aaaaaaaa" [maxmatch-C [some "a" some "b"] [tally repeat 2 "a"]])

    ; If neither rule succeeds the maxmatch fails
    ;
    ~parse-mismatch~ !! (
        parse "aaaaaaaa" [maxmatch-C [repeat 100 "a"] [some "a" some "b"]]
    )
    ~parse-mismatch~ !! (
        parse "aaaaaaaa" [maxmatch-C [some "a" some "b"] [repeat 100 "a"]]
    )

    ; COLLECT VARIATIONS - DEMONSTRATE THE AUTOMATIC ROLLBACK VARIANT
    ;
    ; If a parser is successful its results are kept, if it fails then
    ; not.  This does not account for the potential subtlety that the
    ; cretor of MAXMATCH might have wanted the less matching combinator
    ; to have its accrued results disregarded.  That requires using the
    ; manual rollback interface.

    ; eight "a", so it's possible to get 4 matches of 2 "a" in but only
    ; 2 matches of 3 "a".
    ; (This version of maxmatch only keeps the maxmatch's contributions; the
    ; lesser match--though successful--has its contributions discarded)
    ;
    (["aa" "aa" "aa" "aa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-C
                [some keep across repeat 2 "a"]
                [some keep across repeat 3 "a"]
        ]
    ])
    (["aa" "aa" "aa" "aa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-C
                [some keep across repeat 3 "a"]
                [some keep across repeat 2 "a"]
        ]
    ])

    ; With 9, it's the 3 "a" rule that gets further than the 2 "a" rule
    ; Just for fun show different bracketing options.  :-)
    ; (This version of maxmatch only keeps the maxmatch's contributions; the
    ; lesser match--though successful--has its contributions discarded)
    ;
    (["aaa" "aaa" "aaa"] = parse "aaaaaaaaa" [
        collect [
            maxmatch-C
                some [keep across repeat 2 "a"]
                some [keep across repeat 3 "a"]
        ]
    ])
    (["aaa" "aaa" "aaa"] = parse "aaaaaaaaa" [
        collect [
            maxmatch-C
                [some keep [across repeat 3 "a"]]
                [some keep [across repeat 2 "a"]]
        ]
    ])

    ; As long as one rule succeeds, it's the longest match
    ; (Failing rule has its results discarded, that's always the case)
    ;
    (["aa" "aa" "aa" "aa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-C
                [some keep across repeat 2 "a"]
                [some keep "a" some keep "b"]
        ]
    ])
    (["aa" "aa" "aa" "aa"] = parse "aaaaaaaa" [
        collect [
            maxmatch-C
                [some keep "a" some keep "b"]
                [some keep across repeat 2 "a"]
        ]
    ])

    ; If neither rule succeeds the maxmatch fails
    ; (Nothing is collected, returns null)
    ;
    ~parse-mismatch~ !! (parse "aaaaaaaa" [
        collect [
            maxmatch-C [repeat 100 keep "a"] [some keep "a" some keep "b"]
        ]
    ])
    ~parse-mismatch~ !! (parse "aaaaaaaa" [
        collect [
            maxmatch-C [some keep "a" some keep "b"] [repeat 100 keep "a"]
        ]
    ])

    ; Mix it up with both a collect and a gather in effect...
    ; (The z and "aaa" triples from shorter match won't be in the final result)
    (
        g: ~
        all [
            ["aa" "aa" "aa"] = parse "aaaaaaaa" [collect [
                g: gather [
                    maxmatch-C [
                        repeat 3 keep across repeat 2 "a"
                        emit x: ["a" (10)] emit y: ["a" (20)]
                    ] [
                        emit z: (304)
                        some keep across repeat 3 "a"
                    ]
                ]
            ]]
            g = make object! [
                x: 10
                y: 20
            ]
        ]
    )
]
