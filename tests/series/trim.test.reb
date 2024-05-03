; functions/series/trim.r

; refinement order
[#83
    (strict-equal?
        trim/all/with "a" "a"
        trim/with/all "a" "a"
    )
]

[#1948
    ("foo^/" = trim "  foo ^/")
]

(#{BFD3} = trim #{0000BFD30000})
(#{10200304} = trim/with #{AEAEAE10200304BDBDBD} #{AEBD})

; Incompatible refinement errors.
[
    (did s: copy "")

    ~bad-refines~ !! (trim/auto/head s)
    ~bad-refines~ !! (trim/auto/tail s)
    ~bad-refines~ !! (trim/auto/lines s)
    ~bad-refines~ !! (trim/auto/all s)
    ~bad-refines~ !! (trim/all/head s)
    ~bad-refines~ !! (trim/all/tail s)
    ~bad-refines~ !! (trim/all/lines s)
    ~bad-refines~ !! (trim/auto/with s {*})
    ~bad-refines~ !! (trim/head/with s {*})
    ~bad-refines~ !! (trim/tail/with s {*})
    ~bad-refines~ !! (trim/lines/with s {*})

    (s = {})
]

("a  ^/  b  " = trim/head "  a  ^/  b  ")
("  a  ^/  b" = trim/tail "  a  ^/  b  ")
("foo^/^/bar^/" = trim "  foo  ^/ ^/  bar  ^/  ^/  ")
("foobar" = trim/all "  foo  ^/ ^/  bar  ^/  ^/  ")
("foo bar" = trim/lines "  foo  ^/ ^/  bar  ^/  ^/  ")
("x^/" = trim/auto "^/  ^/x^/")
("x^/" = trim/auto "  ^/x^/")
("x^/ y^/ z^/" = trim/auto "  x^/ y^/   z^/")
("x^/y" = trim/auto "^/^/  x^/  y")

([a b] = trim [a b])
([a b] = trim [a b _])
([a b] = trim [_ a b _])
([a _ b] = trim [_ a _ b _])
([a b] = trim/all [_ a _ b _])
([_ _ a _ b] = trim/tail [_ _ a _ b _ _])
([a _ b _ _] = trim/head [_ _ a _ b _ _])
([a _ b] = trim/head/tail [_ _ a _ b _ _])  ; Red result is [1 2]


; From Red's %tests/source/units/series-test.red

(
    str: " ^(A0) ^-a b  ^- c  ^(2000) "
    "a b  ^- c" = trim copy str
)


[
    (
        mstr: {   a ^-1^/    ab2^-  ^/  ac3  ^/  ^/^/}
        true
    )

    ("a ^-1^/ab2^/ac3^/" = trim copy mstr)
    ("a ^-1^/    ab2^-  ^/  ac3  ^/  ^/^/" = trim/head copy mstr)
    ("   a ^-1^/    ab2^-  ^/  ac3" = trim/tail copy mstr)
    ("a ^-1^/    ab2^-  ^/  ac3" = trim/head/tail copy mstr)
    ("a 1 ab2 ac3" = trim/lines copy mstr)
    ("a1ab2ac3" = trim/all copy mstr)

    ; Note: Ren-C semantics of TRIM/WITH do not presume you mean spaces are
    ; also included.  So if your /WITH is "ab" then the line itself has to
    ; start with "ab" not "  ab"
    ;
    ; ("    ^-1^/    2^-  ^/  c3  ^/  ^/^/" = trim/with copy mstr "ab")
    ; ("    ^-1^/    b2^-  ^/  c3  ^/  ^/^/" = trim/with copy mstr #"a")
    ; ("    ^-1^/    b2^-  ^/  c3  ^/  ^/^/" = trim/with copy mstr 97)
]

("a1ab2ac3" = trim/all { a ^-1^/ ab2^- ^/ ac3 ^/ ^/^/})

; https://github.com/red/red/issues/5076
(2 = length of trim first split {х^/+й} "+")

[
    (#{} = trim #{00})
    (#{1234} = trim #{000012340000})
    (#{12340000} = trim/head #{000012340000})
    (#{00001234} = trim/tail #{000012340000})
    (#{} = trim/tail #{000000})
    (#{11} = head trim/tail at #{11000000} 1)
    (#{11} = head trim/tail at #{11000000} 2)
    (#{1100} = head trim/tail at #{11000000} 3)
    (#{110000} = head trim/tail at #{11000000} 4)
    (#{11000000} = head trim/tail at #{11000000} 5)
    (#{} = trim/head #{000000})
    (#{} = trim at #{11000000} 2)
]
