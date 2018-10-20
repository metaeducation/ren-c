; functions/math/arctangent.r

; Tests here use lax comparison with IS, because there is limited precision
; for establishing bitwise equivalence of things like square roots formulation
; compared to the calculated result of a trig function.

(-90 is arctangent -1e16)
((pi / -2) is arctangent/radians -1e16)
(-60 is arctangent negate square-root 3)
((pi / -3) is arctangent/radians negate square-root 3)
(-45 is arctangent -1)
((pi / -4) is arctangent/radians -1)
(-30 is arctangent (square-root 3) / -3)
((pi / -6) is arctangent/radians (square-root 3) / -3)
(0 is arctangent 0)
(0 is arctangent/radians 0)
(30 is arctangent (square-root 3) / 3)
((pi / 6) is arctangent/radians (square-root 3) / 3)
(45 is arctangent 1)
((pi / 4) is arctangent/radians 1)
(60 is arctangent square-root 3)
((pi / 3) is arctangent/radians square-root 3)
(90 is arctangent 1e16)
((pi / 2) is arctangent/radians 1e16)
