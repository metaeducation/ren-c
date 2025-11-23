; %parse-into.test.r
;
; INTO [...] is equivalent to SUBPARSE ONE [...]

('a = parse [[a a]] [into [some 'a]])
(#a = parse ["aa"] [into [some #a]])
