; functions/convert/mold.r
; cyclic block
[#860 #6 (
    a: copy []
    insert/only a a
    text? mold a
)]
; cyclic paren
(
    a: mutable first [()]
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
        if error? trap [
            loop n [a: append/only copy [] a]
            mold a
        ] [throw true]
        n: n * 2
    ]]
)]
[#719
    ("()" = mold lit ())
]

[#77
    ("#[block! [[1 2] 2]]" == mold/all next [1 2])
]
[#77
    (null? find mold/flat make object! [a: 1] "    ")
]

[#84
    (equal? mold make bitset! "^(00)" "make bitset! #{80}")
]
[#84
    (equal? mold/all make bitset! "^(00)" "#[bitset! #{80}]")
]


;-- NEW-LINE markers

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
    mold block = {[^/    a b c]}
)

(
    block: [a b c
    ]
    mold block = {[a b c^/]}
)

(
    block: [a b
        c
    ]
    mold block = {[a b^/    c^/]}
)

(
    block: copy [a b c]
    new-line block true
    new-line tail block true
    append block [d e f]
    mold block = {[^/    a b c^/    d e f]}
)

(
    block: copy [a b c]
    new-line block true
    new-line tail block true
    append/line block [d e f]
    mold block = {[^/    a b c^/    d e f^/]}
)

(
    block: copy []
    append/line block [d e f]
    mold block = {[^/    d e f^/]}
)

(
    block: copy [a b c]
    new-line block true
    new-line tail block true
    append/line block [d e f]
    mold block = {[^/    a b c^/    d e f^/]}
)

[#145 (
    test-block: [a b c d e f]
    set 'f func [
        /local buff
    ][
        buff: copy ""
        for-each val test-block [
            loop 5000 [
                append buff form reduce [reduce [<td> 'OK </td>] cr lf]
            ]
        ]
        buff
    ]
    f
    recycle
    true
)]
