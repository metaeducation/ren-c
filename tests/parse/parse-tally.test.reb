; %parse-tally.test.reb
;
; TALLY is a new rule for making counting easier

(3 = parse "aaa" [tally "a"])

(raised? parse "aaa" [tally "b"])  ; doesn't finish parse
(0 = parse "aaa" [tally "b" elide to <end>])  ; must be at end
(0 = parse "aaa" [accept tally "b"])  ; alternately, use non-force-completion

(did all [
    parse "(((stuff)))" [
        n: tally "("
        inner: between <here> repeat (n) ")"
    ]
    inner = "stuff"
])

(3 == parse "abbcccabb" [tally ["a" repeat 2 "b" | repeat 3 "c"]])
