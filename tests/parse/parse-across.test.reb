; %parse-across.test.reb
;
; ACROSS is UPARSE's version of historical PARSE COPY.
;
; !!! It is likely that this will change back, due to making peace with the
; ambiguity that COPY represents as a term, given all the other ambiguities.
; However, ACROSS is distinct and hence can be mixed in for compatibility
; while hybrid syntax is still around.


(did all [
    uparse? "aaabbb" [x: across some "a", y: across [some "b"]]
    x = "aaa"
    y = "bbb"
])
