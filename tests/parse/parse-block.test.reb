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


; No-op rule of empty block should always match.
[
    (none? uparse "" [])
    (none? uparse "" [[]])
    (none? uparse "" [[[]]])

    (none? uparse [] [])
    (none? uparse [] [[[]]])
    (null = uparse [x] [])
    (null = uparse [x] [[[]]])
    ('x = uparse [x] [[] 'x []])
]


; Returns last value
[
    (
        wa: ['a]
        true
    )
    (
        res: 0
        did all [
            'a == uparse [a] [res: wa]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            'a == uparse [a a] [res: 2 wa]
            res = 'a
        ]
    )
]

; | means alternate clause
[
    (didn't uparse [a b] ['b | 'a])
    (#a == uparse [#a] [[#b | #a]])
    (didn't uparse [a b] [['b | 'a]])
    ('b == uparse [a b] [['a | 'b] ['b | 'a]])
]


; a BLOCK! rule combined with SET-WORD! will evaluate to the last value-bearing
; result in the rule.  This provides compatibility with the historical idea
; of doing Redbol rules like `set var [integer! | text!]`, but in that case
; it would set to the first item captured from the original input...more like
; `copy data [your rule], (var: first data)`.
;
; https://forum.rebol.info/t/separating-parse-rules-across-contexts/313/6
[
    (2 = uparse [1 2] [[integer! integer!]])
    ("a" = uparse ["a"] [[integer! | text!]])
]


; A BLOCK! rule is allowed to return NULL, distinct from failure
[
    (
        x: ~
        did all [
            '~null~ == meta uparse [1] [x: [integer! opt text!]]
            x = null
        ]
    )

    (
        x: ~
        did all [
            '~null~ == meta uparse [1] [integer! x: [(null)]]
            x = null
        ]
    )
]


; INLINE SEQUENCING is the idea of using || to treat everyting on the left
; as if it's in a block.
;
;    ["a" | "b" || "c"] <=> [["a" | "b"] "c"]
[
    ("c" = uparse "ac" ["a" | "b" || "c"])
    ("c" = uparse "bc" ["a" | "b" || "c"])
    (null = uparse "xc" ["a" | "b" || "c"])

    ("c" = uparse "ac" ["a" || "b" | "c"])
    ("b" = uparse "ab" ["a" || "b" | "c"])
    (null = uparse "ax" ["a" || "b" | "c"])
]


; UPARSE can be used where "heavy" nulls produced by the rule products do not
; trigger ELSE, but match failures do.
;
; !!! Is it worth it to add a way to do something like ^[...] block rules to
; say you don't want the ~null~ isotope, or does that just confuse things?  Is
; it better to just rig that up from the outside?
;
;     uparse data rules then result -> [
;         ; If RESULT is null we can react differently here
;     ] else [
;         ; This is a match failure null
;     ]
;
; Adding additional interface complexity onto UPARSE may not be worth it when
; it's this easy to work around.
[
    (
        x: uparse "aaa" [some "a" (null)] else [fail "Shouldn't be reached"]
        x = null
    )
    (
        did-not-match: false
        uparse "aaa" [some "b"] else [did-not-match: true]
        did-not-match
    )
]


[#1672 (  ; Self invoking rule, stack overflow
    a: [a]
    error? trap [uparse [] a]
)]


[
    (
        res: ~
        did all [
            'b == uparse [a a b] [<any> res: ['a | 'b] <any>]
            res = 'a
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse [a] [res: ['c | 'b]]
            res = '~before~
        ]
    )
]
