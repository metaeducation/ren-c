; %collect.test.reb
;
; The original COLLECT didn't have any tests in the Saphirion collection.
; It actually is tested pretty well by virtue of being used a lot, but the
; subtlety of the return value of KEEP (not used that often) as well as the
; behavior of COLLECT* is something that wasn't getting validated. 


; COLLECT* is the lower-level operation that returns NULL if it opts out of
; collecting with BLANK!s or has no collects.  Empty blocks count as asking
; to collect emptiness.
[
    (null = collect* [])
    ([] = collect [])

    (null = collect* [assert [null = (keep _)]])
    ([] = collect [assert [null = (keep _)]])

    ([] = collect* [assert [[] = (keep [])]])
    ([] = collect [assert [[] = (keep [])]])
]
