; datatypes/logic.r
(logic? true)
(logic? false)
(not logic? 1)
(logic! = type of true)
(logic! = type of false)
(on = true)
(off = false)
(yes = true)
(no = false)
(false = make logic! 0)
(true = make logic! 1)
(true = to logic! 0)
(true = to logic! 1)
(true = to logic! "f")

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
