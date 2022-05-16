; %parse-while.test.reb
;
; SOME and WHILE have become value-bearing; giving back the result of the
; last successful call to the parser they are parameterized with.


(
    x: ~
    did all [
        "a" == uparse "aaa" [x: while "b", while "a"]
        none? get/any 'x
    ]
)

[
    (none? uparse [] [while 'a])
    (none? uparse [] [while 'b])
    ('a == uparse [a] [while 'a])
    (didn't uparse [a] [while 'b])
    ('a == uparse [a] [while 'b <any>])
    ('b == uparse [a b a b] [while ['b | 'a]])
]

[(
    x: ~
    did all [
        "a" == uparse "aaa" [x: while "a"]
        x = "a"
    ]
)]

; A WHILE that never actually has a succeeding rule gives back a match that is
; a ~none~ isotope, as do 0-iteration REPEAT and INTEGER! rules.
[
    (none? uparse "a" ["a" while "b"])
    (none? uparse "a" ["a" [while "b"]])
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
    (none? uparse "" [while #a])
    (none? uparse "" [while #b])
    (#a == uparse "a" [while #a])
    (didn't uparse "a" [while #b])
    (#a == uparse "a" [while #b <any>])
    (#b == uparse "abab" [while [#b | #a]])
]

; WHILE tests from %parse-test.red
[
    (
        x: blank
        true
    )
    (#[true] == uparse #{020406} [while [x: across <any> :(even? first x)]])
    (didn't uparse #{01} [x: across <any> :(even? first x)])
    (didn't uparse #{0105} [some [x: across <any> :(even? first x)]])
    (none? uparse #{} [while #{0A}])
    (none? uparse #{} [while #{0B}])
    (#{0A} == uparse #{0A} [while #{0A}])
    (didn't uparse #{0A} [while #{0B}])
    (10 == uparse #{0A} [while #{0B} <any>])
    (#{0B} == uparse #{0A0B0A0B} [while [#{0B} | #{0A}]])
    (error? trap [uparse #{} [ahead]])
    (#{0A} == uparse #{0A} [ahead #{0A} #{0A}])
    (1 == uparse #{01} [ahead [#{0A} | #"^A"] <any>])
]

[
    ('a == uparse [a a] [while ['a]])
    (none? uparse [a a] [some ['a] while ['b]])
]
