; functions/series/exclude.r
([1] = exclude [1 2] [2 3])
([[1 2]] = exclude [[1 2] [2 3]] [[2 3] [3 4]])
([path/1] = exclude [path/1 path/2] [path/2 path/3])

; :SKIP facility
[
    ([3 4] = exclude:skip [1 2 3 4] [1 2] 2)
    ([1 2 3 4] = exclude:skip [1 2 3 4] [2 3] 2)

    ("cd" = exclude:skip "abcd" "ab" 2)
    ("abcd" = exclude:skip "abcd" "bc" 2)

    (#{0304} = exclude:skip #{01020304} #{0102} 2)
    (#{01020304} = exclude:skip #{01020304} #{0203} 2)
]
