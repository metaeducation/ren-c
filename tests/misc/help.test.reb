; misc/help

(not error? sys/util/rescue [help])
(not error? sys/util/rescue [help help])
(not error? sys/util/rescue [help system])
(not error? sys/util/rescue [help to])
(not error? sys/util/rescue [help to-])
(not error? sys/util/rescue [help "to"])
(not error? sys/util/rescue [help nihil])
(not error? sys/util/rescue [help nihil?])
(not error? sys/util/rescue [help xxx])
(not error? sys/util/rescue [help function])

; !!! HELP and SOURCE have become more complex in Ren-C, due to the appearance
; of function compositions, and an attempt to have their "meta" information
; guide the help system--to try and explain their relationships, while not
; duplicating unnecessary information.  e.g. a function with 20 parameters
; that is specialized to have one parameter fixed, should not need all
; other 19 parameter descriptions to be copied onto a new function.
;
; The code trying to sort out these relationships has come along organically,
; and involves a number of core questions--such as when to use null vs. blank.
; It has had a tendency to break, so these tests are here even though they
; spew a large amount of output, in the interests of making HELP stay working.
;
(not error? sys/util/rescue [
    for-each w words of lib [
        dump w
        if unset? w [continue]
        if action? get w
            compose [help (w)]
        else [
            help (get w)
        ]
    ]
])
(not error? sys/util/rescue [
    for-each w words of lib [
        dump w
        if action? get/any w
            compose [source (w)]
    ]
])
