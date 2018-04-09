; datatypes/vector.r
(vector? make vector! 0)
(vector? make vector! [integer! 8])
(vector? make vector! [integer! 16])
(vector? make vector! [integer! 32])
(vector? make vector! [integer! 64])
(0 = length of make vector! 0)
(1 = length of make vector! 1)
(1 = length of make vector! [integer! 32])
(2 = length of make vector! 2)
(2 = length of make vector! [integer! 32 2])
[#1538
    (10 = length of make vector! 10.5)
]
[#1213
    (error? try [make vector! -1])
]
(0 = first make vector! [integer! 32])
(all map-each x make vector! [integer! 32 16] [zero? x])
(
    v: make vector! [integer! 32 3]
    v/1: 10
    v/2: 20
    v/3: 30
    v = make vector! [integer! 32 [10 20 30]]
)
