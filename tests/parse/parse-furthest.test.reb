; %parse-furthest.test.reb
;
; The /FURTHEST feature was requested by @CodeByBrett.  Making it work means
; having a hook inside every combinator to know when it succeeds and how
; far it got

(did all [
    null = [# furthest]: uparse "aaabbb" [some "a"]
    furthest = "bbb"
])
