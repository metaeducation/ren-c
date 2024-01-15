; %parse-furthest.test.reb
;
; The FURTHEST feature was requested by @CodeByBrett.  Making it work means
; having a hook into every combinator to know when it succeeds and how
; far it got.
;
; !!! Initially this was a feature provided as an optional return value
; from the parse.  However, that was foiled by the idea that a parse which
; does not complete now returns a raised definitional error...which keeps
; it from returning a pack of values.  It also interferes with the idea that
; the rules themselves would synthesize a multi-return value.  The hack of
; a parse variant PARSE-FURTHEST which takes a variable to write the furthest
; point to is used as a placeholder for a better solution.
;
; https://forum.rebol.info/t/semantics-of-uparses-furthest/1868/2

(
    far: ~
    parse-furthest "aaabbb" [some "a" some "c"] 'far except [
        far = "bbb"
    ]
)

; Unusual property of AHEAD and TO... it counts what the TO's argument parser
; accepted, even though TO ultimately didn't count that as advanced input.
[(
    far: ~
    parse-furthest "aaabbbccc" [some "a" to [some "b"] some "d"] 'far except [
        far = "ccc"
    ]
)(
    far: ~
    parse-furthest "abcd" [to "c"] 'far except [
        far = "d"
    ]
)]
