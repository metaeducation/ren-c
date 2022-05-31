; %parse-maybe.test.reb
;
; MAYBE is an alternative to OPT.  It's similar in the sense that it permits
; the combinator it is parameterized with to fail--and then continue in the
; chain of rules.  But instead of returning NULL, it vanishes.


; Plain usage...potentially vanish inline, leaving prior result

("a" = uparse "aaa" [some "a" maybe some "b"])
("b" = uparse "aaabbb" [some "a" maybe some "b"])


; Usages with SET-WORD!, potentially opting out of changing variables and
; making the rule evaluation their previous value if opting out.

(did all [
    "b" == uparse "bbb" [
        (x: 10, y: 20)
        y: x: maybe some "a"  ; !!! maybe retention concept TBD
        some "b"
    ]
    unset? 'y
    unset? 'x
])

(did all [
    "b" == uparse "aaabbb" [
        (x: 10, y: 20)
        y: x: maybe some "a"  ; matched, so non-void...changes x
        some "b"
    ]
    x = "a"
    y = "a"
])

(did all [
    "b" == uparse "aaabbb" [
        (x: 10, y: 20)
        y: x: maybe elide some "a"  ; !!! TBD: invisibles stay invisible
        some "b"
    ]
    unset? 'x
    unset? 'y
])

(did all [
    "b" == uparse "bbb" [
        (x: 10, y: 20)
        y: x: maybe (~null~)  ; !!! TBD: align with `maybe if true [null]`
        some "b"
    ]
    unset? 'x
    unset? 'y
])

(did all [
    "b" == uparse "bbb" [
        (x: 10, y: 20)
        y: x: maybe (~)  ; Isotopic ~none~ not conflated w/invisible
        some "b"
    ]
    '~ = ^ get/any 'x
    '~ = ^ get/any 'y
])
