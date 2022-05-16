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
;    >> uparse "aaa" [repeat (foo) rule]
;
; However, REPEAT adds much more flexibility.  It can opt-out with a blank:
;
;    >> num: _
;    >> did uparse "aaa" [repeat (num) "b", some "a"]
;    == #[true]
;
; And it can also "opt all the way in" and become a synonym for WHILE with #:
;
;    >> num: #
;    >> did uparse "aaaaaaaaaa" [repeat (num) "a"]
;    == #[true]
;
; These decayed forms mean that you can get behavior differences out of your
; variables driving the looping while using the same rule.  But it also works
; for ranges, which are done with BLOCK!s.  The following for instance becomes
; "at least 3 matches" but has no upper limit:
;
;    >> min: 3
;    >> max: #
;    >> did uparse "aaaaaaa" [repeat (:[min max]) "a"]
;    == #[true]
;    >> did uparse "aaaaaaaaaaaaaaaaaaa" [repeat (:[min max]) "a"]
;    == #[true]
;    >> did uparse "aa" [repeat (:[min max]) "a"]
;    == #[false]
;
; If maximum is blank then it's assumed to be the same as if it were equal
; to the minimum, so `repeat (:[n _])` is the same as `repeat (n)`.  So if
; both maximum and minimum are blank, it's the same as `repeat (_)`, a no-op.
;


(
    var: 3
    rule: "a"
    "a" == uparse "aaa" [repeat (var) rule]  ; clearer than [var rule]
)

(
    var: 3
    rule: "a"
    "a" == uparse "aaaaaa" [2 repeat (var) rule]
)

(
    "b" == uparse ["b" 3 "b" "b" "b"] [rule: <any>, repeat integer! rule]
)

; A WHILE that never actually has a succeeding rule gives back a match that is
; a ~none~ isotope, as do 0-iteration REPEAT and INTEGER! rules.
[
    (none? uparse "a" ["a" repeat (0) "b"])
    ('~none~ = uparse "a" ["a" ^[repeat (0) "b"]])
]

; Conventional ranges
[
    (didn't uparse "a" [repeat ([2 3]) "a"])
    ("a" == uparse "aa" [repeat ([2 3]) "a"])
    ("a" == uparse "aaa" [repeat ([2 3]) "a"])
    (didn't uparse "aaaa" [repeat ([2 3]) "a"])
]

; Opt out completely
[
    ("a" == uparse "aaaaaaa" [repeat (_) "b", while "a"])
    ("a" == uparse "aaaaaaaaaaaaaaaaaaa" [repeat (_) "b", while "a"])
    ("a" == uparse "aa" [repeat (_) "b", while "a"])
    (none? uparse "" [repeat (_) "b", while "a"])
]

; Opt out completely, block form
[
    ("a" == uparse "aaaaaaa" [repeat ([_ _]) "b", while "a"])
    ("a" == uparse "aaaaaaaaaaaaaaaaaaa" [repeat ([_ _]) "b", while "a"])
    ("a" == uparse "aa" [repeat ([_ _]) "b", while "a"])
    (none? uparse "" [repeat ([_ _]) "b", while "a"])
]

; Minimum but no maximum
[
    ("a" == uparse "aaaaaaa" [repeat ([3 #]) "a"])
    ("a" == uparse "aaaaaaaaaaaaaaaaaaa" [repeat ([3 #]) "a"])
    (didn't uparse "aa" [repeat ([3 #]) "a"])
    (didn't uparse "" [repeat ([3 #]) "a"])
]

; Opt out of maximum (e.g. min max equivalence)
[
    ("a" == uparse "aaa" [repeat ([3 _]) "a"])
    (didn't uparse "aaaaaaaaaaaaaaaaaaa" [repeat ([3 _]) "a"])
    (didn't uparse "aa" [repeat ([3 _]) "a"])
    (didn't uparse "" [repeat ([3 _]) "a"])
]

; No minimum or maximum (WHILE equivalent), just using #
[
    ("a" == uparse "aaaaaaa" [repeat (#) "a"])
    ("a" == uparse "aaaaaaaaaaaaaaaaaaa" [repeat (#) "a"])
    ("a" == uparse "aa" [repeat (#) "a"])
    (none? uparse "" [repeat (#) "a"])
]

; No minimum or maximum (WHILE equivalent), block form
[
    ("a" == uparse "aaaaaaa" [repeat ([_ #]) "a"])
    ("a" == uparse "aaaaaaaaaaaaaaaaaaa" [repeat ([_ #]) "a"])
    ("a" == uparse "aa" [repeat ([_ #]) "a"])
    (none? uparse "" [repeat ([_ #]) "a"])
]

; THE-BLOCK! also accepted
[
    (didn't uparse "a" [repeat @[2 3] "a"])
    ("a" == uparse "aaaaaaa" [repeat @[_ _] "b", while "a"])
]
