; %parse.test.reb
;
; Top-level feature tests for PARSE

; Putting non-combinators in the map should make that non-combinator behave like the
; thing it maps to
[(
    combs: copy default-combinators
    combs.here: <here>
    [b c] = parse/combinators [a b c] [skip 1, here, skip 2] combs
)(
    combs: copy default-combinators
    combs.aaa: [foo: across some "a"]
    combs.bbb: [tally "b"]
    all [
        3 = parse/combinators "aaabbb" [aaa bbb] combs
        foo = "aaa"
    ]
)]

; TRASH should be allowable as a combinator result.
[
    (trash? parse "a" ["a" (~)])
]
