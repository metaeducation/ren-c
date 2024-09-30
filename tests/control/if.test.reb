; functions/control/if.r
(
    success: 'no
    if ok [success: 'yes]
    yes? success
)
(
    success: 'yes
    if null [success: 'no]
    yes? success
)
(1 = if ok [1])

(^void = ^ if null [])
('~[~void~]~ = ^ if ok [])

(error? if ok [trap [1 / 0]])
; RETURN stops the evaluation
(
    f1: func [return: [integer!]] [
        if ok [return 1 2]
        2
    ]
    1 = f1
)
; condition datatype tests; action
(if abs/ [okay])
; binary
(if #{00} [okay])
; bitset
(if make bitset! "" [okay])

; literal blocks were once illegal in Ren-C as conditions, but that check
; was based on CELL_FLAG_UNEVALUATED, which was removed when blocks became
; "evaluative" for their bindings.
[
    (if [] [okay])
    (if identity [] [okay])
]

; datatype
(if blank! [okay])
; typeset
(if &any-number? [okay])
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
; integer
(if 0 [okay])
(if 1 [okay])
(if -1 [okay])
(if #a [okay])
(if first ['a/b] [okay])
(if first ['a] [okay])
(if ok [okay])
(^void = ^ if null [okay])
(if $1 [okay])
(if (specialize of/ [property: 'type]) [okay])
(okay = if blank [okay])
(if make object! [] [okay])
(if +/ [okay])
(if 0x0 [okay])
(if first [()] [okay])
(if 'a/b [okay])
(if make port! http:// [okay])
(if /a [okay])
(if first [a.b:] [okay])
(if first [a:] [okay])
(if "" [okay])
(if to tag! "" [okay])
(if 0:00 [okay])
(if 0.0.0 [okay])
(if  http:// [okay])
(if 'a [okay])

; recursive behaviour

('~[~void~]~ = ^ if ok [if null [1]])
(void? if ok [2 if null [1]])
(1 = if ok [if ok [1]])

; infinite recursion
(<deep-enough> = catch wrap [
    depth: 0
    blk: [depth: me + 1, if depth = 1000 [throw <deep-enough>], if ok (blk)]
    eval blk
])

(
    success: 'no
    if no? 'no [success: 'yes]
    yes? success
)
(
    success: 'yes
    if no? 'yes [success: 'no]
    yes? success
)
(1 = if false? 'false [1])

(^void = ^ if not okay [1])
('~[~null~]~ = ^ if no? 'no [null])

(error? if off? 'off [trap [1 / 0]])

; RETURN stops the evaluation
(
    f1: func [return: [integer!]] [
        if not null [return 1 2]
        2
    ]
    1 = f1
)

; Soft Quoted Branching
; https://forum.rebol.info/t/soft-quoted-branching-light-elegant-fast/1020
(
    [1 + 2] = if ok '[1 + 2]
)(
    1020 = if ok '1020
)

; THE-XXX! Branching
;
; !!! At one point this allowed cases of @word, @pa/th, and @tu.p.le as a
; shorthand for `if xxx [:word]` etc.
;
;    j: 304
;    304 = if ok @j
;
;    o: make object! [b: 1020]
;    1020 = if ok @o/b
;
; Might be good for code golf?  (Even better would be to wedge in tailored
; support for plain WORD! etc.)
[(
    x: ~
    all [
        <else> = if ok [x: <branch>, null] *else [<else>]
        x = <branch>
    ]
)

; Feature being considered: @(...) groups do not run unconditionally to make
; branch arguments, but only if the branch is taken.
;(
;    var: <something>
;    all [
;        void? if null @(var: <something-else> [null])
;        var = <something>
;        null? if ok @(var: <something-else> [null])
;        var = <something-else>
;    ]
;)
]
