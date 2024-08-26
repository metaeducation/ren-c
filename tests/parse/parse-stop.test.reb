; %parse-stop.test.reb
;
; PARSE STOP is like BREAK except the parse succeeds.  It can optionally
; match a rule and return a result.

[
    (nothing? parse "a" [some ["a" stop]])
    ("a" = parse "a" [some [stop "a"]])
]

; You should be able to stop at any depth
[
    ("aa" == parse "aaa" [some ["a" stop] "aa"])
    ("aa" == parse "aaa" [some ["a" [stop]] "aa"])
    ("a" == parse "aaa" [some ["a" [["a" stop]]] "a"])
]

; STOP optionally takes a rule as an argument
[
    ("a" == parse "aaa" [some ["a" stop "a"] "a"])
    ("a" == parse "aaa" [some ["a" [stop "a"]] "a"])
    ("" == parse "aaa" [some ["a" [["a" stop "a"]]] ""])
]

(
    "a" == parse "aaa" [some ["a", elide opt stop "b"]]
)

; https://github.com/Oldes/Rebol-issues/issues/967
;
(x: ~, all [nothing? parse "" [some  [(x: 2) stop]], x = 2])
