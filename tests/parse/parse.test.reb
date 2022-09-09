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
    did all [
        3 = parse/combinators "aaabbb" [aaa bbb] combs
        foo = "aaa"
    ]
)]

; PARSE+ reflects the believed desirable wrapper for PARSE:
;
; * If a THEN handler is provided, it runs on a successful parse--and the
;   branch is passed the parse result(s).
;
; * If an ELSE handler is provided, it runs on a failing parse--and the branch
;   is passed an ERROR! reflecting information about the parse failure.
;
; * If neither a THEN nor ELSE handler are provided, then a successful parse
;   will evaluate to the parse result(s)...while a failing parse will actually
;   *raise* the informative error.
;
; * Presence of a THEN handler only will defuse the reaction of a failing
;   parse raising an error, leading the overall result to be void.
(
    <null> = parse+ "a" [some "a" (null)] then r -> [if null? r [<null>]]
)
(
    null = (<overwritten> parse+ "a" ["b"] then [<unreachable>])
)
~???~ !! (
    parse+ "a" [some "b" (null)]
)
(
    <error> = parse+ "a" ["b"] else e -> [if error? e [<error>]]
)
(
    null = parse+ "a" [some "a" (null)] else e -> [<unreachable>]
)

; NONE should be allowable as a combinator result.
[
    (none? parse "a" ["a" (none)])
]
