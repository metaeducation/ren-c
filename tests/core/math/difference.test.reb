; functions/math/difference.r
[24:00 = difference 1/Jan/2007 31/Dec/2006]
[0:00 = difference 1/Jan/2007 1/Jan/2007]
; block
[[1 2] = difference [1 3] [2 3]]
[[] = difference [1 2] [1 2]]
; bitset
[(charset "a") = difference charset "a" charset ""]
; bug#1822: DIFFERENCE on date!s problem
[12:00 = difference 13/1/2011/12:00 13/1/2011]
