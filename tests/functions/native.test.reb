; datatypes/native.r
(activation? :reduce)
(not activation? 1)
(isotope! = kind of :reduce)
[#1659 (
    ; natives are active
    same? blank! do reduce [
        (unrun specialize :of [property: 'type]) blank
    ]
)]
