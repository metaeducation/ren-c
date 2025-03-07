; %parse-match.test.reb
;
; PARSE-MATCH is a useful variation of PARSE that will return the
; input series on a match, and null if not a match.

("aaabbb" = parse-match "aaabbb" [some "a" some "b"])

(null = parse-match "aaabbb" [some "a"])
("aaabbb" = parse-match:relax "aaabbb" [some "a"])

(null = parse-match "aaabbb" [some "b"])
(null = parse-match:relax "aaabbb" [some "b"])
