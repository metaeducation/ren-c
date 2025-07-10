; %parse-tally.test.r
;
; TALLY is a new rule for making counting easier

(3 = parse "aaa" [tally "a"])

~parse-incomplete~ !! (parse "aaa" [tally "b"])
(0 = parse "aaa" [tally "b" elide to <end>])
(0 = parse "aaa" [accept tally "b"])

(all [
    let inner
    parse "(((stuff)))" [
        let n: tally "("
        inner: between <here> repeat (n) ")"
    ]
    inner = "stuff"
])

(3 = parse "abbcccabb" [tally ["a" repeat 2 "b" | repeat 3 "c"]])
