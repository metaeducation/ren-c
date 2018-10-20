; functions/math/arcsine.r

; Tests here use lax comparison with IS, because there is limited precision
; for establishing bitwise equivalence of things like square roots formulation
; compared to the calculated result of a trig function.

(0 is arcsine 0)
(0 is arcsine/radians 0)
(30 is arcsine 0.5)
((pi / 6) is arcsine/radians 0.5)
(45 is arcsine (square-root 2) / 2)
((pi / 4) is arcsine/radians (square-root 2) / 2)
(60 is arcsine (square-root 3) / 2)
((pi / 3) is arcsine/radians (square-root 3) / 2)
(90 is arcsine 1)
((pi / 2) is arcsine/radians 1)
(-30 is arcsine -0.5)
((pi / -6) is arcsine/radians -0.5)
(-45 is arcsine (square-root 2) / -2)
((pi / -4) is arcsine/radians (square-root 2) / -2)
(-60 is arcsine (square-root 3) / -2)
((pi / -3) is arcsine/radians (square-root 3) / -2)
(-90 is arcsine -1)
((pi / -2) is arcsine/radians -1)
((1e-12 / (arcsine 1e-12)) is (pi / 180))
((1e-9 / (arcsine/radians 1e-9)) is 1.0)
(error? trap [arcsine 1.1])
(error? trap [arcsine -1.1])
