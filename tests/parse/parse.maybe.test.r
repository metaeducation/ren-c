; %parse-opt.test.r
;
; OPT is an alternative to the TRY combinator, that synthesizes VOID instead
; of NULL when a parse rule does not match.

(void? parse "aaa" [some "a" opt some "b"])
(void? parse "aaabbb" [some "a" opt some "b"])


; Usages with SET-WORD!, potentially opting out of changing variables and
; making the rule evaluation their previous value if opting out.
;
; !!! So long as OPT is designed to return nihil, this can't do a legal
; assignment...demonstrate meta

(all [
    "b" = parse "bbb" [
        (x: 10, y: 20)
        y: x: [opt some "a"]
        some "b"
    ]
    unset? $y
    unset? $x
])

(all [
    "b" = parse "aaabbb" [
        (x: 10, y: 20)
        y: x: [opt some "a"]  ; matched, so non-void...changes x
        some "b"
    ]
    x = "a"
    y = "a"
])

(all [
    "b" = parse "bbb" [
        (x: 10, y: 20)
        y: x: opt (~)
        some "b"
    ]
    tripwire? ^x
    tripwire? $y
])
