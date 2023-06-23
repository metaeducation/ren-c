; %parse-path.test.reb
;
; Historical Rebol and Red considered PATH! processing "too slow" to
; make convenient to use in rules.  Ren-C seeks to provide convenience
; and expressivity, so it permits object field access...however that
; is expected to be generally done with TUPLE!
;
; PATH!s currently have a special feature that if a path ends in a
; slash it will be looked up as a normal function, whose arguments
; will then be fulfilled by the values synthesized from parse rules.
; It's a dodgy mechanism, but interesting.
;
; PATH! combinator is still a work in progress.

[https://github.com/red/red/issues/4101
    ('a/b == parse [a/b] ['a/b])

    ~unassigned-attach~ !! (
        parse [a/b] [a/b]
    )
    ~unassigned-attach~ !! (
        parse [a b c] [change repeat 3 word! d/e]
    )
    ~unassigned-attach~ !! (
        parse [a/b c d] [remove a/b]
    )
    ~unassigned-attach~ !! (
        parse [c d] [insert a/b repeat 2 word!]
    )
]
