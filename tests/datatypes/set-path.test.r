(set-tuple? first [a.b:])
(not set-tuple? 1)
(set-tuple? first [a.b:])
; the minimum
[#1947
    (set-tuple? transcode:one "[a].1:")
]

("a.b:" = mold first [a.b:])

; set-tuples are active
(
    a: make object! [b: null]
    a.b: 5
    5 = a.b
)
[#1 (
    o: make object! [a: 0x0]
    o.a.x: 71830
    o.a.x = 71830
)]
; set-tuple evaluation order
(
    a: 1x2
    a.x: (a: [x 4] 3)
    any [
        a = 3x2
        a = [x 3]
    ]
)
[#64 (
    blk: [1]
    i: 1
    blk.(i): 2
    blk = [2]
)]


; Typically SET and GET evaluate GROUP!s in paths, vs. treat them "as-is".
; But various circumstances like using GROUP! as map keys warrant it.  This
; can be done via POKE or with the BLOCK! form of SET and GET.
;
; The block form is also used to avoid double-evaluations in things like
; DEFAULT.  The groups are evaluated and captured as a block of steps in the
; GET pass, and that block of inert elements is reused on the SET pass.
(
   counter: 0
   obj: make object! [x: null]
   obj.(counter: counter + 1 'x): default [<thing>]
   all [
       obj.x = <thing>
       counter = 1
   ]
)(
    m: make map! 10
    set @[m (1 + 2)] <hard>
    all [
        <hard> = pick m the (1 + 2)
        <hard> = get @[m (1 + 2)]
    ]
)
