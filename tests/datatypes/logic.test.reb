; datatypes/logic.r
(logic? true)
(logic? false)
(not logic? 1)
(isotope! = kind of true)
(isotope! = kind of false)
(on = true)
(off = false)
(yes = true)
(no = false)
(false = false-if-zero 0)
(true = false-if-zero 1)
(true = to-logic 0)
(true = to-logic 1)
(true = to-logic "f")

~bad-isotope~ !! (mold true)
~bad-isotope~ !! (mold false)

("true" = mold logic-to-word true)
("false" = mold logic-to-word false)

; Legacy support for LOAD-ability, compatible with Rebol2/R3-Alpha
; (This support could be shifted to the Redbol module, perhaps?)
[
    ('~true~ = the #[true])
    ('~false~ = the #[false])
    (true = #[true])  ; legacy support
    (false = #[false])
]
