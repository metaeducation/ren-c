; %pack-old.test.r
;
; Prior to the creation of antiform-BLOCK!-packs, this function was created
; to replace the idiom of using SET on a BLOCK! that you REDUCE.  It used
; infix to relate a SET-BLOCK! on the left to reduced expressions on the right,
; to work around issues related to antiform states that can't be in blocks.
;
; This was then achieved in a generalized fashion in the evaluator with the
; antiform blocks, and their intrinsic relationship with SET-BLOCK!.
;
; The old code is kept here just as a test that exercises some features (and
; as a demonstration that usermode infix is quite powerful for implementing
; dialecting multi-returns for those who want something different from what
; the system's SET-BLOCK! offers).
;
; Note: The original SET facility was agnostic about what kind of word it
; was setting.  See #1745.
;
;    Rebol2>> set [a 'b :c d: /e] [1 2 3 4 5]
;    == [1 2 3 4 5]
;
; This wasn't supported by PACK-OLD, and SET-BLOCK! also has a more
; dialected notion of what the words represent.


[(pack-old: infix func [
    "Prepare a BLOCK! of values for storing each in a SET-BLOCK!"
    return: [any-stable?]
    @vars [set-block? set-group?]
    block "Reduced if normal [block], but values used as-is if @[block]"
        [block! @block!]
][
    if group? vars: unchain vars [vars: eval vars]

    ; Want to reduce the block ahead of time, because we don't want partial
    ; writes to the results (if one is written, all should be)
    ;
    ; (Hence need to do validation on the ... for unpacking and COMPOSE the
    ; vars list too, but this is a first step.)
    ;
    block: if match [@block!] block [
        map-each 'item unpin block [quote item]
    ]
    else [
        reduce:predicate block lift/
    ]

    let ^result: ^void
    for-each [val'] block [
        if void? ^result [
            ^result: either space? vars.1 [^void] [unlift val']
        ]
        if tail? vars [
            panic "Too many values for vars in PACK (use <...> if on purpose)"
        ]
        if vars.1 = <...> [
            continue  ; ignore all other values (but must reduce all)
        ]
        switch:type vars.1 [
            space?/ []  ; no assignment
            word! tuple! [set inside vars vars.1 unlift val']
            word?:metaform/ [set inside vars vars.1 val']
        ]
        vars: my next
    ]
    if <...> = try vars.1 [
        if not last? vars [
            panic "<...> must appear only at the tail of PACK variable list"
        ]
    ] else [
        ; We do not error on too few values (such as `[a b c]: [1 2]`) but
        ; instead unset the remaining variables (e.g. `c` above).  There could
        ; be a refinement to choose whether to error on this case.
        ;
        for-each 'var vars [  ; if not enough values for variables, unset
            if not space? var [unset inside vars var]
        ]
    ]
    return ^result
], ok)

(
    a: b: ~#bad~
    all [
        3 = [a b]: pack-old [1 + 2 3 + 4]
        a = 3
        b = 7
    ]
)

(
    a: b: ~#bad~
    all wrap [
        1 = [a b c]: pack-old @[1 + 2]
        a = 1
        b = '+
        c = 2
    ]
)

; <...> is used to indicate willingness to discard extra values
(
    all wrap [
        1 = [a b <...>]: pack-old @[1 2 3 4 5]
        a = 1
        b = 2
    ]
)

(
    a: <before>
    '~null~ = [a]: pack-old pin reduce:predicate [null] reify/
    '~null~ = a
)
(
    a: <a-before>
    b: <b-before>
    2 = [a b]: pack-old pin reduce:predicate [2 null] reify/
    a = 2
    '~null~ = b
)

(a: 1 b: null [b]: pack-old pin [a] b = 'a)

(
    a: 10
    b: 20
    all wrap [space = [a b]: pack-old @[_ _], space? a, space? b]
)
(
    a: 10
    b: 20
    c: 30
    [a b c]: pack-old [null 99]
    all [null? a, b = 99, unset? $c]
)

(
    a: 10
    b: 20
    c: 30
    [a b c]: pack-old [~null~ 99]
    all [null? a, b = 99, unset? $c]
)]
