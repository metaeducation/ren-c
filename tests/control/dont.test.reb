; DON'T is an experimental exposure of a "neutral" form of DO, which will
; only return whether or not the position of the end of the evaluation can
; be reliably determined.  It cannot handle variadic functions (including
; EVAL, which is effectively variadic)
;
; These are some relatively basic tests, but much better would be to run
; the "idle" evaluator in parallel against the non-idle evaluator and make
; sure they stay in alignment, as a comprehensive debug mode.

[
    pos: _
    did all [
        don't []
        don't/next [] 'pos
        pos = []
    ]
]

[
    success: true
    did all [
        don't [success: false (success: false) :abs set 'success <bad>]
        success = true
    ]
][
    pos: _
    success: true
    code: [success: 1 + 2 (success: false) :abs set 'success <bad>]
    did all [
        don't code
        don't/next code 'pos
        pos = [(success: false) :abs set 'success <bad>]
        don't/next pos 'pos
        pos = [:abs set 'success <bad>]
        don't/next pos 'pos
        pos = [set 'success <bad>]
        don't/next pos 'pos
        pos = []
        don't/next pos 'pos
        pos = []
        success = true
    ]
]

[
    false = don't [eval :abs -1] ;-- EVAL is effectively variadic
]

[
    true = don't [throw 10]
][
    true = don't [return 20]
]
