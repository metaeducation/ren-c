; %comment.test.r
;
; COMMENT returns GHOST!, which is an unstable antiform.  It limits the
; number of types it takes in order to avoid the illusion that it's
; could suppress evaluations, e.g. consider (comment print "HI").  Since
; PRINT is an evaluator-active WORD!, it would just be commenting out
; that PRINT word but leave behind the "HI".

(1 = eval [comment "a" 1])
(1 = eval [1 comment "a"])
(void? comment "a")
(void? (comment "a"))

(3 = (1 + 2 comment "invisible"))

('~,~ = (lift comment "a"))
((quote '~,~) = lift (lift comment "a"))

('~,~ = lift eval [comment "a"])
((quote '~,~) = lift (lift eval [comment "a"]))

; !!! At one time, comment mechanics allowed comments to be infix such that
; they ran as part of the previous evaluation.  This is no longer the case,
; as invisible mechanics no longer permit interstitials--which helps make
; the evaluator more sane, without losing the primary advantages of invisibles.
;
; https://forum.rebol.info/t/1582

~no-value~ !! (
    [pos val]: evaluate:step [
        1 + comment "a" comment "b" 2 * 3 panic "too far"
    ]
)
(
    [pos val]: evaluate:step [
        1 comment "a" + comment "b" 2 * 3 panic "too far"
    ]
    all [
        val = 1
        pos = [comment "a" + comment "b" 2 * 3 panic "too far"]
    ]
)
(
    [pos val]: evaluate:step [
        1 comment "a" comment "b" + 2 * 3 panic "too far"
    ]
    all [
        val = 1
        pos = [comment "a" comment "b" + 2 * 3 panic "too far"]
    ]
)

~no-value~ !! (
    1 + 2 (comment "stale") + 3
)
