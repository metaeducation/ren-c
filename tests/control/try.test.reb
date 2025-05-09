; functions/control/try.r
;
; Note: This file is testing the error trapping functions.  So don't change
; the calls to TRAP here into test dialect calls:
;
;      ~dont-do-this~ !! (
;         warn "DON'T USE THIS TEST PATTERN HERE!"
;      )
;
; Annotations like #trap are used here to act as a reminder not to remove the
; traps from the test.

#trap (
    e: trap [1 / 0]
    e.id = 'zero-divide
)
#trap (
    e: trap [e: 1 / 0]
    e.id = 'zero-divide
)
#trap (
    success: 'true
    error? trap [
        1 / 0
        success: 'false
    ]
    true? success
)
#rescue (
    success: 'true
    f1: does [
        1 / 0
        success: 'false
    ]
    error? sys.util/rescue [f1]
    true? success
)
[#822
    #trap (
        trap [make error! ""] then [<branch-not-run>] else [okay]
    )
]
#rescue (
    sys.util/rescue [panic make error! ""] then [okay]
)
#trap (
    trap [1 / 0] then (:error?)
)
#trap (
    trap [1 / 0] then e -> [error? e]
)
#trap (
    trap [] then (func [e] [return <handler-not-run>]) else [okay]
)
[#1514
    #trap (
        error? sys.util/rescue [trap [1 / 0] then (:add)]
    )
]

[#1506
    #trap (
        10 = reeval unrun func [return: [integer!]] [trap [return 10] 20]
    )
]

; ENTRAP (similar to TRAP but single result, ^META result if not an error)

#entrap (
    (meta void) = entrap []
)
#entrap (
    (meta null) = entrap [null]
)
#entrap (
    (the '3) = entrap [1 + 2]
)
#entrap (
    (the '[b c]) = entrap [skip [a b c] 1]
)
#entrap (
    'zero-divide = (entrap [1 / 0]).id
)
#entrap (
    f: make frame! lambda [] [raise 'test]
    all wrap [
        error? e: entrap f
        e.id = 'test
    ]
)
#entrap (
    f: make frame! lambda [] [1000 + 20]
    all wrap [
        quoted? q: entrap f
        1020 = unquote q
    ]
)


; Multiple return values
#trap (
    null? trap [10 + 20]
)
#trap (
    e: trap [raise 'something]  ; trap before assign attempt
    all [
        error? e
        e.id = 'something
    ]
)
#trap (
    a: <a>
    b: <b>
    e: trap [[a b]: raise 'something]  ; trap after assign attempt
    all [
        error? e
        e.id = 'something
        a = <a>
        b = <b>
    ]
)
