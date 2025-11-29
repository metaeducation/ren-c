; %parse-not.test.r
;
; Historically NOT was actually "NOT AHEAD".
;
; Ren-C rethinks this to make NOT a "combinator modifier" that is able to
; negate the beahavior of some combinators, to make them more useful.  e.g.
; when parsing a block, a NOT combinator of a single element can be taken
; to mean you want to match a single item that is not the one given:
;
;     >> parse ["alpha"] [not "beta"]
;     == "alpha"
;
; The approach has limits--as not all combinators are sensibly negatable.  If
; you are string parsing instead of block parsing, you can use NOT with single
; characters, but not with strings:
;
;     >> parse "alpha" [some not #q]
;     == #a  ; the final #a in "alpha", which matched [not #q]
;
;     >> parse "alpha" [not "beta"]
;     ** Error: TEXT! combinator cannot be negated on string input
;
; The problem in the string case is that it's not semantically clear how much
; input to skip.  Should it be four characters, because "beta" is four?  Or
; should it be one character, in case you were matching against "xbeta" and
; you're one step away from matching?  So not all combinators offer the
; option of negatability.
;
;   https://forum.rebol.info/t/1536/2

[
    ("a" = parse "a" [[ahead "a"] "a"])
    ("a" = parse "a" [not not "a"])
]

[#1246
    ("1" = parse "1" [not not "1"])
    ("1" = parse "1" [not ahead [not ahead "1"] "1"])

    ~parse-mismatch~ !! (parse "" [not ahead repeat 0 "a"])
    ~parse-mismatch~ !! (parse "" [not ahead [repeat 0 "a"]])
]

[#1240
    ('~#not~ = lift parse "" [not ahead "a"])
    ('~#not~ = lift parse "" [not ahead next])
    ('~#not~ = lift parse "" [not ahead veto])
]

[
    ~parse-mismatch~ !! (parse [] [not <end>])
    ~parse-mismatch~ !! (parse [a] [not ahead next])
    ~parse-mismatch~ !! (parse [a] [not ahead one one])

    ('a = parse [a] [not 'b])
    ('a = parse [a] [not ahead ['b] 'a])
    (
        wb: ['b]
        'a = parse [a] [not ahead wb 'a]
    )
    (~#not~ = parse [a a] [not ahead [some 'b] ^ to <end>])

    ~parse-mismatch~ !! (parse [a a] [not ahead ['a 'a] to <end>])
]

[
    ~parse-mismatch~ !! (parse "" [not <end>])
    ~parse-mismatch~ !! (parse "a" [not ahead one])
    ~parse-mismatch~ !! (parse "a" [not ahead next next])

    (#a = parse "a" [not #b])
    (#a = parse "a" [not ahead [#b] #a])
    (
        wb: [#b]
        #a = parse "a" [not ahead wb #a]
    )
    (~#not~ = parse "aa" [not ahead [some #b] ^ to <end>])

    ~parse-mismatch~ !! (parse "aa" [not ahead [#a #a] to <end>])
]

[
    ~parse-mismatch~ !! (parse #{} [not <end>])
    ~parse-mismatch~ !! (parse #{0A} [not ahead one])
    ~parse-mismatch~ !! (parse #{0A} [not ahead next one])

    (#{0A} = parse #{0A} [not ahead #{0B} #{0A}])
    (#{0A} = parse #{0A} [not ahead [#{0B}] #{0A}])
    (
        wb: [#b]
        #{0A} = parse #{0A} [not ahead wb #{0A}]
    )
    (~#not~ = parse #{0A0A} [not ahead [some #{0B}] ^ to <end>])

    ~parse-mismatch~ !! (parse #{0A0A} [not ahead [#{0A} #{0A}] to <end>])
]
