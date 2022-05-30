; %loops/for-next.test.reb

(
    success: true
    for-next i "a" [continue, success: false]
    success
)
(
    success: true
    for-next i [a] [continue, success: false]
    success
)
; text! test
(
    out: copy ""
    for-next i "abc" [append out i]
    out = "abcbcc"
)
; block! test
(
    out: copy []
    for-next i [1 2 3] [append out i]
    out = [1 2 3 2 3 3]
)
; TODO: is hash! test and list! test needed too?


; REPEAT in Rebol2 with an ANY-SERIES! argument acted like a FOR-EACH on that
; series.  This is redundant with FOR-EACH.
;
; R3-Alpha changed the semantics to be like a FOR-NEXT (e.g. FORALL) where you
; could specify the loop variable instead of insisting your loop variable be
; the data you are iterating.
;
; Red forbids ANY-SERIES! as the argument of what to iterate over.
;
; https://trello.com/c/CjEfA0ef
(
    out: copy ""
    for-next i "abc" [append out first i]
    out = "abc"
)
(
    out: copy []
    for-next i [1 2 3] [append out first i]
    out = [1 2 3]
)

(none? for-each x [1 2 3] [maybe if x != 3 [x]])
