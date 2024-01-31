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
    pointfree*: func* [
        {Specialize by example: https://en.wikipedia.org/wiki/Tacit_programming}

        return: [action?]
        frame [<unrun> frame!]  ; this lower version takes frame AND a block
        block [block!]
        <local> params var
    ][
        ; If we did a GET of a PATH! it comes back as a partially specialized
        ; function, where the refinements are reported as normal args at the
        ; right spot in the evaluation order.  (e.g. GET @APPEND/DUP returns a
        ; function where DUP is a plain WORD! parameter in the third spot).
        ;
        ; We prune out any unused refinements for convenience.
        ;
        params: map-each w parameters of frame [
            match [word! lit-word? get-word!] w  ; !!! what about skippable?
        ]

        frame: make frame! frame

        ; Step through the block we are given--first looking to see if there is
        ; a BLANK! in the slot where a parameter was accepted.  If it is blank,
        ; then leave the parameter null in the frame.  Otherwise take one step
        ; of evaluation or literal (as appropriate) and put the parameter in
        ; the frame slot.
        ;
        for-skip p params 1 [
            case [
                ; !!! Ue STRICT-EQUAL?, else '_ says type equal to blank
                blank? block.1 [block: skip block 1]

                match word! p.1 [
                    if not (var: evaluate/next block @block, block) [
                        break  ; out of args, assume remaining unspecialized
                    ]
                    frame.(p.1): get/any @var
                ]

                all [
                    match [lit-word?] p.1
                    match [group! get-word! get-path!] block.1
                ][
                    frame.(p.1): reeval block.1
                    block: skip block 1  ; NEXT not defined yet
                ]

                true [  ; hard literal argument or non-escaped soft literal
                    frame.(p.1): block.1
                    block: skip block 1  ; NEXT not defined yet
                ]
            ]
        ]

        if block and (:block.1) [
            fail 'block ["Unused argument data at end of POINTFREE block"]
        ]

        ; We now create an action out of the frame.  trash parameters are
        ; taken as being unspecialized and gathered at the callsite.
        ;
        return runs frame
    ]

    pointfree: specialize (enclose :pointfree* lambda [f] [
        set let frame f.frame: (match frame! any [  ; no SET-WORD! namecache
            if match [word! path!] f.block.1 [unrun get/any f.block.1]
        ]) else [
            fail "POINTFREE requires FRAME! argument at head of block"
        ]

        ; rest of block is invocation by example
        f.block: skip f.block 1  ; Note: NEXT not defined yet
    ])[
        frame: unrun :panic/value  ; overwritten, best to make something mean
    ]

    <-: enfix func* [
        {Declare action by example instantiation, missing args unspecialized}

        return: [action?]
        :left "Enforces nothing to the left of the pointfree expression"
            [<end>]
        :expression "POINTFREE expression, BLANK!s are unspecialized arg slots"
            [element? <variadic>]
    ][
        return pointfree make block! expression  ; !!! vararg param, efficiency?
    ]

    true
)

(
    x: [1 2 3 4 5 6]
    did all [
        5 = until/predicate [take x] (<- greater? _ 4)
        x = [6]
    ]
)]
