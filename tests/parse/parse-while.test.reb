; %parse-while.test.reb
;
; This is a test for UPARSE's arity-2 WHILE.
;
;     while rule1 rule2  <=>  opt some [rule1 rule2]
;
; It doesn't test for truthiness or falseyness of the rule--which is a bit
; confusing, rather whether the rule succeeded.

(parse [1 2 3] [(x: 0) while :(x < 3) [x: integer!]])

(1020 = parse "aaa" [while [some "a"] (1020)])

(nothing? parse "" [while [some "a"] (1020)])

(["c" ~null~ "c"] = collect [
    parse "aabcabaaabc" [while [x: [some "a" "b" opt "c"]] (keep reify x)]
])
