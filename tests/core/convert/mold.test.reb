; functions/convert/mold.r
; bug#860
; bug#6
; cyclic block
[
    a: copy []
    insert/only a a
    string? mold a
]
; cyclic paren
[
    a: first [()]
    insert/only a a
    string? mold a
]
; cyclic object
; bug#69
[
    a: make object! [a: self]
    string? mold a
]
; deep nested block mold
; bug#876
[
    n: 1
    forever [
        a: copy []
        if error? try [
            loop n [a: append/only copy [] a]
            mold a
        ] [break/return true]
        n: n * 2
    ]
]
; bug#719
["()" = mold quote ()]
; bug#77
["#[block! [[1 2] 2]]" == mold/all next [1 2]]
; bug#77
[blank? find mold/flat make object! [a: 1] "    "]
; bug#84
[equal? mold make bitset! "^(00)" "make bitset! #{80}"]
[equal? mold/all make bitset! "^(00)" "#[bitset! #{80}]"]
