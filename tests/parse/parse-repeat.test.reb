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
;    >> uparse? "aaa" [repeat (num) "b", some "a"]
;    == #[true]
;
; And it can also "opt all the way in" and become a synonym for WHILE with #:
;
;    >> num: #
;    >> uparse? "aaaaaaaaaa" [repeat (num)]
;    == #[true]
;
; These decayed forms mean that you can get behavior differences out of your
; variables driving the looping while using the same rule.  But it also works
; for ranges, which are done with BLOCK!s.  The following for instance becomes
; "at least 3 matches" but has no upper limit:
;
;    >> min: 3
;    >> max: #
;    >> uparse? "aaaaaaa" [repeat (:[min max]) "a"]
;    == #[true]
;    >> uparse? "aaaaaaaaaaaaaaaaaaa" [repeat (:[min max]) "a"]
;    == #[true]
;    >> uparse? "aa" [repeat (:[min max]) "a"]
;    == #[false]
;
; If maximum is blank then it's assumed to be the same as if it were equal
; to the minimum, so `repeat (:[n _])` is the same as `repeat (n)`.  So if
; both maximum and minimum are blank, it's the same as `repeat (_)`, a no-op.
;


(
    var: 3
    rule: "a"
    uparse? "aaa" [repeat (var) rule]  ; clearer than [var rule]
)

(
    var: 3
    rule: "a"
    uparse? "aaaaaa" [2 repeat (var) rule]
)

(
    uparse? ["b" 3 "b" "b" "b"] [rule: <any>, repeat integer! rule]
)

; A WHILE that never actually has a succeeding rule gives back a match that is
; a ~none~ isotope, as do 0-iteration REPEAT and INTEGER! rules.
[
    ('~none~ = ^ uparse "a" ["a" repeat (0) "b"])
    ('~none~ = uparse "a" ["a" ^[repeat (0) "b"]])
]

; Conventional ranges
[
    (not uparse? "a" [repeat ([2 3]) "a"])
    (uparse? "aa" [repeat ([2 3]) "a"])
    (uparse? "aaa" [repeat ([2 3]) "a"])
    (not uparse? "aaaa" [repeat ([2 3]) "a"])
]

; Opt out completely
[
    (uparse? "aaaaaaa" [repeat (_) "b", while "a"])
    (uparse? "aaaaaaaaaaaaaaaaaaa" [repeat (_) "b", while "a"])
    (uparse? "aa" [repeat (_) "b", while "a"])
    (uparse? "" [repeat (_) "b", while "a"])
]

; Opt out completely, block form
[
    (uparse? "aaaaaaa" [repeat ([_ _]) "b", while "a"])
    (uparse? "aaaaaaaaaaaaaaaaaaa" [repeat ([_ _]) "b", while "a"])
    (uparse? "aa" [repeat ([_ _]) "b", while "a"])
    (uparse? "" [repeat ([_ _]) "b", while "a"])
]

; Minimum but no maximum
[
    (uparse? "aaaaaaa" [repeat ([3 #]) "a"])
    (uparse? "aaaaaaaaaaaaaaaaaaa" [repeat ([3 #]) "a"])
    (not uparse? "aa" [repeat ([3 #]) "a"])
    (not uparse? "" [repeat ([3 #]) "a"])
]

; Opt out of maximum (e.g. min max equivalence)
[
    (uparse? "aaa" [repeat ([3 _]) "a"])
    (not uparse? "aaaaaaaaaaaaaaaaaaa" [repeat ([3 _]) "a"])
    (not uparse? "aa" [repeat ([3 _]) "a"])
    (not uparse? "" [repeat ([3 _]) "a"])
]

; No minimum or maximum (WHILE equivalent), just using #
[
    (uparse? "aaaaaaa" [repeat (#) "a"])
    (uparse? "aaaaaaaaaaaaaaaaaaa" [repeat (#) "a"])
    (uparse? "aa" [repeat (#) "a"])
    (uparse? "" [repeat (#) "a"])
]

; No minimum or maximum (WHILE equivalent), block form
[
    (uparse? "aaaaaaa" [repeat ([_ #]) "a"])
    (uparse? "aaaaaaaaaaaaaaaaaaa" [repeat ([_ #]) "a"])
    (uparse? "aa" [repeat ([_ #]) "a"])
    (uparse? "" [repeat ([_ #]) "a"])
]

; THE-BLOCK! also accepted
[
    (not uparse? "a" [repeat @[2 3] "a"])
    (uparse? "aaaaaaa" [repeat @[_ _] "b", while "a"])
]
