; datatypes/unset.r
[void? ()]
[blank? type-of ()]
[not void? 1]
; bug#68
[void? try [a: ()]]
[error? try [a: () a]]
[not error? try [set/opt 'a ()]]
