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
    a: make object! [a: self]
    text? mold a
)]
; deep nested block mold
[#876 (
    n: 1
    catch [forever [
        a: copy []
        if error? sys/util/rescue [
            repeat n [a: append/only copy [] a]
            mold a
        ] [throw okay]
        n: n * 2
    ]]
)]
[#719
    ("()" = mold the ())
]

[#77
    (null? find mold/flat make object! [a: 1] "    ")
]

[#84
    (equal? mold make bitset! #"^(00)" "#[bitset! #{80}]")
]


;-- NEW-LINE markers

[
    (did block: copy [a b c])

    (
        {[a b c]} = mold block
    )(
        new-line block 'yes
        {[^/    a b c]} = mold block
    )(
        new-line tail block 'yes
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
    new-line block 'yes
    new-line tail block 'yes
    append block [d e f]
    {[^/    a b c^/    d e f]} = mold block
)

(
    block: copy [a b c]
    new-line block 'yes
    new-line tail block 'yes
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
    new-line block 'yes
    new-line tail block 'yes
    append/line block [d e f]
    {[^/    a b c^/    d e f^/]} = mold block
)

[#145 (
    test-block: [a b c d e f]
    set 'f func [
        /local buff
    ][
        buff: copy ""
        for-each val test-block [
            repeat 5000 [
                append buff form reduce [reduce [<td> 'OK </td>] cr lf]
            ]
        ]
        buff
    ]
    f
    recycle
    okay
)]
