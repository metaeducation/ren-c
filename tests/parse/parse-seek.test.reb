; %parse-seek.test.reb
;
; SEEK allows you to seek an arbitrary position in the input series, either
; by integer index or by position.  It tolerates BLANK! to mean no-op.

(uparse? "a" [seek (_) "a"])
(uparse? "aaabbb" ["a" pos: <here> 2 "a" seek (pos) 2 "a" 3 "b"])
(uparse? "aaabbb" ["a" 2 "a" seek (2) 2 "a" 3 "b"])
