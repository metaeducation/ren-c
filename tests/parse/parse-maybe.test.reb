; %parse-maybe.test.reb
;
; MAYBE is an alternative to OPT.  It's similar in the sense that it permits
; the combinator it is parameterized with to fail--and then continue in the
; chain of rules.  But instead of returning NULL, it vanishes.
;
; !!! This may be too confusing.  Review.


; Plain usage...potentially vanish inline, leaving prior result

("a" = parse "aaa" [some "a" maybe some "b"])
("b" = parse "aaabbb" [some "a" maybe some "b"])


; Usages with SET-WORD!, potentially opting out of changing variables and
; making the rule evaluation their previous value if opting out.
;
; !!! So long as MAYBE is designed to return nihil, this can't do a legal
; assignment...demonstrate meta

(did all [
    "b" == parse "bbb" [
        (x: 10, y: 20)
        y: x: ^[maybe some "a"]  ; !!! maybe retention concept TBD
        some "b"
    ]
    nihil' = y
    nihil' = x
])

(did all [
    "b" == parse "aaabbb" [
        (x: 10, y: 20)
        y: x: ^[maybe some "a"]  ; matched, so non-void...changes x
        some "b"
    ]
    x = quote "a"
    y = quote "a"
])

(did all [
    "b" == parse "bbb" [
        (x: 10, y: 20)
        y: x: maybe (~)  ; Isotopic ~ is a none, unset variable
        some "b"
    ]
    '~ = ^ get/any 'x
    unset? 'y
])
