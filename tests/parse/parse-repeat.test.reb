; %parse-repeat.test.reb
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
; However, REPEAT adds much more flexibility.  It can opt-out with a blank:
;
;    >> num: null
;    >> did parse "aaa" [repeat (num) "b", some "a"]
;    == ~true~  ; anti
;
; It can also "opt all the way in" and become a synonym for MAYBE SOME with #:
;
;    >> num: #
;    >> did parse "aaaaaaaaaa" [repeat (num) "a"]
;    == ~true~  ; anti
;
; These decayed forms mean that you can get behavior differences out of your
; variables driving the looping while using the same rule.  But it also works
; for ranges, which are done with BLOCK!s.  The following for instance becomes
; "at least 3 matches" but has no upper limit:
;
;    >> min: 3
;    >> max: #
;    >> did parse "aaaaaaa" [repeat (:[min max]) "a"]
;    == ~true~  ; anti
;    >> did parse "aaaaaaaaaaaaaaaaaaa" [repeat (:[min max]) "a"]
;    == ~true~  ; anti
;    >> did parse "aa" [repeat (:[min max]) "a"]
;    == ~false~  ; anti
;
; If maximum is blank then it's assumed to be the same as if it were equal
; to the minimum, so `repeat (:[n _])` is the same as `repeat (n)`.  So if
; both maximum and minimum are blank, it's the same as `repeat (_)`, a no-op.
;


(
    var: 3
    rule: "a"
    "a" == parse "aaa" [repeat (var) rule]  ; clearer than [var rule]
)

(
    var: 3
    rule: "a"
    "a" == parse "aaaaaa" [repeat 2 repeat (var) rule]
)

(
    "b" == parse ["b" 3 "b" "b" "b"] [rule: <any>, repeat integer! rule]
)

; Plain loops that never actually run their body give back a match that is
; a void, as do 0-iteration REPEAT and INTEGER! rules.
[
    (void? parse "a" ["a" repeat (0) "b"])
    (void' = parse "a" ["a" ^[repeat (0) "b"]])
]

; Conventional ranges
[
    (raised? parse "a" [repeat ([2 3]) "a"])
    ("a" == parse "aa" [repeat ([2 3]) "a"])
    ("a" == parse "aaa" [repeat ([2 3]) "a"])
    (raised? parse "aaaa" [repeat ([2 3]) "a"])
]

; Opt out completely
[
    ("a" == parse "aaaaaaa" [repeat (_) "b", try some "a"])
    ("a" == parse "aaaaaaaaaaaaaaaaaaa" [repeat (_) "b", try some "a"])
    ("a" == parse "aa" [repeat (_) "b", try some "a"])
    (null = parse "" [repeat (_) "b", try some "a"])
]

; Opt out completely, block form
[
    ("a" == parse "aaaaaaa" [repeat ([_ _]) "b", try some "a"])
    ("a" == parse "aaaaaaaaaaaaaaaaaaa" [repeat ([_ _]) "b", try some "a"])
    ("a" == parse "aa" [repeat ([_ _]) "b", try some "a"])
    (null = parse "" [repeat ([_ _]) "b", try some "a"])
]

; Minimum but no maximum
[
    ("a" == parse "aaaaaaa" [repeat ([3 #]) "a"])
    ("a" == parse "aaaaaaaaaaaaaaaaaaa" [repeat ([3 #]) "a"])
    (raised? parse "aa" [repeat ([3 #]) "a"])
    (raised? parse "" [repeat ([3 #]) "a"])
]

; Opt out of maximum (e.g. min max equivalence)
[
    ("a" == parse "aaa" [repeat ([3 _]) "a"])
    (raised? parse "aaaaaaaaaaaaaaaaaaa" [repeat ([3 _]) "a"])
    (raised? parse "aa" [repeat ([3 _]) "a"])
    (raised? parse "" [repeat ([3 _]) "a"])
]

; No minimum or maximum (MAYBE SOME equivalent), just using #
[
    ("a" == parse "aaaaaaa" [repeat (#) "a"])
    ("a" == parse "aaaaaaaaaaaaaaaaaaa" [repeat (#) "a"])
    ("a" == parse "aa" [repeat (#) "a"])
    (void? parse "" [repeat (#) "a"])
]

; No minimum or maximum (MAYBE SOME equivalent), block form
[
    ("a" == parse "aaaaaaa" [repeat ([_ #]) "a"])
    ("a" == parse "aaaaaaaaaaaaaaaaaaa" [repeat ([_ #]) "a"])
    ("a" == parse "aa" [repeat ([_ #]) "a"])
    (void? parse "" [repeat ([_ #]) "a"])
]
