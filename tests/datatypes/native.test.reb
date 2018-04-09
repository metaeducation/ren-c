; datatypes/native.r
(function? :reduce)
(not function? 1)
(function! = type of :reduce)
[#1659 (
    ; natives are active
    same? blank! do reduce [
        (specialize 'of [property: 'type]) make blank! blank
    ]
)]
