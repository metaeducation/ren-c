; functions/control/repeat.r
(
    success: true
    num: 0
    repeat i 10 [
        num: num + 1
        success: success and (i = num)
    ]
    success and (10 = num)
)
; cycle return value
(false = repeat i 1 [false])
; break cycle
(
    num: 0
    repeat i 10 [num: i break]
    num = 1
)
; break return value
(null? repeat i 10 [break])
; continue cycle
(
    success: true
    repeat i 1 [continue, success: false]
    success
)
(
    success: true
    repeat i "a" [continue, success: false]
    success
)
(
    success: true
    repeat i [a] [continue, success: false]
    success
)
; decimal! test
([1 2 3] == collect [repeat i 3.0 [keep i]])
([1 2 3] == collect [repeat i 3.1 [keep i]])
([1 2 3] == collect [repeat i 3.5 [keep i]])
([1 2 3] == collect [repeat i 3.9 [keep i]])
; text! test
(
    out: copy ""
    repeat i "abc" [append out i]
    out = "abcbcc"
)
; block! test
(
    out: copy []
    repeat i [1 2 3] [append out i]
    out = [1 2 3 2 3 3]
)
; TODO: is hash! test and list! test needed too?
; zero repetition
(
    success: true
    repeat i 0 [success: false]
    success
)
(
    success: true
    repeat i -1 [success: false]
    success
)
; Test that return stops the loop
(
    f1: func [] [repeat i 1 [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    e: repeat i 2 [num: i trap [1 / 0]]
    all [error? e num = 2]
)
; "recursive safety", "locality" and "body constantness" test in one
(repeat i 1 b: [not same? 'i b/3])
; recursivity
(
    num: 0
    repeat i 5 [
        repeat i 2 [num: num + 1]
    ]
    num = 10
)
; local variable type safety
(
    test: false
    error? trap [
        repeat i 2 [
            either test [i == 2] [
                test: true
                i: false
            ]
        ]
    ]
)


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
    repeat i "abc" [append out first i]
    out = "abc"
)
(
    out: copy []
    repeat i [1 2 3] [append out first i]
    out = [1 2 3]
)
