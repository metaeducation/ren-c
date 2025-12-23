; functions/series/split.r

[
    #1886

    (["1234" "5678" "1234" "5678"] = split "1234567812345678" 4)
    (["123" "456" "781" "234" "567" "8"] = split "1234567812345678" 3)
    (["12345" "67812" "34567" "8"] = split "1234567812345678" 5)
    ([[1 2 3] [4 5 6]] = split:into [1 2 3 4 5 6] 2)
    (["12345678" "12345678"] = split:into "1234567812345678" 2)
    (["12345" "67812" "345678"] = split:into "1234567812345678" 3)
    (["123" "456" "781" "234" "5678"] = split:into "1234567812345678" 5)
]

; Delimiter longer than series
(["1" "2" "3" "" "" ""] = split:into "123" 6)
([[1] [2] [3] [] [] []] = split:into [1 2 3] 6)

([[1 2] [3] [4 5 6]] = split [1 2 3 4 5 6] @[2 1 3])
(["1234" "5678" "12" "34" "5" "6" "7" "8"] = split "1234567812345678" @[4 4 2 2 1 1 1 1])
([(1 2 3) (4 5 6) (7 8 9)] = split first [(1 2 3 4 5 6 7 8 9)] 3)
([#{01020304} #{050607} #{08} #{090A}] = split #{0102030405060708090A} @[4 3 1 2])
([[1 2] [3]] = split [1 2 3 4 5 6] @[2 1])
([[1 2] [3] [4 5 6] []] = split [1 2 3 4 5 6] @[2 1 3 5])
([[1 2] [3] [4 5 6]] = split [1 2 3 4 5 6] @[2 1 6])
([[1 2] [5 6]] = split [1 2 3 4 5 6] @[2 -2 2])
(["abc" "de" "fghi" "jk"] = split "abc,de,fghi,jk" #",")
(["a" "b" "c"] = split "a.b.c" ".")
(["c" "c"] = split "c c" " ")
(["1,2,3"] = split "1,2,3" " ")
(["1" "2" "3"] = split "1,2,3" ",")
(["1" "2" "3" ""] = split "1,2,3," ",")
(["1" "2" "3" ""] = split "1,2,3," charset ",.")
(["1" "2" "3" ""] = split "1.2,3." charset ",.")
(["-" "-"] = split "-a-a" ["a"])
(["-" "-" "'"] = split "-a-a'" ["a"])
(["abc" "de" "fghi" "jk"] = split "abc|de/fghi:jk" charset "|/:")
(["abc" "de" "fghi" "jk"] = split "abc^M^Jde^Mfghi^Jjk" [CR LF | #"^M" | newline])
(["abc" "de" "fghi" "jk"] = split "abc     de fghi  jk" [some #" "])

; tag delimiter
([[a] [b c] []] = split [a <t> b c <t>] quote <t>)
(["abc" "de" "fghi" "jk"] = split "abc<br>de<br>fghi<br>jk" quote <br>)

; WORD! delimiter in block
([[a] [b c] []] =  split [a | b c |] quote '|)
([[a] [b c] []] = split [a x b c x] quote 'x)


[#690 (
    ["This" " is a" " test" " to see "]
        = split "This! is a. test? to see " charset "!?."
)]

(["a" "b" "c"] = split "a-b-c" "-")
(["a" "c"] = split "a-b-c" "-b-")
(["a-b-c"] = split "a-b-c" "x")

; https://github.com/red/red/pull/4381
;
(null = eval compose [
    same? (second split "a," ",") (second split "b," ",")
])

(equal? ["1" "3" "" "3" "" ""] split "1,3,.3,," charset ".,")

(equal? ["1" ""] split "1^/" #"^/")
(equal? ["1" "2" ""] split "1^/2^/" #"^/")

(2 = length of trim first split -[х^/+й]- "+")

; VOID has nothing to split by, return original input (but in a block, to
; match the other outputs).
;
(["a,b,c"] = split "a,b,c" ^ghost)
([[a, b, c]] = split [a, b, c] ^ghost)

; SPREAD support is TBD when SPLIT is based on UPARSE.
;
~???~ !! (split [< > a b c < > d e < > f g] spread [< >])
