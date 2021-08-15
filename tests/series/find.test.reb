; functions/series/find.r
;
; !!! R3-Alpha had a severe lack of tests for FIND.  However, the same routine
; is used in PARSE, so parse tests exercise the same code path (though not the
; reverse case currently...)

[#473 (
    null? find blank 1
)]
(null? find [] 1)
(
    blk: [1]
    same? blk find blk 1
)
(null? find/part [x] just x 0)
(equal? [x] find/part [x] just x 1)
(equal? [x] find-reverse tail of [x] just x)

; Historically Rebol FIND/MATCH implied /TAIL.  This was frowned upon in Ren-C,
; and ultimately changed by Red, which seems like pretty good evidence that
; it's the sort of thing that clearly is better changed:
;
; https://github.com/red/red/issues/4943
;
; This includes some of the tests from Red's %find-test.red (BSD-3 License,
; Copyright (C) 2011-2015 Red Foundation).
[
    (equal? [x y] find/match [x y] 'x)
    (equal? [y] find/match/tail [x y] 'x)

    ([here and now] = find/match [here and now] 'here)
    (null = find/match [here and now] 'her)
    (null = find/match [her and now] 'here)
    ("here and now" = find/match "here and now" "here")
    ("hereandnow" = find/match "hereandnow" "here")
    (null = find/match "her and now" "here")
    ("here✐ and now" = find/match "here✐ and now" "here✐")
    ("here✐andnow" = find/match "here✐andnow" "here")
    (null = find/match "her and now" "he✐r")
    (null = find/match "here and now" "✐here")
    ("here^(010000)andnow" = find/match "here^(010000)andnow" "here")
    (null = find/match "her and now" "here^(010000)")
    ("here^(010000) and now" = find/match "here^(010000) and now" "here^(010000)")
    ("^(010000)hereandnow" = find/match "^(010000)hereandnow" "^(010000)here")
    (null = find/match "her^(010000) and now" "here^(010000)")
    ([he✐re and now] = find/match [he✐re and now] 'he✐re)
]

(equal? [x] find-last [x] just x)
(equal? [x] find-last [x x x] just x)
[#66
    (null? find/skip [1 2 3 4 5 6] 2 3)
]
[#88
    ("c" = find "abc" charset ["c"])
]
[#88
    (null? find/part "ab" "b" 1)
]

[#2324 (
    str: "1.1.1"
    all [
        "1.1.1" == find/part str "1." 2
        (elide str: skip str 2)
        "1.1" == find str "1."
        "1.1" == find/part str "1." 2
    ]
)]

[
    ("" = find "" "")
    ("a" = find "a" "")
    ("" = find tail "a" "")
    (null = find "" "a")

    ("ab" = find "ab" "a")
    ("b" = find "ab" "b")
    (null = find "ab" "c")

    ("" = find-reverse "" "")
    ("a" = find-reverse "a" "")
    ("" = find-reverse tail "a" "")
    (null = find-reverse "" "a")

    ("ab" = find-reverse tail "ab" "a")
    ("b" = find-reverse tail "ab" "b")
    (null = find-reverse tail "ab" "c")
]

[
    ("def" = find/skip tail "abcdef" "def" -3)
    (null = find/skip tail "abcdef" "def" -2)
    ("def" = find/skip tail "abcdef" "def" -1)

    ("abcdef" = find/skip tail "abcdef" "abc" -3)
    ("abcdef" = find/skip tail "abcdef" "abc" -2)
    (null = find/skip back tail "abcdef" "abc" -2)
    ("abcdef" = find/skip tail "abcdef" "abc" -1)
]

("cd" = find skip "abcd" 2 "cd")
("abcd" = find-reverse skip "abcd" 2 "abcd")

[
    (did ab: to binary! "ab")

    (ab = find ab "a")
    ((to binary! "b") = find ab "b")
    (null = find ab "c")

    ; !!! String search in binary only supports /skip of 1 for now (e.g no -1)
    ;(ab = find-reverse tail ab "a")
    ;((to binary! "b") = find-reverse tail ab "b")
    ;(null = find-reverse tail ab "c")
]

(null = find "api-transient" "to")
("transient" = find "api-transient" "trans")


; Strings can be searched for in binaries, #{00} bytes aren't legal in string
; and should be skipped as match candidates (like illegal UTF-8, e.g. #{FF}).
[
    (#{616263} = to binary! "abc")

    (#{61626300FF} = find #{FF0061626300FF} "ABC")
    (null = find/case #{FF0061626300FF} "ABC")
    (#{61626300FF} = find #{FF0061626300FF} "abc")
    (#{61626300FF} = find/case #{FF0061626300FF} "abc")

    (#{61626300FF} = find #{FF0061626300FF} #{616263})
    (#{61626300FF} = find/case #{FF0061626300FF} #{616263})  ; /case ignored

    (#{414243} = to binary! "ABC")
    (null = find #{FF0061626300FF} #{414243})
    (null = find/case #{FF0061626300FF} #{414243})  ; /case ignored
]

[
    (#{C386} = find/skip to binary! "Æ" "Æ" 1)
    (#{C386} = find/skip tail to binary! "Æ" "Æ" -1)
]

[
    (#{00} = find #{00} #{00})
    (#{00} = find/case #{00} #{00})
    (#{00} = find #{00} #)
    (#{00} = find/case #{00} #)
]

; @ types are now handled as literal by APPEND/FIND unless quoted
[
    ([b c] = find [a b c] @b)
    ([[b c] d] = find [a [b c] d] @[b c])
]
