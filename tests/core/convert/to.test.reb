; functions/convert/to.r
; bug#38
['logic! = to word! logic!]
['percent! = to word! percent!]
['money! = to word! money!]
; bug#1967
[not same? to binary! [1] to binary! [2]]
