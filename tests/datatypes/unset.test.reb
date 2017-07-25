; datatypes/unset.r
[void? ()]
[blank? type-of ()]
[not void? 1]

[#68 | void? try [a: ()]]

[error? try [a: () a]]
[not error? try [set/only 'a ()]]
