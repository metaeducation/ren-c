; %parse-ellipsis.test.r
;
; This is a souped up version of `ELIDE THRU` that effectively wrap the
; items to its right in a BLOCK!, or default to seeking <end> if there is
; nothing to the right.

(trash? parse "" [...])

("a" = parse "ab" ["a" ...])
("b" = parse "ab" [... "b"])

("b" = parse "aabbbcc" [... some "b" ...])
("b" = parse "aabbbcc" [... some "x" | some "b" ...])
