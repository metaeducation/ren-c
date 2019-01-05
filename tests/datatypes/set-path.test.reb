; datatypes/set-path.r
(set-path? first [a/b:])
(not set-path? 1)
(set-path! = type of first [a/b:])
; the minimum
[#1947
    (set-path? load "#[set-path! [[a] 1]]")
]

;; ANY-PATH! are no longer positional
;;
;;(
;;    all [
;;        set-path? a: load "#[set-path! [[a b c] 2]]"
;;        2 == index? a
;;    ]
;;)

("a/b:" = mold first [a/b:])
; set-paths are active
(
    a: make object! [b: _]
    a/b: 5
    5 == a/b
)
[#1 (
    o: make object! [a: 0x0]
    o/a/x: 71830
    o/a/x = 71830
)]
; set-path evaluation order
(
    a: 1x2
    a/x: (a: mutable [x 4] 3)
    any [
        a == 3x2
        a == [x 3]
    ]
)
[#64 (
    blk: mutable [1]
    i: 1
    blk/:i: 2
    blk = [2]
)]


;; Typically SET and GET want to avoid evaluating GROUP!s in paths, but if
;; but if you pre-compose them and use /HARD it allows it.  This enables
;; functions like DEFAULT to avoid double-evaluation.
(
   counter: 0
   obj: make object! [x: _]
   obj/(counter: counter + 1 'x): default [<thing>]
   did all [
       obj/x = <thing>
       counter = 1
   ]
)(
    m: make map! 10
    set/hard 'm/(1 + 2) <hard>
    did all [
        <hard> = pick m lit (1 + 2)
        <hard> = get/hard 'm/(1 + 2)
    ]
)


