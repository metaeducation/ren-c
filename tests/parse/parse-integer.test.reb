; %parse-integer.test.reb
;
; INTEGER! has some open questions regarding how it is not very visible when
; used as a rule...that maybe it should need a keyword.  Ranges are not worked
; out in terms of how to make [2 4 rule] range between 2 and 4 occurrences,
; as that breaks the combinator pattern at this time.

(uparse? "" [0 <any>])
(uparse? "a" [1 "a"])
(uparse? "aa" [2 "a"])

; A WHILE that never actually has a succeeding rule gives back a match that is
; a void isotope, as do 0-iteration REPEAT and INTEGER! rules.
[
    ("a" = uparse "a" ["a" 0 "b"])
    ("a" = uparse "a" ["a" [0 "b"]])
]

[#1280 (
    uparse "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]
