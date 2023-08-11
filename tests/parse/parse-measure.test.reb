; %parse-measure.test.reb
;
; Inspired by converting TRIM to UPARSE and calculating TRIM/AUTO indents.
; This measures the length in series units across a match.

(3 = parse "aaa" [measure some "a"])
(raised? parse "aaa" [measure some "b"])

; Applying MEASURE to itself should return the same thing (rule still
; spans the same amount of input)
;
(3 = parse [1 2 3] [measure some integer!])
(3 = parse [1 2 3] [measure measure some integer!])

; Seeking back in the input causes an error, but seeking ahead is ok.
;
~???~ !! (
    parse "abbbc" [pos: <here>, "a", measure [some "b" seek (pos)]]
)
(4 = parse "abbbc" [
    ahead [thru "c" pos: <here>]
    "a" measure [some "b" seek (pos)]
])
