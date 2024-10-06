; %parse.test.reb
;
; Top-level feature tests for PARSE

; Putting non-combinators in the map should make that non-combinator behave like the
; thing it maps to
[(
    combs: copy default-combinators
    combs.here: <here>
    [b c] = parse:combinators [a b c] [skip 1, here, skip 2] combs
)(
    foo: ~
    combs: copy default-combinators
    combs.across-a: [foo: across some "a"]
    combs.tally-b: [tally "b"]
    all [
        3 = parse:combinators "aaabbb" [across-a tally-b] combs
        foo = "aaa"
    ]
)]

; NOTHING should be allowable as a combinator result.
[
    (nothing? parse "a" ["a" (~)])
]

; :RELAX means you don't have to match the full input
[
    (10 = parse:relax "aaa" ["a" (10)])
    ~parse-mismatch~ !! (10 = parse:relax "aaa" ["b" (10)])
]
