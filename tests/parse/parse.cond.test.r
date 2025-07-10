; %parse-cond.test.r
;
; This implements what is known as IF in R3-Alpha and Red.
;
; It's an arity-1 construct that proceeds if the condition would trigger a
; branch to run, and skips to the next alternate if it would not.
;
; Calling such a thing "IF" when it is arity-1 is believed to be a poor choice,
; hence it's called COND(ITIONAL) in Ren-C.

(
    var: 'true
    true? parse [a a a] [cond (true? var) some 'a ('true)]
)(
    var: 'true
    true? parse [a a a] [cond (false? var) some 'a ('false) | some 'a ('true)]
)

(
    x: ~
    3 = parse [3 3 3] [x: cond (1 + 2) some inline (quote x) | (panic "boo")]
)(
    x: <untouched>
    <untouched> = parse [3 3 3] [x: cond (even? 1 + 2) | some '3 (x)]
)

[
    (
        x: ~
        "6" = parse "246" [some [
            x: across one elide cond (even? transcode:one x)
        ]]
    )
    ~parse-mismatch~ !! (
        x: ~
        parse "1" [x: across one elide cond (even? transcode:one x)]
    )
    ~parse-mismatch~ !! (
        x: ~
        parse "15" [some [x: across one elide cond (even? transcode:one x)]]
    )
]
