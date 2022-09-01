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
    (did s: copy {})

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
([a _ b] = trim/head/tail [_ _ a _ b _ _])
