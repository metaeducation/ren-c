; %parse-measure.test.reb
;
; Inspired by converting TRIM to UPARSE and calculating TRIM/AUTO indents.
; This measures the length in series units across a match.

(3 = uparse "aaa" [measure some "a"])
(null = uparse "aaa" [measure some "b"])

; Applying MEASURE to itself should return the same thing (rule still
; spans the same amount of input)
;
(3 = uparse [1 2 3] [measure some integer!])
(3 = uparse [1 2 3] [measure measure some integer!])

; Seeking back in the input causes an error, but seeking ahead is ok.
;
(error? trap [
    uparse "abbbc" [pos: <here>, "a", measure [some "b" seek (pos)]]
])
(4 = uparse "abbbc" [
    ahead [thru "c" pos: <here>]
    "a" measure [some "b" seek (pos)]
])
