; %parse-accept.test.reb
;
; RETURN was removed from PARSE3 but was re-added to UPARSE under the new
; name of ACCEPT.  This helps avoid conflation with a function's RETURN, as
; well as suggest the ability of an ACCEPT rule to fail (vs. the seeming
; inevitability of RETURN).
;
; !!! Should there be a corresponding REJECT?  Would it be arity-0?

(
    "bbb" = parse "aaabbb" [some "a", accept <here>]
)
(
    10 = parse [aaa] [accept (10)]
)
(
    all [
        let result: parse "aaabbbccc" [
            accept gather [
                emit x: collect some ["a", keep (<a>)]
                emit y: collect some ["b", keep (<b>)]
            ]
            (fail "unreachable")
        ]
        result.x = [<a> <a> <a>]
        result.y = [<b> <b> <b>]
    ]
)

(
    null = parse "aaa" [accept (null)]
)

; Optionally accept a failing rule is like any other non-match, won't take
; the action...
(
    "b" = parse "aaabbb" [some "a", opt accept some "c", some "b"]
)
