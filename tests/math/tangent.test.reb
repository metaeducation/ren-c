; functions/math/tangent.r
(error? trap [tangent -90])
(error? trap [tangent/radians pi / -2])
((negate square-root 3) = tangent -60)
((negate square-root 3) = tangent/radians pi / -3)
(-1 = tangent -45)
(-1 = tangent/radians pi / -4)
(((square-root 3) / -3) = tangent -30)
(((square-root 3) / -3) = tangent/radians pi / -6)
(0 = tangent 0)
(0 = tangent/radians 0)
(((square-root 3) / 3) = tangent 30)
(((square-root 3) / 3) = tangent/radians pi / 6)
(1 = tangent 45)
(1 = tangent/radians pi / 4)
((square-root 3) = tangent 60)
((square-root 3) = tangent/radians pi / 3)
(error? trap [tangent 90])
(error? trap [tangent/radians pi / 2])
; Flint Hills test
(
    n: 25000
    s4t: 0.0
    count-up l n [
        k: to decimal! l
        kt: tangent/radians k
        s4t: (((1.0 / (kt * kt)) + 1.0) / (k * k * k)) + s4t
    ]
    30.314520404 = round/to s4t 1e-9
)
