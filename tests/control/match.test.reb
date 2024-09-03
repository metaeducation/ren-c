; %match.test.reb
;
; MATCH started out as a userspace function, but gained frequent usage..
;

(10 = match integer! 10)
(null = match integer! "ten")

("ten" = match [integer! text!] "ten")
(20 = match [integer! text!] 20)
(null = match [integer! text!] <tag>)

(10 = match :even? 10)
(null = match :even? 3)

(nothing? match blank! _)
(null = match blank! 10)
(null = match blank! false)

; Since its other features were implemented with a fairly complex enclosed
; specialization, it's good to keep that usermode implementation around,
; just to test those features.

[
    (did match2: enclose specialize 'either-test [
        branch: [null] ;-- runs on test failure
    ] function [
        return: [~null~ any-value!]
        f [frame!]
    ][
        arg: :f/arg else [
            fail "MATCH cannot take null as input" ;-- EITHER-TEST allows it
        ]

        ; Ideally we'd pass through all input results on a "match" and give
        ; null to indicate a non-match.  But what about:
        ;
        ;     if match [logic!] 1 > 2 [...]
        ;     if match [blank!] find "abc" "d" [...]
        ;
        ; Rather than have MATCH return a falsey result in these cases of
        ; success, pass back a NOTHING in the hopes of drawing attention.

        set the result: do f  ; can't access f/arg after the DO

        if not :arg and [not null? :result] [
            return ~  ; nothing if matched a falsey type
        ]
        :result  ; return null if no match, else truthy result
    ])

    (10 = match2 integer! 10)
    (null = match2 integer! "ten")
    (nothing? match2 blank! _)
    (null = match2 blank! 10)
]
