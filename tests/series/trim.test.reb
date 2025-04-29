; functions/series/trim.r

; refinement order
[#83
    (equal? trim/all/with "a" "a" trim/with/all "a" "a")
]

[#1948
    ("foo^/" = trim "  foo ^/")
]

(#{BFD3} = trim #{0000BFD30000})
(#{10200304} = trim/with #{AEAEAE10200304BDBDBD} #{AEBD})

; Incompatible refinement errors.
(error? sys/util/rescue [trim/auto/head {}])
(error? sys/util/rescue [trim/auto/tail {}])
(error? sys/util/rescue [trim/auto/lines {}])
(error? sys/util/rescue [trim/auto/all {}])
(error? sys/util/rescue [trim/all/head {}])
(error? sys/util/rescue [trim/all/tail {}])
(error? sys/util/rescue [trim/all/lines {}])
(error? sys/util/rescue [trim/auto/with {} {*}])
(error? sys/util/rescue [trim/head/with {} {*}])
(error? sys/util/rescue [trim/tail/with {} {*}])
(error? sys/util/rescue [trim/lines/with {} {*}])

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
