; %uparse2.test.reb
;
; UPARSE2 should have a more comprehensive test as part of Redbol, but until
; that is done here are just a few basics to make sure it's working at all.

(true = uparse2? "aaa" [some "a"])
(true = uparse2? "aaa" [any "a"])
(true = uparse2? ["aaa"] [into any "a"])
(true = uparse2? "aaabbb" [
    pos: some "a" some "b" :pos some "a" some "b"
])
(
    x: null
    did all [
        true = uparse2? "aaabbbccc" [to "b" copy x to "c" some "c"]
        x = "bbb"
    ]
)
(
    x: null
    did all [
        true = uparse2? "aaabbbccc" [to "b" set x some "b" thru <end>]
        x = #"b"
    ]
)


; These BREAK tests from %parse-test.red show that their notion of BREAK is
; kind of incoherent.  We could make a compatible BREAK that tolerates not
; being in an iterative rule, but that doesn't seem useful.
;
; Alternative meanings of ACCEPT could take the place of this, if it made
; sense.
[
    ; (uparse2? [] [break])
    ; (not uparse2? [a] [break])
    ; (uparse2? [a] [[break 'b] 'a])
    ; (uparse2? [a] [['b | break] 'a])

    (uparse2? [a a] [some ['b | break] 2 'a])
    (uparse2? [a a] [some ['b | [break]] 2 'a])

    ; (uparse2? "" [break])
    ; (not uparse2? "a" [break])
    ; (uparse2? "a" [[break #b] #a])
    ; (uparse2? "a" [[#b | break] #a])

    (uparse2? "aa" [some [#b | break] 2 #a])
    (uparse2? "aa" [some [#b | [break]] 2 #a])

    ; (uparse2? #{} [break])
    ; (not uparse2? #{0A} [break])
    ; (uparse2? #{0A} [[break #{0B}] #{0A}])
    ; (uparse2? #{0A} [[#{0B} | break] #{0A}])

    (uparse2? #{0A0A} [some [#{0B} | break] 2 #{0A}])
    (uparse2? #{0A0A} [some [#{0B} | [break]] 2 #{0A}])

    ; These tests suggest the BREAK is breaking out of the `|` rule and then
    ; making no progress?  (!)  Little of this made sense.
    ;
    ; (not uparse2? "aa" [some [#b | 2 [#c | break]] 2 #a])
    ; (not uparse2? [a a] [some ['b | 2 ['c | break]] 2 'a])
    ; (not uparse2? #{0A0A} [some [#{0B} | 2 [#"^L" | break]] 2 #{0A}])
]


; These tests are also from %parse-test.red, but like with the BREAK tests
; we are not currently emulating tolerance of the use of REJECT outside of
; an iterating rule.  That's possible, but the semantics are unclear.
[
    ; (not uparse2? [] [reject])
    ; (not uparse2? [a] [reject 'a])
    ; (
    ;     wa: ['a]
    ;     not uparse2? [a] [reject wa]
    ; )
    ; (not uparse2? [a] [[reject] 'a])
    ; (uparse2? [a] [[reject 'b] | 'a])
    ; (not uparse2? [a] [['b | reject] 'a])
    ; (uparse2? [a] [['b | reject] | 'a])

    (uparse2? [a a] [some reject | 2 'a])
    (uparse2? [a a] [some [reject] | 2 'a])

    ; (not uparse2? "" [reject])
    ; (not uparse2? "a" [reject #a])
    ; (
    ;     wa: [#a]
    ;     not uparse2? "a" [reject wa]
    ; )
    ; (not uparse2? "a" [[reject] #a])
    ; (uparse2? "a" [[reject #b] | #a])
    ; (not uparse2? "a" [[#b | reject] #a])
    ; (uparse2? "a" [[#b | reject] | #a])

    (uparse2? "aa" [some reject | 2 #a])
    (uparse2? "aa" [some [reject] | 2 #a])

    ; (not uparse2? #{} [reject])
    ; (not uparse2? #{0A} [reject #{0A}])
    ; (
    ;     wa: [#{0A}]
    ;     not uparse2? #{0A} [reject wa]
    ; )
    ; (not uparse2? #{0A} [[reject] #{0A}])
    ; (uparse2? #{0A} [[reject #{0B}] | #{0A}])
    ; (not uparse2? #{0A} [[#{0B} | reject] #{0A}])
    ; (uparse2? #{0A} [[#{0B} | reject] | #{0A}])

    (uparse2? #{0A0A} [some reject | 2 #{0A}])
    (uparse2? #{0A0A} [some [reject] | 2 #{0A}])
]

; UPARSE2's INTEGER! combinator breaks the pattern by quoting.
[
    (not uparse2? [a a] [1 1 ['a]])
    (uparse2? [a a] [1 2 ['a]])
    (uparse2? [a a] [2 2 ['a]])
    (uparse2? [a a] [2 3 ['a]])
    (not uparse2? [a a] [3 4 ['a]])
    (uparse2? [a a] [1 2 'a])
    (uparse2? [a a] [2 2 'a])
    (uparse2? [a a] [2 3 'a])
    (not uparse2? [a a] [3 4 'a])
]
