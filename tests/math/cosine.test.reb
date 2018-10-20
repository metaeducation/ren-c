; functions/math/cosine.r

; Tests here use lax comparison with IS, because there is limited precision
; for establishing bitwise equivalence of things like square roots formulation
; compared to the calculated result of a trig function.

(1 is cosine 0)
(1 is cosine/radians 0)
(((square-root 3) / 2) is cosine 30)
(((square-root 3) / 2) is cosine/radians pi / 6)
(((square-root 2) / 2) is cosine 45)
(((square-root 2) / 2) is cosine/radians pi / 4)
(0.5 is cosine 60)
(0.5 is cosine/radians pi / 3)
(0 is cosine 90)
(0 is cosine/radians pi / 2)
(-1 is cosine 180)
(-1 is cosine/radians pi)
(((square-root 3) / -2) is cosine 150)
(((square-root 3) / -2) is cosine/radians pi * 5 / 6)
(((square-root 2) / -2) is cosine 135)
(((square-root 2) / -2) is cosine/radians pi * 3 / 4)
(-0.5 is cosine 120)
(-0.5 is cosine/radians pi * 2 / 3)
