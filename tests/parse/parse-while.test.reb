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

[
    (uparse? [] [while 'a])
    (uparse? [] [while 'b])
    (uparse? [a] [while 'a])
    (not uparse? [a] [while 'b])
    (uparse? [a] [while 'b <any>])
    (uparse? [a b a b] [while ['b | 'a]])
]

[(
    x: ~
    did all [
        uparse? "aaa" [x: while "a"]
        x = "a"
    ]
)]

; A WHILE that never actually has a succeeding rule gives back a match that is
; a ~none~ isotope, as do 0-iteration REPEAT and INTEGER! rules.
[
    ('~none~ = ^ uparse "a" ["a" while "b"])
    ('~none~ = ^ uparse "a" ["a" [while "b"]])
    ('~none~ = uparse "a" ["a" ^[while "b"]])
]

; This test works in Rebol2 even if it starts `i: 0`, presumably a bug.
(
    i: 1
    uparse "a" [while [(i: i + 1 j: if i = 2 [[<end> skip]]) j]]
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


[
    (uparse? "" [while #a])
    (uparse? "" [while #b])
    (uparse? "a" [while #a])
    (not uparse? "a" [while #b])
    (uparse? "a" [while #b <any>])
    (uparse? "abab" [while [#b | #a]])
]

; WHILE tests from %parse-test.red
[
    (
        x: blank
        true
    )
    (uparse? #{020406} [while [x: across <any> :(even? first x)]])
    (not uparse? #{01} [x: across <any> :(even? first x)])
    (not uparse? #{0105} [some [x: across <any> :(even? first x)]])
    (uparse? #{} [while #{0A}])
    (uparse? #{} [while #{0B}])
    (uparse? #{0A} [while #{0A}])
    (not uparse? #{0A} [while #{0B}])
    (uparse? #{0A} [while #{0B} <any>])
    (uparse? #{0A0B0A0B} [while [#{0B} | #{0A}]])
    (error? trap [uparse? #{} [ahead]])
    (uparse? #{0A} [ahead #{0A} #{0A}])
    (uparse? #{01} [ahead [#{0A} | #"^A"] <any>])
]

[
    (uparse? [a a] [while ['a]])
    (uparse? [a a] [some ['a] while ['b]])
]
