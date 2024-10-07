; %parse-maybe.test.reb
;
; MAYBE was conceived as an alternative to TRY, which was similar in that
; it would continue if the parameter it was parameterized would fail.  But
; instead of being null it would vanish.
;
; For now, it is not a combinator in use.
;
;     'maybe combinator [
;        "If applying parser fails, succeed and vanish; don't advance input"
;        return: "PARSER's result if it succeeds w/non-NULL, otherwise vanish"
;            [any-value?]
;        parser [action?]
;        <local> result'
;    ][
;        [^result' remainder]: parser input except [
;            remainder: input  ; succeed on parser fail but don't advance input
;            return ~[]~  ; act invisible
;        ]
;        return unmeta result'  ; return successful parser result
;    ]
;

; Plain usage...potentially vanish inline, leaving prior result

; ("a" = parse "aaa" [some "a" maybe some "b"])
; ("b" = parse "aaabbb" [some "a" maybe some "b"])


; Usages with SET-WORD!, potentially opting out of changing variables and
; making the rule evaluation their previous value if opting out.
;
; !!! So long as MAYBE is designed to return nihil, this can't do a legal
; assignment...demonstrate meta

; (all [
;     "b" == parse "bbb" [
;         (x: 10, y: 20)
;         y: x: ^[maybe some "a"]  ; !!! maybe retention concept TBD
;         some "b"
;     ]
;     '~[]~ = y
;     '~[]~ = x
; ])
;
; (all [
;     "b" == parse "aaabbb" [
;         (x: 10, y: 20)
;         y: x: ^[maybe some "a"]  ; matched, so non-void...changes x
;         some "b"
;     ]
;     x = quote "a"
;     y = quote "a"
; ])
;
; (all [
;     "b" == parse "bbb" [
;         (x: 10, y: 20)
;         y: x: maybe (~)  ; antiform ~ is nothing, unset variable
;         some "b"
;     ]
;     '~ = ^ get:any $x
;     unset? $y
; ])
