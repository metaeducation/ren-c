; %parse-repeat.test.r
;
; UPARSE's REPEAT started out as a way of making it more obvious when a rule
; was hard to discern, as integer variables would be taking rules as parameters.
;
;    rebol2>> foo: 3
;    rebol2>> rule: "a"
;
;    rebol2>> parse "aaa" [foo rule]  ; bond between FOO and RULE non-obvious
;
; With REPEAT this was clearer, since it has a known arity of 2, so you know
; to look for the count and the rule.
;
;    >> parse "aaa" [repeat (foo) rule]
;
; However, REPEAT adds much more flexibility.  It can opt-out with void:
;
;    >> num: null
;    >> parse "aaa" [repeat (opt num) "b", some "a"]
;    == "a"
;
; It can also "opt all the way in" and become a synonym for OPT SOME with #
;
;    >> num: #
;    >> parse "aaaaaaaaaa" [repeat (num) "a"]
;    == "a"
;
; These decayed forms mean that you can get behavior differences out of your
; variables driving the looping while using the same rule.  But it also works
; for ranges, which are done with BLOCK!s.  The following for instance becomes
; "at least 3 matches" but has no upper limit:
;
;    >> min: 3
;    >> max: #
;    >> parse "aaaaaaa" [repeat (:[min max]) "a"]
;    == "a"
;    >> parse "aaaaaaaaaaaaaaaaaaa" [repeat (:[min max]) "a"]
;    == "a"
;    >> parse "aa" [repeat (:[min max]) "a"]
;    ** Error: PARSE BLOCK! combinator did not match input
;
; If maximum is space then it's assumed to be the same as if it were equal
; to the minimum, so `repeat (:[n _])` is the same as `repeat (n)`.  So if
; both maximum and minimum are space, it's the same as `repeat (_)`, a no-op.
;

(
    var: 3
    rule: "a"
    "a" = parse "aaa" [repeat (var) rule]  ; clearer than [var rule]
)

(
    var: 3
    rule: "a"
    "a" = parse "aaaaaa" [repeat 2 repeat (var) rule]
)

(
    rule: ~
    "b" = parse ["b" 3 "b" "b" "b"] [rule: one, repeat integer! rule]
)

; Plain loops that never actually run their body give back a match that is
; a void, as do 0-iteration REPEAT and INTEGER! rules.
[
    (void? parse "" [repeat 0 one])

    (void? parse "a" ["a" repeat (0) "b"])
    (void? parse "a" ["a" ^[lift repeat (0) "b"]])

    ("a" = parse "a" ["a" /elide-if-void repeat 0 "b"])
    (
        x: ~
        all [
            ??? = parse "a" ["a" x: ^[repeat 0 "b"]]
            ??? = x
        ]
    )

    ("a" = parse "a" ["a" /elide-if-void repeat 0 "b"])
    ("a" = parse "a" ["a" /elide-if-void [repeat 0 "b"]])
]

[
    ("a" = parse "a" [repeat 1 "a"])
    ("a" = parse "aa" [repeat 2 "a"])
]

; Conventional ranges
[
    ~parse-mismatch~ !! (parse "a" [repeat ([2 3]) "a"])
    ~parse-incomplete~ !! (parse "aaaa" [repeat ([2 3]) "a"])

    ("a" = parse "aa" [repeat ([2 3]) "a"])
    ("a" = parse "aaa" [repeat ([2 3]) "a"])
]

; Opt out completely
[
    ("a" = parse "aaaaaaa" [repeat (_) "b", try some "a"])
    ("a" = parse "aaaaaaaaaaaaaaaaaaa" [repeat (_) "b", try some "a"])
    ("a" = parse "aa" [repeat (_) "b", try some "a"])
    (null = parse "" [repeat (_) "b", try some "a"])
]

; Opt out completely, block form
[
    ("a" = parse "aaaaaaa" [repeat ([_ _]) "b", try some "a"])
    ("a" = parse "aaaaaaaaaaaaaaaaaaa" [repeat ([_ _]) "b", try some "a"])
    ("a" = parse "aa" [repeat ([_ _]) "b", try some "a"])
    (null = parse "" [repeat ([_ _]) "b", try some "a"])
]

; Minimum but no maximum
[
    ("a" = parse "aaaaaaa" [repeat ([3 #]) "a"])
    ("a" = parse "aaaaaaaaaaaaaaaaaaa" [repeat ([3 #]) "a"])

    ~parse-mismatch~ !! (parse "aa" [repeat ([3 #]) "a"])
    ~parse-mismatch~ !! (parse "" [repeat ([3 #]) "a"])
]

; Opt out of maximum (e.g. min max equivalence)
[
    ("a" = parse "aaa" [repeat ([3 _]) "a"])

    ~parse-incomplete~ !! (parse "aaaaaaaaaaaaaaaaaaa" [repeat ([3 _]) "a"])

    ~parse-mismatch~ !! (parse "aa" [repeat ([3 _]) "a"])
    ~parse-mismatch~ !! (parse "" [repeat ([3 _]) "a"])
]

; No minimum or maximum (OPT SOME equivalent), just using #
[
    ("a" = parse "aaaaaaa" [repeat (#) "a"])
    ("a" = parse "aaaaaaaaaaaaaaaaaaa" [repeat (#) "a"])
    ("a" = parse "aa" [repeat (#) "a"])
    (void? parse "" [repeat (#) "a"])
]

; No minimum or maximum (OPT SOME equivalent), block form
[
    ("a" = parse "aaaaaaa" [repeat ([_ #]) "a"])
    ("a" = parse "aaaaaaaaaaaaaaaaaaa" [repeat ([_ #]) "a"])
    ("a" = parse "aa" [repeat ([_ #]) "a"])
    (void? parse "" [repeat ([_ #]) "a"])
]

[
    ~parse-incomplete~ !! (parse [a a] [repeat 1 ['a]])
    ~parse-incomplete~ !! (parse [a a] [repeat 1 'a])
    ~parse-incomplete~ !! (parse [a a] [repeat 1 repeat 1 'a])

    ('a = parse [a a] [repeat 2 ['a]])
    ('a = parse [a a] [repeat 2 'a])

    ~parse-mismatch~ !! (parse [a a] [repeat 3 ['a]])
    ~parse-mismatch~ !! (parse [a a] [repeat 3 'a])
]

[
    ('b = parse [a a b b] [repeat 2 'a repeat 2 'b])
    ~parse-mismatch~ !! (parse [a a b b] [repeat 2 'a repeat 3 'b])
]

[https://github.com/red/red/issues/564
    ~parse-incomplete~ !! (parse [a] [repeat 0 one])
    ('a = parse [a] [repeat 0 one 'a])
    (
        z: ~
        all [
            error? parse [a] [z: across repeat 0 one]
            z = []
        ]
    )
]

[https://github.com/red/red/issues/564
    ~parse-incomplete~ !! (parse "a" [repeat 0 one])
    (#a = parse "a" [repeat 0 one #a])
    (
        z: ~
        all [
            error? parse "a" [z: across repeat 0 one]
            z = ""
        ]
    )
]

[https://github.com/red/red/issues/4591
    (void? parse [] [repeat 0 [ignore me]])
    (void? parse [] [repeat 0 "ignore me"])
    (void? parse [] [repeat 0 repeat 0 [ignore me]])
    (void? parse [] [repeat 0 repeat 0 "ignore me"])

    ~parse-incomplete~ !! (parse [x] [repeat 0 repeat 0 'x])
    ~parse-incomplete~ !! (parse " " [repeat 0 repeat 0 space])
]

[#1280 (
    i: ~
    parse "" [(i: 0) repeat 3 [["a" |] (i: i + 1)]]
    i = 3
)]
