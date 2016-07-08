; datatypes/block.r
[block? [1]]
[not block? 1]
[block! = type-of [1]]
; minimum
[block? []]
; alternative literal representation
[[] == #[block! [[] 1]]]
[[] == make block! 0]
[[] == to block! ""]
["[]" == mold []]
