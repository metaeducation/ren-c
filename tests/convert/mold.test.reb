; functions/convert/mold.r
; cyclic block
[#860 #6 (
    a: copy []
    insert/only a a
    string? mold a
)]
; cyclic paren
(
    a: first [()]
    insert/only a a
    string? mold a
)
; cyclic object
[#69 (
    a: make object! [a: self]
    string? mold a
)]
; deep nested block mold
[#876 (
    n: 1
    catch [forever [
        a: copy []
        if error? try [
            loop n [a: append/only copy [] a]
            mold a
        ] [throw true]
        n: n * 2
    ]]
)]
[#719
    ("()" = mold quote ())
]

[#77
    ("#[block! [[1 2] 2]]" == mold/all next [1 2])
]
[#77
    (blank? find mold/flat make object! [a: 1] "    ")
]

[#84
    (equal? mold make bitset! "^(00)" "make bitset! #{80}")
]
[#84
    (equal? mold/all make bitset! "^(00)" "#[bitset! #{80}]")
]
