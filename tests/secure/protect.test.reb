; functions/secure/protect.r
;
; PROTECT is able to protect values (which involves a bit set on the value
; cell).  It also can protect variables--which in R3-Alpha involved setting a
; bit on a context's key slot.  But that was buggy since keys could be reused
; in multiple objects...so Ren-C also puts the protection of variables as a
; bit on the value slot where the variable keeps its storage, so it is a
; per-instance bit.
;
; So you can have a variable called B which is unchangeable holding a mutable
; BLOCK! value, or a variable called B which is changeable holding an immutable
; BLOCK! value...

~protected-word~ !! (
    b: 10
    protect $b
    b: 20
)


[#1748
    (
        /rescue-protected?: func [code [block!]] [
            for-each 'original [
                [1 + 2 + 3]
                -{1 + 2 + 3}-
                #{01FF02FF03}
            ] (wrap [
                protect value: copy original
                sys.util/rescue code then e -> [
                    any [
                        e.id <> 'series-protected
                        not equal? value original
                    ][
                        ; print ["Original:" mold original]
                        ; print ["Value:" mold value]
                        ; print ["Code:" print mold code]
                        fail e
                    ]
                ]
            ])
            return ok
        ]
        ok
    )

    (rescue-protected? [insert value 4])
    (rescue-protected? [append value 4])
    (rescue-protected? [change value 4])
    (rescue-protected? [poke value 1 4])
    (rescue-protected? [remove:part value 1])
    (rescue-protected? [take value])
    (rescue-protected? [reverse value])
    (rescue-protected? [clear value])
]

[#1764
    (blk: ~ protect:deep $blk, ok)
    (unprotect $blk, ok)
]


; TESTS FOR TEMPORARY EVALUATION HOLDS
; These should elaborated on, and possibly be in their own file.  Simple tests
; for now.
[
    ~series-held~ !! (eval code: [clear code])

    ~series-held~ !! (
        obj: make object! [x: 10]
        eval code: [obj.x: (clear code recycle 20)]
    )
]


; HIDDEN VARIABLES SHOULD STAY HIDDEN
;
; The bit indicating hiddenness lives on the variable slot of the context.
; This puts it at risk of being overwritten by other values...though it is
; supposed to be protected by masking operations.  Make sure changing the
; value doesn't un-hide it...
(
    obj: make object! [x: 10, y: 20]
    word: bind obj 'y
    all [
        20 = get word
        [x y] = words of obj  ; starts out visible

        elide protect:hide $obj.y
        [x] = words of obj  ; hidden
        20 = get word  ; but you can still see it

        set word 30  ; and you can still set it
        [x] = words of obj  ; still hidden
    ]
)
