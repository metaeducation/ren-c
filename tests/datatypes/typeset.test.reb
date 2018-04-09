; datatypes/typeset.r
(typeset? any-array!)
(typeset? to-typeset any-array!)
(typeset? any-path!)
(typeset? to-typeset any-path!)
(typeset? any-context!)
(typeset? to-typeset any-context!)
(typeset? any-string!)
(typeset? to-typeset any-string!)
(typeset? any-word!)
(typeset? to-typeset any-word!)
(typeset? immediate!)
(typeset? to-typeset immediate!)
(typeset? internal!)
(typeset? to-typeset internal!)
(typeset? any-number!)
(typeset? to-typeset any-number!)
(typeset? any-scalar!)
(typeset? to-typeset any-scalar!)
(typeset? any-series!)
(typeset? to-typeset any-series!)
(typeset? make typeset! [integer! blank!])
(typeset? make typeset! reduce [integer! blank!])
(typeset? to-typeset [integer! blank!])
(typeset! = type of any-series!)

[#92 (
    x: to typeset! []
    not (x = now)
)]
