; datatypes/native.r
(action? :reduce)
(not action? 1)
(antiform! = type of :reduce)
[#1659 (
    ; natives are active
    equal? blank! eval reduce [  ; not SAME? as type block array may differ
        (unrun specialize get $of [property: 'type]) blank
    ]
)]
