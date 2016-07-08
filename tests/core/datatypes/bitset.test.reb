; datatypes/bitset.r
[bitset? make bitset! "a"]
[not bitset? 1]
[bitset! = type-of make bitset! "a"]
; minimum, literal representation
[bitset? #[bitset! #{}]]
; TS crash
[bitset? charset reduce [to-char "^(A0)"]]
