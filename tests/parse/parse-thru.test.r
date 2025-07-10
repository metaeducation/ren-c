; %parse-thru.test.r
;
; PARSE-THRU is a wrapper around PARSE that returns how far the parse
; matched, or null.
;
; It is currently implemented in terms of the ACCEPT and <here> combinators
; so redefining those may break it.

("bbb" = parse-thru "aaabbb" [some "a"])
("" = parse-thru "aaabbb" [some "a" some "b"])

(null = parse-thru "aaabbb" [some "b"])
