; %parse-repeat.test.reb
;
; REPEAT lets you make it more obvious when a rule is being repeated, which is
; hidden by INTEGER! variables.

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
; a void isotope, as do 0-iteration REPEAT and INTEGER! rules.
[
    ("a" = uparse "a" ["a" repeat (0) "b"])
    ("a" = uparse "a" ["a" [repeat (0) "b"]])
]
