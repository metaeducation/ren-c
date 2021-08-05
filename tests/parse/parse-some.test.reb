; %parse-some.test.reb
;
; One or more matches.

(
    x: ~
    did all [
        uparse? "aaa" [x: opt some "b", some "a"]
        x = null
    ]
)(
    x: ~
    did all [
        uparse? "aaa" [x: opt some "a"]
        x = "a"
    ]
)


; Unless they are "invisible" (like ELIDE), rules return values.  If the
; rule's purpose is not explicitly to generate new series content (like a
; COLLECT) then it tries to return something very cheap...e.g. a value it
; has on hand, like the rule or the match.  This can actually be useful.
[
    (
        x: null
        did all [
            uparse? "a" [x: "a"]
            "a" = x
        ]
    )(
        x: null
        did all [
            uparse? "aaa" [x: some "a"]
            "a" = x  ; SOME doesn't want to be "expensive" on average
        ]
    )(
        x: null
        did all [
            uparse? "aaa" [x: [some "a" | some "b"]]
            "a" = x  ; demonstrates use of the result (which alternate taken)
        ]
    )
]