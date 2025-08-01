; %parse-elide.test.r
;
; Invisibles are another aspect of UPARSE...where you can have a rule match
; but not contribute its synthesized value to the result.

("a" = parse "aaab" [[some "a" elide "b"]])

(
    j: ~
    all [
        error? parse "b" [[(1000 + 20) elide (j: 304)]]
        j = 304
    ]
)

(
    j: ~
    all [
        1020 = parse "b" ["b" [(1000 + 20) elide (j: 304)]]
        j = 304
    ]
)

("a" = parse "a" ["a" (comment "GROUP! content can vaporize too!")])

; ELIDE doesn't elide failure...just the result on success.
;
~parse-mismatch~ !! (parse "a" [elide "b"])
