; %for-both.test.r
;
; FOR-BOTH was an early talking point for an extremely simple usermode loop
; construct that would be built out of two FOR-EACH loops:
;
;     >> for-both x [1 2] [3 4] [print [x]]
;     1
;     2
;     3
;     4
;
; A naive implementation of this in Rebol2 might look like:
;
;     for-both-naive: func ['var blk1 blk2 body] [
;         foreach (var) blk1 body
;         foreach (var) blk2 body
;     ]
;
; But critically, that will not honor BREAK correctly:
;
;     >> for-both-naive x [1 2] [3 4] [if x = '2 [break], print [x]]
;     1
;     3  ; the BREAK only broke the first FOR-EACH
;     4
;
; There's no way from the outside of Rebol2 or Red's FOREACH to know for sure
; that a BREAK was requested.  Complex binding to search the loop bodies and
; bind them would be needed--even for this simple goal.
;
; There's also a property that loops are supposed to have to make the last body
; evaluation "drop out".  But if the second series is empty, the fallout from
; the first loop is forgotten:
;
;     >> for-both-naive x [1 2] [] [print [x], x * 10]
;     1  ; evaluated to 10
;     2  ; evaluated to 20
;     == #[none!]  ; Rebol2's `foreach x [] [<n/a>]`, Red is #[unset]
;
; Addressing these problems rigorously is basically impossible in Rebol2 and
; Red.  But Ren-C has painstakingly developed rules and mechanisms to empower
; preserving these loop properties (as well as more sophisticated ones).  The
; methods apply to situations much more complex than FOR-BOTH, but it was a
; valuable example to focus on while putting the pieces in place.

[
    (
        for-both: lambda [var blk1 blk2 body] [
            unlift:lite all [
                lift:lite for-each var blk1 body
                lift:lite for-each var blk2 body
            ]
        ]

        ok
    )

    ; ^-- This is a LAMBDA instead of a FUNCTION so that no RETURN is needed
    ; (the result "drops out" the bottom of lambdas, and there is no RETURN
    ; definition in effect).
    ;
    ; ^-- Note that this uses LIFT:LITE.  The reason is that it wants to leave
    ; voids and nulls as-is, to serve as the signal for breaking or opting out
    ; of contributing to the final loop result:
    ;
    ;     >> lift ^void
    ;     == ~[]~
    ;
    ;     >> lift:lite ^void
    ;     == ~[]~  ; anti
    ;


    ; If you're the sort to throw softballs, this would be the only case you
    ; would write.  (Perhaps good enough for some Redbols, but not Ren-C!)

    ([1 2 3 4] = collect [
        assert [40 = for-both 'x [1 2] [3 4] [keep x, x * 10]]
    ])

    ; Saves result from second loop output, due to META:LITE vanishing on the
    ; void produced by contract when FOR-EACH does not run.

    ([1 2] = collect [
        assert [20 = for-both 'x [1 2] [] [keep x, x * 10]]
    ])

    ; The all-important support of BREAK... META:LITE of NULL remains NULL, and
    ; is falsey to short circuit the ALL.

    ([1] = collect [
        assert [
            null = for-both 'x [1 2] [3 4] [
                if x = 2 [break]  ; break the first loop
                keep x
            ]
        ]
    ])

    ([1 2 3] = collect [
        assert [
            null = for-both 'x [1 2] [3 4] [
                if x = 4 [break]  ; break the second loop
                keep x
            ]
        ]
    ])

    ; It's not possible to return a "pure NULL" otherwise.  But the existence
    ; of ~[~null~]~ antiforms permit a non-break-signaling construct that
    ; carries semantic intent of a "there's an answer and it is null"

    ([1 2 3 4] = collect [
        let result': ~
        assert [
            '~[~null~]~ = result': lift for-both 'x [1 2] [3 4] [
                keep x
                null
            ]
            result' = '~[~null~]~
        ]
    ])

    ([1 2 3 4] = collect [
        let result: ~
        assert [
            null = result: for-both 'x [1 2] [3 4] [
                keep x
                null
            ]
            result = null  ; it decayed to pure NULL in the assignment
        ]
    ])

    ; The contract of returning VOID is preserved when no loop bodies
    ; run, as both FOR-EACH in the ALL have their contributions erased
    ; and effectively leave behind an `all []`.  Ren-C's working definition
    ; (motivated by this kind of example) is that should produce a VOID
    ; as well.  Technical reasons besides this scenario lead it to be favorable
    ; for UNMETA:LITE to be willing to take VOID and return it as-is instead
    ; of raising an error, and that plays to our advantage here.

    (void? for-both 'x [] [] [panic "Body Never Runs"])

    (
        <something> = eval [
            <something>
            elide-if-void for-both 'x [] [] [panic "Body Never Runs"]
        ]
    )

    ; Handles antiforms (^META operations make quasiforms, these are truthy, so
    ; the only falsey possibility is the BREAK)

    ([1 2 3 4] = collect [
        assert [
            '~#bad~ = lift for-both 'x [1 2] [3 4] [
                keep x
                ~#bad~  ; makes antiform
            ]
        ]
    ])

    ; FOR-BOTH provides a proof of why this is the case:
    ;
    ;     >> for-each 'x [1 2] [if x = 2 [continue]]
    ;     == ~  ; anti
    ;
    ; Plain void is reserved for "loop didn't run", and we do not want
    ; a loop that consists of just CONTINUE to lie and say the body of the
    ; loop didn't run.  It forces our hand to return something else to
    ; convey a void intent.  "Heavy voids" would be undecayable (~[~[]~]~)
    ; so we fall back on trash for this edge case.

    (trash? for-both 'x [1 2] [3 4] [if x > 2 [continue] x * 10])

    (trash? for-both 'x [1 2] [3 4] [comment "Maintain invariant!"])
]
