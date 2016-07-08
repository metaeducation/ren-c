; functions/control/if.r
[
    success: false
    if true [success: true]
    success
]
[
    success: true
    if false [success: false]
    success
]
[1 = if true [1]]
[void? if true []]
[error? if true [try [1 / 0]]]
; RETURN stops the evaluation
[
    f1: does [
        if true [return 1 2]
        2
    ]
    1 = f1
]
; condition datatype tests; action
[if get 'abs [true]]
; binary
[if #{00} [true]]
; bitset
[if make bitset! "" [true]]
; block
[if [] [true]]
; datatype
[if blank! [true]]
; typeset
[if any-number! [true]]
; date
[if 1/1/0000 [true]]
; decimal
[if 0.0 [true]]
[if 1.0 [true]]
[if -1.0 [true]]
; email
[if me@rt.com [true]]
[if %"" [true]]
[if does [] [true]]
[if first [:first] [true]]
[if #"^@" [true]]
[if make image! 0x0 [true]]
; integer
[if 0 [true]]
[if 1 [true]]
[if -1 [true]]
[if #a [true]]
[if first ['a/b] [true]]
[if first ['a] [true]]
[if true [true]]
[void? if false [true]]
[if $1 [true]]
[if :type-of [true]]
[void? if blank [true]]
[if make object! [] [true]]
[if get '+ [true]]
[if 0x0 [true]]
[if first [()] [true]]
[if 'a/b [true]]
[if make port! http:// [true]]
[if /a [true]]
[if first [a/b:] [true]]
[if first [a:] [true]]
[if "" [true]]
[if to tag! "" [true]]
[if 0:00 [true]]
[if 0.0.0 [true]]
[if  http:// [true]]
[if 'a [true]]
; recursive behaviour
[void? if true [if false [1]]]
[1 = if true [if true [1]]]
; infinite recursion
[
    blk: [if true blk]
    error? try blk
]
