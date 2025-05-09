; %pointfree.test.reb
;
; Pointfree was removed from the bootstrap, and is instead being moved to a
; test file.
;
; The lower-level pointfree function separates out the action it takes, but
; the higher level one uses a block.  Specialize out the action, and then
; overwrite it in the enclosure with an action taken out of the block.
;


[(
    ; POINTFREE* is the lower-level version that takes FRAME! and BLOCK! of
    ; just the arguments.
    ;
    ; NOTE: Variadics are not handled at this time.
    ;
    ; 1. If what gets passed in is something like :append, then we don't wants
    ;    to be modifying that directly.  We make a copy.  This *should* be
    ;    done with just `copy frame`, but that is residually a lightweight
    ;    way to make a Details identity that resists hijacking.
    ;
    ; 2. If we did a GET of a PATH! it comes back as a partially specialized
    ;    function, where the refinements are reported as normal args at the
    ;    right spot in the evaluation order.  (e.g. GET $APPEND:DUP returns a
    ;    function where DUP is a plain WORD! parameter in the third spot).
    ;
    ; 3. If the user does (pointfree :append [_ [d e]]) then the blank signals
    ;    an unspecialized slot.  So that would be like writing:
    ;
    ;        specialize append/ [value: [d e]]  ; leave series unspecialized
    ;
    pointfree*: func [
        "Specialize by example: https://en.wikipedia.org/wiki/Tacit_programming"

        return: [action!]
        frame [<unrun> frame!]
        block "Invocation by example (BLANK!s are unspecialized)"
            [block!]
        <local> e
    ][
        frame: copy frame  ; don't mutate incoming frame [1]

        for-each [key param] (parameters of frame) [
            if tail? block [break]  ; no more args, leave rest unspecialized

            if blank? block.1 [  ; means leave unspecialized [3]
                block: skip block 1  ; this code avoided NEXT when mezzanine
                continue
            ]

            if param.optional [
                continue  ; skip unused refinements [2]
            ]

            switch param.class [
                'normal [
                    [block frame.(key)]: evaluate:step block
                ]

                'meta [
                    [block ^frame.(key)]: evaluate:step block
                ]

                'just  ; !!! review binding nuance
                'the [
                    if param.escapable and (group? block.1) [
                        frame.(key): reeval block.1
                    ]
                    else [
                        frame.(key): block.1
                    ]
                    block: skip block 1  ; avoided NEXT when mezzanine
                ]

                fail ~<unexpected parameter class>~
            ]
        ]

        if not tail? block [
            fail:blame [
                "Unused argument data at end of POINTFREE block"
            ] $block
        ]

        return runs frame
    ]

    ; POINTFREE is the higher-level version that wants the name of an action
    ; at the head of the block, e.g.
    ;
    ;      pointfree [append _ [d e]]
    ;      =>
    ;      pointfree* :append [_ [d e]]
    ;
    /pointfree: specialize (adapt pointfree*/ [
        frame: (match frame! any [  ; no SET-WORD! namecache
            if match [word! chain! path!] block.1 [
                unrun get:any inside block block.1
            ]
        ]) else [
            fail "POINTFREE requires FRAME! argument at head of block"
        ]

        block: skip block 1  ; Note: NEXT not defined yet
    ])[
        frame: unrun panic/  ; overwritten, but best to be something mean
    ]

    ; pf demonstrates a concept of a variadic operator that lets you express a
    ; pointfree expression inside a GROUP!
    ;
    ;    (pf append _ [d e])
    ;    =>
    ;    pointfree [append _ [d e]]
    ;    =>
    ;    pointfree* append/ [_ [d e]]
    ;
    ; Hopefully you see some of the ambition, here.
    ;
    pf: infix func [
        "Declare action by example instantiation, missing args unspecialized"

        return: [action!]
        @(left) "Enforces nothing to the left of the pointfree expression"
            [<end>]
        @expression "POINTFREE expression, BLANK!s are unspecialized arg slots"
            [element? <variadic>]
    ][
        return pointfree make block! expression  ; !!! vararg param, efficiency?
    ]

    ok
)

    (
        /apde: pointfree* append/ [_ [d e]]
        [a b c [d e]] = apde [a b c]
    )

    (
        /apde: pointfree [append _ [d e]]
        [a b c [d e]] = apde [a b c]
    )

    (
        /apde: (pf append _ [d e])
        [a b c [d e]] = apde [a b c]
    )

    (
        /apabc: pointfree* append/ [[a b c]]
        [a b c [d e]] = apabc [d e]
    )

    (
        /apabc: pointfree [append [a b c]]
        [a b c [d e]] = apabc [d e]
    )

    (
        /apabc: (pf append [a b c])
        [a b c [d e]] = apabc [d e]
    )

    ~???~ !! (
        ap12invalid: (pf append _ 1 2)  ; unused data at end
    )

    (
        /ap1twice: (pf append:dup _ 1 2)
        [a b c 1 1] = ap1twice [a b c]
    )

(
    x: [1 2 3 4 5 6]
    all [
        5 = until:predicate [take x] (pf greater? _ 4)
        x = [6]
    ]
)]
