; %parse-quoted.test.reb
;
; Quoted values are matched as is, but for strings they are formed.

(
    did all [
        pos: uparse* [... [a b]] [to '[a b], <here>]
        pos = [[a b]]
    ]
)
(uparse? [... [a b]] [thru '[a b]])
(uparse? [1 1 1] [some '1])

; !!! Review: how do we go INTO a QUOTED! series?
;
;   pos: uparse* [''[1 + 2]] [into quoted! [copy x to <end>], <here>]
;   [] == pos
;   x == [1 + 2]
;

; Ren-C made it possible to use quoted WORD!s in place of CHAR! or TEXT! to
; match in strings.  This gives a cleaner look, as you drop off 3 vertical
; tick marks from everything like ["ab"] to become just ['ab]
[
    (did all [
        pos: uparse* "abbbbbc" ['a some ['b], <here>]
        "c" = pos
    ])
    (did all [
        pos: uparse* "abbbbc" ['ab, some ['bc | 'b], <here>]
        "" = pos
    ])
    (did all [
        pos: uparse* "abc10def" ['abc '10, <here>]
        "def" = pos
    ])
]

[#682 (  ; like the old way...
    t: ~
    uparse "<tag>text</tag>" [thru '<tag> t: across to '</tag> to <end>]
    t == "text"
)(
    "text" = uparse "<tag>text</tag>" [between '<tag> '</tag>]  ; ah, uparse!
)]
