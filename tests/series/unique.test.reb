; functions/series/unique.r
([1 2 3] == unique [1 2 2 3])
([[1 2] [2 3] [3 4]] == unique [[1 2] [2 3] [2 3] [3 4]])
([path/1 path/2 path/3] == unique [path/1 path/2 path/2 path/3])

; case-insensitive default
(
    [aa "aa" #"a"] == unique [
        aa AA aa aA Aa aa
        "aa" "AA" "aa" "aA" "Aa" "aa"
        #"a" #"A"
    ]
)
(
    [aa AA aA Aa "aa" "AA" "aA" "Aa" #"a" #"A"] == unique/case [
        aa AA aa aA AA Aa aA Aa
        "aa" "AA" "aa" "aA" "AA" "Aa" "aA" "Aa"
        #"a" #"A" #"A" #"a"
    ]
)
