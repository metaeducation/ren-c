; functions/math/divide.r
(1 = divide -2147483648 -2147483648)
(2 = divide -2147483648 -1073741824)
(1073741824 = divide -2147483648 -2)
<32bit>
(warning? rescue [divide -2147483648 -1])
(warning? rescue [divide -2147483648 0])
(-2147483648 = divide -2147483648 1)
(-1073741824 = divide -2147483648 2)
(-2 = divide -2147483648 1073741824)
(0.5 = divide -1073741824 -2147483648)
(1 = divide -1073741824 -1073741824)
(536870912 = divide -1073741824 -2)
(1073741824 = divide -1073741824 -1)
(warning? rescue [divide -1073741824 0])
(-1073741824 = divide -1073741824 1)
(-536870912 = divide -1073741824 2)
(-1 = divide -1073741824 1073741824)
(1 = divide -2 -2)
(2 = divide -2 -1)
(warning? rescue [divide -2 0])
(-2 = divide -2 1)
(-1 = divide -2 2)
(0.5 = divide -1 -2)
(1 = divide -1 -1)
(warning? rescue [divide -1 0])
(-1 = divide -1 1)
(-0.5 = divide -1 2)
(0 = divide 0 -2147483648)
(0 = divide 0 -1073741824)
(0 = divide 0 -2)
(0 = divide 0 -1)
(warning? rescue [divide 0 0])
(0 = divide 0 1)
(0 = divide 0 2)
(0 = divide 0 1073741824)
(0 = divide 0 2147483647)
(-0.5 = divide 1 -2)
(-1 = divide 1 -1)
(warning? rescue [divide 1 0])
(1 = divide 1 1)
(0.5 = divide 1 2)
(-1 = divide 2 -2)
(-2 = divide 2 -1)
(warning? rescue [divide 2 0])
(2 = divide 2 1)
(1 = divide 2 2)
(-0.5 = divide 1073741824 -2147483648)
(-1 = divide 1073741824 -1073741824)
(-536870912 = divide 1073741824 -2)
(-1073741824 = divide 1073741824 -1)
(warning? rescue [divide 1073741824 0])
(1073741824 = divide 1073741824 1)
(536870912 = divide 1073741824 2)
(1 = divide 1073741824 1073741824)
(-1 = divide 2147483647 -2147483647)
(-1073741823.5 = divide 2147483647 -2)
(-2147483647 = divide 2147483647 -1)
(warning? rescue [divide 2147483647 0])
(2147483647 = divide 2147483647 1)
(1073741823.5 = divide 2147483647 2)
(1 = divide 2147483647 2147483647)
(10.0 = divide 1 0.1)
(10.0 = divide 1.0 0.1)
(10x10 = divide 1x1 0.1)
[#1974
    (10.10.10 = divide 1.1.1 0.1)
]


[#2516 (
    all [
        let code: [1 / 2]
        let obj: make object! [
            /: infix func [a b] [
                return reduce @(b a)
            ]
        ]
        0.5 = eval code
        code: overbind obj code
        @(2 1) = eval code
    ]
)]
