; %parse-get-group.test.reb
;
; GET-GROUP!s will splice rules, null means no rule but succeeds...FALSE is
; useful for failing, and TRUE is a synonym for NULL in this context.
;
; They act like a COMPOSE/ONLY that runs each time the GET-GROUP! is passed.

(uparse? "aaa" [:(if false ["bbb"]) "aaa"])
(uparse? "bbbaaa" [:(if true ["bbb"]) "aaa"])

(uparse? "aaabbb" [:([some "a"]) :([some "b"])])
(uparse? "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])])
(uparse? "aaa" [:('some) "a"])
(not uparse? "aaa" [:(1 + 1) "a"])
(uparse? "aaa" [:(1 + 2) "a"])
(
    count: 0
    uparse? ["a" "aa" "aaa"] [some [into text! [:(count: count + 1) "a"]]]
)
