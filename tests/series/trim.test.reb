; functions/series/trim.r

; refinement order
[#83
    (strict-equal? trim/all/with "a" "a" trim/with/all "a" "a")
]

[#1948
    ("foo^/" == trim "  foo ^/")
]

(#{BFD3} == trim #{0000BFD30000})
(#{10200304} == trim/with #{AEAEAE10200304BDBDBD} #{AEBD})

; Incompatible refinement errors.
(error? trap [trim/auto/head {}])
(error? trap [trim/auto/tail {}])
(error? trap [trim/auto/lines {}])
(error? trap [trim/auto/all {}])
(error? trap [trim/all/head {}])
(error? trap [trim/all/tail {}])
(error? trap [trim/all/lines {}])
(error? trap [trim/auto/with {} {*}])
(error? trap [trim/head/with {} {*}])
(error? trap [trim/tail/with {} {*}])
(error? trap [trim/lines/with {} {*}])

("a  ^/  b  " == trim/head "  a  ^/  b  ")
("  a  ^/  b" == trim/tail "  a  ^/  b  ")
("foo^/^/bar^/" == trim "  foo  ^/ ^/  bar  ^/  ^/  ")
("foobar" == trim/all "  foo  ^/ ^/  bar  ^/  ^/  ")
("foo bar" == trim/lines "  foo  ^/ ^/  bar  ^/  ^/  ")
("x^/" == trim/auto "^/  ^/x^/")
("x^/" == trim/auto "  ^/x^/")
("x^/y^/ z^/" == trim/auto "  x^/ y^/   z^/")

([a b] == trim [a b])
([a b] == trim [a b _])
([a b] == trim [_ a b _])
([a _ b] == trim [_ a _ b _])
([a b] == trim/all [_ a _ b _])
([_ _ a _ b] == trim/tail [_ _ a _ b _ _])
([a _ b _ _] == trim/head [_ _ a _ b _ _])
([a _ b] == trim/head/tail [_ _ a _ b _ _])
