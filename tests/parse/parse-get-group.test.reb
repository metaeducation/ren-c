; %parse-get-group.test.reb
;
; GET-GROUP!s will splice rules, null means no rule but succeeds...FALSE is
; useful for failing, and TRUE is a synonym for NULL in this context.

(uparse? "aaa" [:(if false ["bbb"]) "aaa"])
(uparse? "bbbaaa" [:(if true ["bbb"]) "aaa"])
