; functions/series/lengthq.r
[
    #1626 ; "Allow LENGTH? to take blank as an argument, return blank"
    #1688 ; "LENGTH? TRASH returns TRUE" (should return TRASH)
    (null? length of blank)  ; in Ren-C, returns null
]
