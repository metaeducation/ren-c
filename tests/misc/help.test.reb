; misc/help

(not error? trap [help])
(not error? trap [help help])
(not error? trap [help system])
(not error? trap [help to])
(not error? trap [help to-])
(not error? trap [help "to"])
(not error? trap [help nihil])
(not error? trap [help nihil?])
(not error? trap [help xxx])
(not error? trap [help function])

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
(not error? trap [
    for-each w words of lib [
        dump w
        if voided? w [continue]
        if action? get w
            compose [help (w)]
        else [
            help (get w)
        ]
    ]
])
(not error? trap [
    for-each w words of lib [
        dump w
        if action? get/any w
            compose [source (w)]
    ]
])
