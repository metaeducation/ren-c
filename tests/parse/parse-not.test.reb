; %parse-not.test.reb
;
; NOT is actually NOT-AHEAD, but simplified to just NOT

; NOT NOT should be equivalent to AHEAD
; Red at time of writing has trouble with this
; As does Haskell Parsec, e.g. (notFollowedBy . notFollowedBy != lookAhead)
; https://github.com/haskell/parsec/issues/8
[
    ("a" = match-uparse "a" [[ahead "a"] "a"])
    ("a" = match-uparse "a" [[not not "a"] "a"])
]
