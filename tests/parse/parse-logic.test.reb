; %parse-logic.test.reb
;
; A logic true acts as a no-op, while a logic false causes matches to fail

(uparse? "ab" ["a" true "b"])
(not uparse? "ab" ["a" false "b"])
(uparse? "ab" ["a" :(1 = 1) "b"])
(not uparse? "ab" ["a" :(1 = 2) "b"])
