; %for-both.test.reb
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
;         foreach :var blk1 body
;         foreach :var blk2 body
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
        for-both: func ['var blk1 blk2 body] [
            return unmeta* all [
                meta* for-each (var) blk1 body
                meta* for-each (var) blk2 body
            ]
        ]

        true
    )

    ; ^-- Note that this uses META* and not META (a.k.a. `^`)  The reason is
    ; that it wants to leave voids and nulls as-is:
    ;
    ;     >> 1 + 2 meta comment "hi"
    ;     ; null  <-- the true result, e.g. what the ^META arg would be
    ;
    ;     >> 1 + 2 meta* comment "hi"
    ;     == 3  ; <-- what we want in situations like this
    ;


    ; If you're the sort to throw softballs, this would be the only case you
    ; would write.  (Perhaps good enough for some Redbols, but not Ren-C!)

    ([1 2 3 4] = collect [
        assert [40 = for-both x [1 2] [3 4] [keep x, x * 10]]
    ])

    ; Saves result from second loop output, due to MAYBE/META vanishing on the
    ; ~()~ isotope produced by contract when FOR-EACH does not run.

    ([1 2] = collect [
        assert [20 = for-both x [1 2] [] [keep x, x * 10]]
    ])

    ; The all-important support of BREAK... META of NULL remains NULL, and
    ; is falsey to short circuit the ALL.

    ([1] = collect [
        assert [
            null = for-both x [1 2] [3 4] [
                if x = 2 [break]  ; break the first loop
                keep x
            ]
        ]
    ])

    ([1 2 3] = collect [
        assert [
            null = for-both x [1 2] [3 4] [
                if x = 4 [break]  ; break the second loop
                keep x
            ]
        ]
    ])

    ; It's not possible to return a "pure NULL" otherwise.  But the existence
    ; of ~_~ isotopes permit a non-break-signaling construct that carries
    ; semantic intent of a null, and will decay to it upon variable assignment.

    ([1 2 3 4] = collect [
        assert [did all [
            '~_~ = meta result: for-both x [1 2] [3 4] [
                keep x
                null
            ]
            result = null  ; it decayed to pure NULL in the assignment
        ]]
    ])

    ; Contract of returning ~()~ isotope is preserved when no loop bodies
    ; run, as both FOR-EACH inside the ALL have their ~()~ isotopes erased
    ; and effectively leave behind an `all []`.  Ren-C's working definition
    ; (motivated by this kind of example) is that should produce a ~()~
    ; isotope.  Technical reasons besides this scenario lead it to be favorable
    ; for UNMETA to be willing to take isotopes and return them as-is instead
    ; of raising an error, and that plays to our advantage here.

    (void? for-both x [] [] [fail "Body Never Runs"])
    (
        <something> = do [
            <something>
            for-both x [] [] [fail "Body Never Runs"]
        ]
    )

    ; Handles isotopes (^META operations make BAD-WORD!, these are truthy, so
    ; the only falsey possibility is the BREAK

    ([1 2 3 4] = collect [
        assert [
            '~bad~ = ^ for-both x [1 2] [3 4] [
                keep x
                ~bad~  ; makes isotope
            ]
        ]
    ])

    ; FOR-BOTH provides a proof of why this is true:
    ;
    ;     >> for-each x [1 2] [if x = 2 [continue]]
    ;     == ~  ; nothing isotope  <= NOT 1
    ;
    ; Isotopic void is reserved for "loop didn't run", and we do not want
    ; a loop that consists of just CONTINUE to lie and say the body of the
    ; loop didn't run.  It forces our hand to this answer.  It would be an
    ; esoteric feature anyway...more useful to have a form of MAYBE that can
    ; let the last loop iteration signal a desire for overall erasure.

    ('~()~ = ^ for-both x [1 2] [3 4] [if x > 2 [continue] x * 10])
    ('~()~ = ^ for-both x [1 2] [3 4] [comment "Maintain invariant!"])
]
