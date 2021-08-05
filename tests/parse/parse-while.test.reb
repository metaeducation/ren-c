; %parse-while.test.reb
;
; SOME and WHILE have become value-bearing; giving back the result of the
; last successful call to the parser they are parameterized with.
;
; Note: currently a WHILE that never matches leads to errors since it is a
; ~void~ isotope in that case (succeeds but reified invisible).  UPARSE is
; not distinguishing at the moment between reified invisibility and total
; invisbility, so that issue has to be tackled before this test works:
;
;     x: ~
;     did all [
;         uparse? "aaa" [x: while "b", while "a"]
;         x = null
;     ]

[(
    x: ~
    did all [
        uparse? "aaa" [x: while "a"]
        x = "a"
    ]
)]

; A WHILE that never actually has a succeeding rule gives back a match that is
; a void isotope, as do 0-iteration REPEAT and INTEGER! rules.
;
; !!! These isotopes currently mean invisibility (in the evaluator, these cases
; would wind up becoming ~stale~ isotopes).  Review the difference.
[
    ("a" = uparse "a" ["a" while "b"])
    ("a" = uparse "a" ["a" [while "b"]])
]

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    uparse "a" [while [(i: i + 1 j: if i = 2 [[end skip]]) j]]
    i == 2
)

[#1268 (
    i: 0
    <infinite?> = catch [
        uparse "a" [while [(i: i + 1) (if i > 100 [throw <infinite?>])]]
    ]
)(
    i: 0
    uparse "a" [while [(i: i + 1 j: try if i = 2 [[false]]) j]]
    i == 2
)]
