; %parse-phase.test.reb
;
; The concept of "phase" is very new and not completely figured out yet, but it deals
; with "deferred" groups.  It may be that you can label deferred groups, e.g.:
;
;    (<defer>.A, print "this is defer group A")
;
; Then the PHASE combinator could be somehow parameterized by which phase you wanted
; to act upon, perhaps by saying [phase #A [...]] or something like that.  In any
; case, right now there's an implicit PHASE wrapping UPARSE* and UPARSE.

; Delayed groups automatically run at the end of a successful parse
(
    earlier: copy []
    later: copy []
    did all [
        "d" = uparse "abcd" [
           "a" (append earlier <A>)
           "b" (<delay> append later <B>)
           "c" (append earlier <C>)
           "d" (<delay> append later <D>)
           (assert [earlier == [<A> <C>]])
           (assert [later == []])
        ]
        earlier == [<A> <C>]
        later == [<B> <D>]
    ]
)

; A failed parse will not run the delayed groups
(
    earlier: copy []
    later: copy []
    did all [
        didn't uparse "abcd" [
           "a" (append earlier <A>)
           "b" (<delay> append later <B>)
           "c" (append earlier <C>)
           "d" (<delay> append later <D>)
           (assert [earlier == [<A> <C>]])
           (assert [later == []])
           "e"
        ]
        earlier == [<A> <C>]
        later == []
    ]
)

; Introducing a PHASE will cause delayed groups to run explicitly
(
    earlier: copy []
    later: copy []
    did all [
        didn't uparse "abcd" [
           phase [
               "a" (append earlier <A>)
               "b" (<delay> append later <B>)
               "c" (append earlier <C>)
               "d" (<delay> append later <D>)
               (assert [earlier == [<A> <C>]])
               (assert [later == []])
           ]
           (assert [earlier == [<A> <C>]])
           (assert [later == [<B> <D>]])
           "e"
        ]
        earlier == [<A> <C>]
        later == [<B> <D>]
    ]
)
