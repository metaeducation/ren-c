; functions/math/complement.r
;
; Historical Rebol unified bitwise not with "set complement" operations.
;
; But the idea of folding together the set operations with bitwise operations
; runs into trouble when you have something that can be treated with both
; methods: e.g. a BINARY!.  If the "intersection" of #{0102} and #{0203} is
; to be #{02}, then that must be distinct from bitwise AND.  This leads all
; the set-based operations to separate from the bitswise ones.

; logic
[#849
    (false = not+ true)
]
(true = not+ false)

; integer
(-1 = not+ 0)
(0 = not+ -1)
(2147483647 = not+ -2147483648)
(-2147483648 = not+ 2147483647)

; tuple
(255.255.255 = not+ 0.0.0)
(0.0.0 = not+ 255.255.255)

; binary
(#{ffffffffff} = not+ #{0000000000})
(#{0000000000} = not+ #{ffffffffff})

; bitset
(not find complement charset "b" #"b")
(did find complement charset "a" #"b")

(
    a: make bitset! #{0000000000000000000000000000000000000000000000000000000000000000}
    a == complement complement a
)
(
    a: make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF}
    a == complement complement a
)
