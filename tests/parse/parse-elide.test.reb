; %parse-elide.test.reb
;
; Invisibles are another aspect of UPARSE...where you can have a rule match
; but not contribute its synthesized value to the result.

("a" = uparse "aaab" [[some "a" elide "b"]])

(
    j: ~
    did all [
        null = uparse "b" [[(1000 + 20) elide (j: 304)]]
        j = 304
    ]
)

(
    j: ~
    did all [
        1020 = uparse "b" ["b" [(1000 + 20) elide (j: 304)]]
        j = 304
    ]
)

("a" = uparse "a" ["a" (comment "GROUP! content can vaporize too!")])

; ELIDE doesn't elide failure...just the result on success.
;
(didn't uparse "a" [elide "b"])
