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
        (x: 10, y: ~)
        y: x: maybe some "a"  ; no match, so void, no change to x
        some "b"
    ]
    y = 10
    x = 10
])

(did all [
    "b" == uparse "aaabbb" [
        (x: 10, y: ~)
        y: x: maybe some "a"  ; matched, so non-void...changes x
        some "b"
    ]
    x = "a"
    y = "a"
])

(did all [
    "b" == uparse "aaabbb" [
        (x: 10, y: ~)
        y: x: maybe elide some "a"  ; invisibles stay invisible, no change to x
        some "b"
    ]
    x = 10
    y = 10
])

(did all [
    "b" == uparse "bbb" [
        (x: 10, y: ~)
        y: x: maybe (~null~)  ; Align with `maybe if true [null]` vanishing
        some "b"
    ]
    x = 10
    y = 10
])

(did all [
    "b" == uparse "bbb" [
        (x: 10, y: ~)
        y: x: maybe (~none~)  ; Isotopic ~none~ not conflated w/invisible
        some "b"
    ]
    none? get/any 'x
    none? get/any 'y
])
