; functions/series/change.r
(
    blk1: at copy [1 2 3 4 5] 3
    blk2: at copy [1 2 3 4 5] 3
    change/part blk1 6 -2147483647
    change/part blk2 6 -2147483648
    equal? head of blk1 head of blk2
)
[#9
    (equal? "tr" change/part "str" "" 1)
]

(
    s: copy "abc"
    all [
        "bc" = change s "-"
        s = "-bc"
    ]
)
(
    s: copy "abc"
    all [
        "bc" = change s "ò"
        s = "òbc"
    ]
)
(
    s: copy "abc"
    all [
        "c" = change s "--"
        s = "--c"
    ]
)
(
    s: copy "abc"
    all [
        "" = change s "----"
        s = "----"
    ]
)

(
    s: copy [a b c]
    all [
        [b c] = change s '-
        s = [- b c]
    ]
)
(
    s: copy [a b c]
    all [
        [c] = change s spread [- -]
        s = [- - c]
    ]
)
(
    s: copy [a b c]
    all [
        [] = change s spread [- - - -]
        s = [- - - -]
    ]
)

(
    s: copy #{0A0B0C}
    all [
        #{0B0C} = change s #{11}
        s = #{110B0C}
    ]
)
(
    s: copy #{0A0B0C}
    all [
        #{0C} = change s #{1111}
        s = #{11110C}
    ]
)
(
    s: copy #{0A0B0C}
    all [
        #{} = change s #{11111111}
        s = #{11111111}
    ]
)


(
    x: "abcd"
    change next x "1111"
    x = "a1111"
)

(
    x: ""
    all [
        "" = change x "xyz"
        x = "xyz"
    ]
)

[#490 (
    data: "C# Rules"
    change/part data "REBOL" 2
    data = "REBOL Rules"
)]

; !!! FORMAT isn't really tested, but it's built on CHANGE.

(
    "ò " = format 2 "ò"
)
