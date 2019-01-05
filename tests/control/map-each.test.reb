; functions/control/map-each.r
; "return bug"
(
    integer? eval does [map-each v [] [] 1]
)

; PATH! is immutable, but MAP-EACH should work on it

(
     [[a 1] [b 1] [c 1]] = map-each x 'a/b/c [reduce [x 1]]
)
