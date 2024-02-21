; %parse-elide.test.reb
;
; Invisibles are another aspect of UPARSE...where you can have a rule match
; but not contribute its synthesized value to the result.

("a" = parse "aaab" [[some "a" elide "b"]])

(
    j: ~
    all [
        raised? parse "b" [[(1000 + 20) elide (j: 304)]]
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
(raised? parse "a" [elide "b"])
