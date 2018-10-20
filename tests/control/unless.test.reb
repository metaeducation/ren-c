; In historical Rebol, UNLESS was a synonym for IF NOT.  This somewhat
; unambitious use of the word has been replaced in Ren-C with an enfix
; operator that allows one to put the default for an evaluation before the
; cases which may produce a result.
;
;     >> x: {default} unless case [1 > 2 [{nope}] 3 > 4 [{not this either}]]
;     >> print x
;     default
;
; !!! Temporarily, the lib context contains a hybridized variadic UNLESS
; which detects if it thinks the usage is expecting the "old unless" style.
; This makes it a bit strange, but allows it to alert people used to the
; old habits about the new behavior.

(
    20 == (10 unless 20)
)(
    _ == (10 unless _) ;-- BLANK! is considered a value (use OPT if not)
)(
    10 == (10 unless null)
)(
    x: 10 + 20 unless case [
        false [<no>]
        false [<nope>]
        false [<nada>]
    ]
    x == 30
)(
    x: 10 + 20 unless case [
        false [<no>]
        true [<yip!>]
        false [<nada>]
    ]
    x == <yip!>
)
