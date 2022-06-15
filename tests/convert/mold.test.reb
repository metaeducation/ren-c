; functions/convert/mold.r
; cyclic block
[#860 #6 (
    a: copy []
    insert/only a a
    text? mold a
)]
; cyclic paren
(
    a: first [()]
    insert/only a a
    text? mold a
)
; cyclic object
[#69 (
    a: make object! [a: binding of 'a]
    text? mold a
)]
; deep nested block mold
[#876 (
    n: 1
    catch [forever [
        a: copy []
        if error? trap [
            repeat n [a: append/only copy [] a]
            mold a
        ] [throw true]
        n: n * 2
    ]]
)]
[#719
    ("()" = mold the ())
]

[#77
    ("#[block! [[1 2] 2]]" == mold/all next [1 2])
]
[#77
    (null? find mold/flat make object! [a: 1] "    ")
]

[#84
    (equal? mold/all (make bitset! #{80}) "#[bitset! #{80}]")
]


; NEW-LINE markers

[
    (did block: copy [a b c])

    (
        {[a b c]} = mold block
    )(
        new-line block true
        {[^/    a b c]} = mold block
    )(
        new-line tail block true
        {[^/    a b c^/]} = mold block
    )(
        {[^/]} = mold tail block
    )
]

(
    block: [
        a b c]
    {[^/    a b c]} = mold block
)

(
    block: [a b c
    ]
    {[a b c^/]} = mold block
)

(
    block: [a b
        c
    ]
    {[a b^/    c^/]} = mold block
)

(
    block: copy [a b c]
    new-line block true
    new-line tail block true
    append block [d e f]
    {[^/    a b c^/    d e f]} = mold block
)

(
    block: copy [a b c]
    new-line block true
    new-line tail block true
    append/line block [d e f]
    {[^/    a b c^/    d e f^/]} = mold block
)

(
    block: copy []
    append/line block [d e f]
    {[^/    d e f^/]} = mold block
)

(
    block: copy [a b c]
    new-line block true
    new-line tail block true
    append/line block [d e f]
    {[^/    a b c^/    d e f^/]} = mold block
)

[#145 (
    test-block: [a b c d e f]
    set 'f func [
        <local> buff
    ][
        buff: copy ""
        for-each val test-block [
            repeat 5000 [
                append buff form reduce [reduce [<td> 'OK </td>] cr lf]
            ]
        ]
        return buff
    ]
    f
    recycle
    true
)]

; NEW-LINE shouldn't be included on first element of a MOLD/ONLY
;
("a b" = mold/only new-line [a b] true)
("[^/    a b]" = mold new-line [a b] true)

[https://github.com/metaeducation/ren-c/issues/1033 (
    "[^/    1^/    2^/]" == mold new-line/all [1 2] true
)]

[https://github.com/metaeducation/rebol-httpd/issues/10 (
    x: load "--^/a/b"
    did all [
        x = [-- a/b]
        not new-line? x
        new-line? next x
        not new-line? next next x
    ]
)(
    x: load "--^/a/b/c"
    did all [
        x = [-- a/b/c]
        not new-line? x
        new-line? next x
        not new-line? next next x
    ]
)]


[#2405
    ({"ab} = mold/limit "abcdefg" 3)
    (
        [str trunc]: mold/limit "abcdefg" 3
        did all [
            str = {"ab}
            trunc = true
        ]
    )
    (
        [str trunc]: mold/limit "abcdefg" 300
        did all [
            str = {"abcdefg"}
            trunc = false
        ]
    )
]
