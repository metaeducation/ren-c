; functions/control/if.r
(
    success: null
    if okay [success: okay]
    success
)
(
    success: okay
    if null [success: null]
    success
)
(1 = if okay [1])

(null? if null [])
(void? when null [])
(trash? if okay [])

(error? if okay [sys/util/rescue [1 / 0]])
; RETURN stops the evaluation
(
    f1: func [] [
        if okay [return 1 2]
        return 2
    ]
    1 = f1
)
; condition datatype tests; action
(if get 'abs [okay])
; binary
(if #{00} [okay])
; bitset
(if make bitset! "" [okay])

(not error? sys/util/rescue [if [] [okay]])  ; no error (CELL_FLAG_UNEVALUATED removed)
(if identity [] [okay])

; datatype
(if blank! [okay])
; typeset
(if any-number! [okay])
; date
(if 1/1/0000 [okay])
; decimal
(if 0.0 [okay])
(if 1.0 [okay])
(if -1.0 [okay])
; email
(if me@rt.com [okay])
(if %"" [okay])
(if does [] [okay])
(if first [:first] [okay])
(if #"^@" [okay])
; integer
(if 0 [okay])
(if 1 [okay])
(if -1 [okay])
(if #a [okay])
(if first ['a/b] [okay])
(if first ['a] [okay])
(if okay [okay])
(null? if null [okay])
(if $1 [okay])
(if (specialize 'of [property: 'type]) [okay])
(if make object! [] [okay])
(if get '+ [okay])
(if 0x0 [okay])
(if first [()] [okay])
(if 'a/b [okay])
(if make port! http:// [okay])
(if /a [okay])
(if first [a/b:] [okay])
(if first [a:] [okay])
(if "" [okay])
(if to tag! "" [okay])
(if 0:00 [okay])
(if 0.0.0 [okay])
(if  http:// [okay])
(if 'a [okay])

; recursive behaviour

(trash? if okay [if null [1]])
(1 = if okay [if okay [1]])

; infinite recursion
(
    blk: [if okay blk]
    error? sys/util/rescue blk
)

; IF-NOT is for performance minded cases, or cases where one doesn't want to
; deal with the fact that equality completes left, e.g.:
;
;     if not x > 10 [...] => if (not x) > 10 [...]
;     if-not x > 10 [...] => if not (x > 10) [...]
;

(
    success: null
    if-not null [success: okay]
    success
)
(
    success: okay
    if-not okay [success: null]
    success
)
(1 = if-not null [1])

(null? if-not okay [1])
(trash? if-not null [])

(error? if-not null [sys/util/rescue [1 / 0]])

; RETURN stops the evaluation
(
    f1: func [] [
        if-not null [return 1 2]
        return 2
    ]
    1 = f1
)
