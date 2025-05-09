; %parse-block.test.reb
;
; BLOCK! is the fundamental rule category of UPARSE, but it's also a combinator.
; This means it can be overridden and hooked.
;
; It is one of the combinators that has to get involved with managing the
; meaning of the `pending` return value, because it splits its contents into
; conceptual "parser alternate groups".  Mere success of one parser in the
; group does not mean it is supposed to add its collected matter to the
; pending list...the entire group must succeed


; Empty block rules vaporize
(
    var: []
    'b = parse [a b] ['a 'b var]
)
('b = parse [a b] ['a 'b :(if ok '[])])


; No-op rule of empty block should always match.
[
    (void? parse "" [])
    (void? parse "" [[]])
    (void? parse "" [[[]]])

    (void? parse [] [])
    (void? parse [] [[[]]])

    ~parse-incomplete~ !! (parse [x] [])
    ~parse-incomplete~ !! (parse [x] [[[]]])

    ('x = parse [x] [[] 'x []])
]


; Returns last value
[
    (
        wa: ['a]
        ok
    )
    (
        res: 0
        all [
            'a = parse [a] [res: wa]
            res = 'a
        ]
    )
    (
        res: 0
        all [
            'a = parse [a a] [res: repeat 2 wa]
            res = 'a
        ]
    )
]

; | means alternate clause
[
    ~parse-incomplete~ !! (parse [a b] ['b | 'a])
    ~parse-incomplete~ !! (parse [a b] [['b | 'a]])

    (#a = parse [#a] [[#b | #a]])
    ('b = parse [a b] [['a | 'b] ['b | 'a]])
]


; a BLOCK! rule combined with SET-WORD! will evaluate to the last value-bearing
; result in the rule.  This provides compatibility with the historical idea
; of doing Redbol rules like `set var [integer! | text!]`, but in that case
; it would set to the first item captured from the original input...more like
; `copy data [your rule], (var: first data)`.
;
; https://forum.rebol.info/t/separating-parse-rules-across-contexts/313/6
[
    (2 = parse [1 2] [[integer! integer!]])
    ("a" = parse ["a"] [[integer! | text!]])
]


; A BLOCK! rule is allowed to return VOID and NULL, distinct from failure
[
    (
        x: ~
        all [
            void = parse [1] [x: [integer! opt text!]]
            x = null
        ]
    )

    (
        x: ~
        all [
            null = parse [1] [integer! x: [(null)]]
            x = null
        ]
    )
]


; INLINE SEQUENCING is the idea of using || to treat everyting on the left
; as if it's in a block.
;
;    ["a" | "b" || "c"] <=> [["a" | "b"] "c"]
[
    ("c" = parse "ac" ["a" | "b" || "c"])
    ("c" = parse "bc" ["a" | "b" || "c"])
    ~parse-mismatch~ !! (parse "xc" ["a" | "b" || "c"])

    ("c" = parse "ac" ["a" || "b" | "c"])
    ("b" = parse "ab" ["a" || "b" | "c"])
    ~parse-mismatch~ !! (parse "ax" ["a" || "b" | "c"])
]

[
    (
        x: parse "aaa" [some "a" (null)] except [panic "Shouldn't be reached"]
        x = null
    )
    (
        matched: 'yes
        parse "aaa" [some "b"] except [matched: 'no]
        no? matched
    )
]


[#1672 (  ; infinite recursion
    <deep-enough> = catch wrap [
        x: 0
        a: [(x: x + 1, if x = 200 [throw <deep-enough>]) a]
        parse [] a
    ]
)]


[
    (
        res: ~
        all [
            'b = parse [a a b] [<next> res: ['a | 'b] one]
            res = 'a
        ]
    )
    (
        res: '~before~
        all [
            error? parse [a] [res: ['c | 'b]]
            res = '~before~
        ]
    )
]
