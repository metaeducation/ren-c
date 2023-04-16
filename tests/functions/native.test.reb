; datatypes/native.r
(action? :reduce)
(not action? 1)
(action! = kind of :reduce)
[#1659 (
    ; natives are active
    same? blank! do reduce [
        (specialize :of [property: 'type]) blank
    ]
)]
