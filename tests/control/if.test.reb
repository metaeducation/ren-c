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

((lift void) = ^ if null [])
('~[]~ = ^ if ok [])

(warning? if ok [trap [1 / 0]])
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
(if integer! [okay])
; typeset
(if any-number?/ [okay])
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
((lift void) = ^ if null [okay])
(if (specialize of/ [property: 'type]) [okay])
(okay = if space [okay])
(if make object! [] [okay])
(if +/ [okay])
(if 0x0 [okay])
(if first [()] [okay])
(if 'a/b [okay])
(if make port! http:// [okay])
(if '/a [okay])
(if first [a.b:] [okay])
(if first [a:] [okay])
(if "" [okay])
(if to tag! "" [okay])
(if 0:00 [okay])
(if 0.0.0 [okay])
(if  http:// [okay])
(if 'a [okay])

; recursive behaviour

('~[]~ = lift if ok [if null [1]])
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

((lift void) = ^ if not okay [1])
('~[~null~]~ = ^ if no? 'no [null])

(warning? if off? 'off [trap [1 / 0]])

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
