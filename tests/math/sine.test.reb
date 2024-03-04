; functions/math/sine.r
(0 = sine 0)
(0 = sine/radians 0)
(0.5 = sine 30)
(0.5 = sine/radians pi / 6)
(((square-root 2) / 2) = sine 45)
(((square-root 2) / 2) = sine/radians pi / 4)
(((square-root 3) / 2) = sine 60)
(((square-root 3) / 2) = sine/radians pi / 3)
(1 = sine 90)
(1 = sine/radians pi / 2)
(0 = sine 180)
(0 = sine/radians pi)
(-0.5 = sine -30)
(-0.5 = sine/radians pi / -6)
(((square-root 2) / -2) = sine -45)
(((square-root 2) / -2) = sine/radians pi / -4)
(((square-root 3) / -2) = sine -60)
(((square-root 3) / -2) = sine/radians pi / -3)
(-1 = sine -90)
(-1 = sine/radians pi / -2)
(0 = sine -180)
(0 = sine/radians negate pi)
(((sine 1e-12) / 1e-12) = (pi / 180))
(((sine/radians 1e-9) / 1e-9) = 1.0)
; Flint Hills test
[#852 (
    n: 25000
    s4: 0.0
    count-up l n [
        k: to decimal! l
        ks: sine/radians k
        s4: (1.0 / (k * k * k * ks * ks)) + s4
    ]
    30.314520404 = round/to s4 1e-9
)]
