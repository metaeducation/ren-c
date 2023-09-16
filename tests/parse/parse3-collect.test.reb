; %parse3-collect.test.reb
;
; COLLECT and KEEP keywords in PARSE
;
; Non-keyword COLLECT has issues with binding, but also does not have the
; necessary hook to be able to "backtrack" and remove kept material when a
; match rule containing keeps ultimately fails.  These keywords were initially
; introduced in Red, without backtracking...and affecting the return result.
; In Ren-C, backtracking is implemented, and also it is used to set variables
; (like a SET or COPY) instead of affecting the return result.
;
; As an additional twist on PARSE3 collect vs UPARSE collect, there is no
; notion of "rule product"...hence what to keep is based on the range of
; the match.  KEEP INTEGER! thus keeps a block with a single element in it,
; and KEEP SPREAD INTEGER! is needed to splice the integer into the collected
; block.  It is not considered worth it to re-engineer parse3 to resolve this.

(
    parse3 [1 2 3] [x: collect [keep spread [some integer!]]]
    x = [1 2 3]
)
(
    parse3 [1 2 3] [x: collect [some [keep spread integer!]]]
    x = [1 2 3]
)
(
    parse3 [1 2 3] [x: collect [keep [some integer!]]]
    x = [[1 2 3]]
)
(
    parse3 [1 2 3] [x: collect [some [keep integer!]]]
    x = [[1] [2] [3]]
)

; Collecting non-array series fragments

(
    pos: parse3 "aaabbb" [x: collect [keep [some "a"]] accept <here>]
    did all [
        "bbb" = pos
        x = ["aaa"]
    ]
)
(
    pos: parse3 "aaabbbccc" [
        x: collect [keep [some "a"] some "b" keep [some "c"]]
        accept <here>
    ]
    did all [
        "" = pos
        x = ["aaa" "ccc"]
    ]
)

; Backtracking (more tests needed!)

(
    pos: parse3 [1 2 3] [
        x: collect [
            keep spread integer! keep spread integer! keep text!
            |
            keep spread integer! keep spread [some integer!]
        ]
        accept <here>
    ]
    did all [
        [] = pos
        x = [1 2 3]
    ]
)

; No change to variable on failed match (consistent with Rebol2/R3-Alpha/Red
; behaviors w.r.t SET and COPY)

(did all [
    x: <before>
    not try parse3 [1 2] [x: collect [keep spread integer! keep spread text!]]
    x = <before>
])

; Nested collect

(
    parse3 [1 2 3 4] [
        a: collect [
            keep spread integer!
            b: collect [keep spread [2 integer!]]
            keep spread integer!
        ]
        <end>
    ]

    did all [
        a = [1 4]
        b = [2 3]
    ]
)

; GROUP! can be used to keep material that did not originate from the
; input series or a match rule.
;
(
    pos: parse3 [1 2 3] [
        x: collect [
            keep spread integer!
            keep spread (second [A [<pick> <me>] B])
            keep spread integer!
        ]
        accept <here>
    ]

    did all [
        [3] = pos
        x = [1 <pick> <me> 2]
    ]
)
(
    pos: parse3 [1 2 3] [
        x: collect [
            keep spread integer!
            keep (second [A [<pick> <me>] B])
            keep spread integer!
        ]
        accept <here>
    ]

    did all [
        [3] = pos
        x = [1 [<pick> <me>] 2]
    ]
)
(
    parse3 [1 2 3] [x: collect [keep ([a b c]) to <end>]]
    x = [[a b c]]
)

; KEEP without blocks
https://github.com/metaeducation/ren-c/issues/935
[
    (
        parse3 "aaabbb" [x: collect [keep some "a" keep some "b"]]
        x = ["aaa" "bbb"]
    )

    (
        parse3 "aaabbb" [x: collect [keep to "b"] to <end>]
        x = ["aaa"]
    )

    (
        parse3 "aaabbb" [
            outer: collect [
                some [inner: collect keep some "a" | keep some "b"]
            ]
        ]
        did all [
            outer = ["bbb"]
            inner = ["aaa"]
        ]
    )
]

; Because COLLECT works with a SET-WORD! on the left, it also works with
; LET, so it can limit the scope of a variable
(
    x: <x>
    y: <y>
    parse3 "aaa" [let x: collect [some [keep "a"]], (y: x)]
    did all [
        x = <x>
        y = ["a" "a" "a"]
    ]
)

; Special tweak to make COLLECT work as a synthesized result value, just to
; give a hint at what's coming as UPARSE features go mainline.
(
    ["a" "a" "a"] = parse3 "aaa" [collect [some [keep "a"]]]
)
