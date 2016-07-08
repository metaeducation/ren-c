; functions/series/lengthq.r
; bug#1626: "Allow LENGTH? to take blank as an argument, return blank"
; bug#1688: "LENGTH? NONE returns TRUE" (should return NONE)
[blank? length? blank]
