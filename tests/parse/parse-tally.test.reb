; %parse-tally.test.reb
;
; TALLY is a new rule for making counting easier

(3 = uparse "aaa" [tally "a"])

(null = uparse "aaa" [tally "b"])  ; doesn't finish parse
(0 = uparse "aaa" [tally "b" elide to <end>])  ; must be at end
(0 = uparse* "aaa" [tally "b"])  ; alternately, use non-force-completion

(did all [
    uparse "(((stuff)))" [
        n: tally "("
        inner: between <here> repeat (n) ")"
    ]
    inner = "stuff"
])
