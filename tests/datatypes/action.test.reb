; datatypes/action.r

(action? :abs)
(not action? 1)
(action! == type of :abs)
; actions are active
[#1659
    (1 == do reduce [:abs -1])
]
