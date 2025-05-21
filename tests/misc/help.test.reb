; %help.test.reb
;
; !!! HELP and SOURCE have become more complex in Ren-C, due to the appearance
; of function compositions, and an attempt to have their "meta" information
; guide the help system--to try and explain their relationships, while not
; duplicating unnecessary information.  e.g. a function with 20 parameters
; that is specialized to have one parameter fixed, should not need all
; other 19 parameter descriptions to be copied onto a new function.
;
; The code trying to sort out these relationships has come along organically,
; and involves a number of core questions--such as when to use null vs. space.
; It has had a tendency to break, so these tests are here even though they
; spew a large amount of output, in the interests of making HELP stay working.

(trash? help)
(trash? help help)
(trash? help system)
(trash? help to)
(trash? help "to-")
(trash? help "to")
(trash? help void)
(trash? help xxx)
(trash? help function)

(
    for-each 'w words of lib [
        dump w
        if vacant? w [continue]
        if action? get w
            (compose:deep [assert [trash? help (w)]])
        else [
            if not rune? get w [  ; "don't open web browser"
                assert [trash? help (get w)]
            ]
        ]
    ]
    ok
)
(
    trash? source ||   ; Was once a tricky case, SOURCE of a barrier
)
(
    for-each 'w words of lib [
        dump w
        if quasi? (lift get:any w) [continue]
        if action? get w
            (compose:deep [assert [trash? source (w)]])
    ]
    ok
)

[https://github.com/metaeducation/ren-c/issues/1106
    (trash? help "any")
]

(trash? about)
