; datatypes/native.r
(action? :reduce)
(not action? 1)
(action! = type of :reduce)
[#1659 (
    ; natives are active
    same? blank! eval reduce [
        (specialize 'of [property: 'type]) blank
    ]
)]
