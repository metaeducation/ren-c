; functions/math/power.r

; 0 to the 0 power is defined to be 1.  This is the recommendation of Don
; Knuth, and followed by most languages despite the indeterminate status:
;
; https://rosettacode.org/wiki/Zero_to_the_zero_power
;
(1.0 ?= power 0 0)

(1 ?= power 1 1000)
(1 ?= power 1000 0)
(4 ?= power 2 2)
(0.5 ?= power 2 -1)
(0.1 ?= power 10 -1)
