; functions/control/if.r
(
    success: false
    if true [success: true]
    success
)
(
    success: true
    if false [success: false]
    success
)
(1 = if true [1])

(null? if false [])
('~void~ = ^ if true [])

(error? if true [trap [1 / 0]])
; RETURN stops the evaluation
(
    f1: func [] [
        if true [return 1 2]
        2
    ]
    1 = f1
)
; condition datatype tests; action
(if get 'abs [true])
; binary
(if #{00} [true])
; bitset
(if make bitset! "" [true])

; literal blocks illegal as condition in Ren-C, but evaluation products ok
(error? trap [if [] [true]])
(if identity [] [true])

; datatype
(if blank! [true])
; typeset
(if any-number! [true])
; date
(if 1/1/0000 [true])
; decimal
(if 0.0 [true])
(if 1.0 [true])
(if -1.0 [true])
; email
(if me@rt.com [true])
(if %"" [true])
(if does [] [true])
(if first [:first] [true])
(if make image! 0x0 [true])
; integer
(if 0 [true])
(if 1 [true])
(if -1 [true])
(if #a [true])
(if first ['a/b] [true])
(if first ['a] [true])
(if true [true])
(null? if false [true])
(if $1 [true])
(if (specialize :of [property: 'type]) [true])
(null? if blank [true])
(if make object! [] [true])
(if get '+ [true])
(if 0x0 [true])
(if first [()] [true])
(if 'a/b [true])
(if make port! http:// [true])
(if /a [true])
(if first [a/b:] [true])
(if first [a:] [true])
(if "" [true])
(if to tag! "" [true])
(if 0:00 [true])
(if 0.0.0 [true])
(if  http:// [true])
(if 'a [true])

; recursive behaviour

('~null~ = ^ if true [if false [1]])
(1 = if true [if true [1]])

; infinite recursion
(
    blk: [if true blk]
    error? trap blk
)

(
    success: false
    if not false [success: true]
    success
)
(
    success: true
    if not true [success: false]
    success
)
(1 = if not false [1])

(null? if not true [1])
('~null~ = ^ if not false [null])

(error? if not false [trap [1 / 0]])

; RETURN stops the evaluation
(
    f1: func [] [
        if not false [return 1 2]
        2
    ]
    1 = f1
)

; Soft Quoted Branching
; https://forum.rebol.info/t/soft-quoted-branching-light-elegant-fast/1020
(
    [1 + 2] = if true '[1 + 2]
)(
    1020 = if true '1020
)

; Lit-Branching
(
    j: 304
    304 = if true ^j
)(
    o: make object! [b: 1020]
    1020 = if true ^o/b
)(
    var: <something>
    did all [
        null? if false ^(var: <something-else> [1000 + 20])
        var = <something>
        1020 = if true ^(var: <something-else> [1000 + 20])
        var = <something-else>
    ]
)
