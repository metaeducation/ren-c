; %parse-return.test.reb
;
; RETURN was removed from PARSE3 but is being re-added to UPARSE, as
; its distortion of the return result is no longer a "distortion" since
; the block combinator can return anything.
;

(
    "bbb" = uparse "aaabbb" [some "a", return <here>]
)
(
    10 = uparse [aaa] [return (10)]
)
(
    let result: uparse "aaabbbccc" [
        return gather [
            emit x: collect some ["a", keep (<a>)]
            emit y: collect some ["b", keep (<b>)]
        ]
    ] else [
        fail "Parse failed"
    ]
    did all [
        result.x = [<a> <a> <a>]
        result.y = [<b> <b> <b>]
    ]
)

; Successful RETURN of NULL will be turned into an isotope.
(
    '~null~ = ^ uparse "aaa" [return (null)]
)

; Trying to return a failing rule is like any other non-match, won't take
; the action...
(
    "b" = uparse "aaabbb" [some "a", opt return some "c", some "b"]
)
