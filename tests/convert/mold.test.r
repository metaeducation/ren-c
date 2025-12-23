; functions/convert/mold.r
; cyclic block
[#860 #6 (
    a: copy []
    insert a a
    text? mold a
)]
; cyclic paren
(
    a: first [()]
    insert a a
    text? mold a
)
; cyclic object
[#69 (
    a: make object! [a: binding of $a]
    text? mold a
)]

; deep nested block mold
;[#876
;    ~stack-overflow~ !! (
;        n: 1
;        catch [forever [
;            a: copy []
;            repeat n [a: append copy [] a]
;            mold a
;            n: n * 2
;        ]]
;    )
;]

[#719
    ("()" = mold the ())
]

[#77
    (null? find mold:flat make object! [a: 1] "    ")
]

[#84
    (equal? mold (make bitset! #{80}) "&[bitset! #{80}]")
]


; NEW-LINE markers

[
    (did block: copy [a b c])

    (
        -[[a b c]]- = mold block
    )(
        new-line block 'yes
        -[[^/    a b c]]- = mold block
    )(
        new-line (tail of block) 'yes
        -[[^/    a b c^/]]- = mold block
    )(
        -[[^/]]- = mold tail-of block
    )
]

(
    block: [
        a b c]
    -[[^/    a b c]]- = mold block
)

(
    block: [a b c
    ]
    -[[a b c^/]]- = mold block
)

(
    block: [a b
        c
    ]
    -[[a b^/    c^/]]- = mold block
)

(
    block: copy [a b c]
    new-line block 'yes
    new-line (tail of block) 'yes
    append block spread [d e f]
    -[[^/    a b c^/    d e f]]- = mold block
)

(
    block: copy [a b c]
    new-line block 'yes
    new-line (tail of block) 'yes
    append:line block spread [d e f]
    -[[^/    a b c^/    d e f^/]]- = mold block
)

(
    block: copy []
    append:line block spread [d e f]
    -[[^/    d e f^/]]- = mold block
)

(
    block: copy [a b c]
    new-line block 'yes
    new-line (tail of block) 'yes
    append:line block spread [d e f]
    -[[^/    a b c^/    d e f^/]]- = mold block
)

[#145 (
    test-block: [a b c d e f]
    f: func [
        {buff}
    ][
        buff: copy ""
        for-each 'val test-block [
            repeat 5000 [
                append buff form reduce [reduce [<td> 'OK </td>] cr lf]
            ]
        ]
        return buff
    ]
    f
    recycle
    ok
)]

; NEW-LINE shouldn't be included on first element of a MOLD SPREAD
;
("a b" = mold spread new-line [a b] 'yes)
("[^/    a b]" = mold new-line [a b] 'yes)

[https://github.com/metaeducation/ren-c/issues/1033 (
    "[^/    1^/    2^/]" = mold new-line:all [1 2] 'yes
)]

[https://github.com/metaeducation/rebol-httpd/issues/10 (
    x: load "--^/a/b"
    all [
        x = [-- a/b]
        not new-line? x
        new-line? next x
        not new-line? next next x
    ]
)(
    x: load "--^/a/b/c"
    all [
        x = [-- a/b/c]
        not new-line? x
        new-line? next x
        not new-line? next next x
    ]
)]


[#2405
    (-["ab]- = mold:limit "abcdefg" 3)
    (
        [str trunc]: mold:limit "abcdefg" 3
        all [
            str = -["ab]-
            trunc = 3
        ]
    )
    (
        [str trunc]: mold:limit "abcdefg" 300
        all [
            str = -["abcdefg"]-
            trunc = null
        ]
    )
]

[
    (null? mold ^ghost)
    ~expect-arg~ !! (mold null)

    (null? form ^ghost)
    ~expect-arg~ !! (form null)
]

(
    string: ""
    for-each 'item [
        ? * + - = |     ; word!s - ordinary
        < > : / . %     ; word!s - special (limited in some contexts)
        #               ; rune! - "hash"/"octothorpe"/"pound"
        _               ; rune! - "space"
        $               ; tied! - tied "space" rune (a "tie")
        ^               ; metaform! - meta "space" rune (a "meta")
        @               ; pinned! - pinned "space" rune (a "pin")
        ~               ; quasiform! - quasi "space" rune (a "quasar")
        ,               ; comma!
    ][
        append string mold item
    ]
    assert [string = "?*+-=<>|:/.%#_$^^@~,"]
)
