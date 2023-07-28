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
    pointfree: specialize* (enclose :pointfree* lambda [f] [
        set let action f.action: (match action! any [  ; no SET-WORD! namecache
            if match [word! path!] f.block.1 [unrun get/any f.block.1]
        ]) else [
            fail "POINTFREE requires ACTION! argument at head of block"
        ]

        ; rest of block is invocation by example
        f.block: skip f.block 1  ; Note: NEXT not defined yet

        inherit-meta (do f) action  ; don't SET-WORD! cache name
    ])[
        action: unrun :panic/value  ; overwritten, best to make something mean
    ], true
)(
    <-: enfix func* [
        {Declare action by example instantiation, missing args unspecialized}

        return: [activation?]
        :left "Enforces nothing to the left of the pointfree expression"
            [<end>]
        :expression "POINTFREE expression, BLANK!s are unspecialized arg slots"
            [any-value! <variadic>]
    ][
        return pointfree make block! expression  ; !!! vararg param for efficiency?
    ], true
)

(
    x: [1 2 3 4 5 6]
    did all [
        5 = until/predicate [take x] (<- greater? _ 4)
        x = [6]
    ]
)]
