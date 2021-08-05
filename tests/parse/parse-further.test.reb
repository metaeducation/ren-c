; %parse-further.test.reb
;
; Historical Rebol's ANY and SOME operations had a built in notion of checking
; for advancement.  This introduced a subtle and easy to trip up on notion
; of whether a rule advanced, so rules that didn't want to stop the iteration
; but removed input etc. would create an issue.  FURTHER takes the test
; for advancement out and makes it usable with any rule.

(null = uparse "" [further [opt "a" opt "b"] ("at least one")])
("at least 1" = uparse "a" [further [opt "a" opt "b"] ("at least 1")])
("at least 1" = uparse "a" [further [opt "a" opt "b"] ("at least 1")])
("at least 1" = uparse "ab" [further [opt "a" opt "b"] ("at least 1")])

(uparse? "" [while further [to <end>]])
