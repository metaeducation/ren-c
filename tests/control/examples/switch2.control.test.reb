; %switch2.control.test.reb
;
; This is a test for building a usermode switch construct which is able to do
; creative matching of partial clauses

[(
log: elide/

switch2: func [
    value [any-value?]
    cases [block!]
    /multi
    <local> more found result' condition branch
][
    found: 'no
    result': ^void
    more: 'yes

    return parse cases [cycle [
        while :(yes? more) [
            ; Find next condition clause, or break loop if we hit => or end
            ;
            condition: *in* between <here> any [
                ['| (more: 'yes)]
                ['=> (more: 'no)]
                [<end> accept (unmeta result')]
            ]

            any [
                ; If we've already found a condition that matched, skip any
                ; following alternates.
                ;
                [when (yes? found) (log ["Skipping condition:" mold condition])]

                ; Otherwise, try building a frame for the condition.  If the
                ; frame is incomplete, slip the switch value into an ~end~ slot
                (
                    log ["Testing condition:" mold condition]

                    let f: make frame! make varargs! condition
                    for-each 'param (parameters of f) [
                        all [word? param, '~end~ = ^f.(param)] then [
                            f.(param): :value
                            break
                        ]
                    ]
                    log ["Built frame for condition:" mold f]

                    if yes? found: to-yesno eval f [
                        log "Result was truthy, skipping to branch!"
                    ] else [
                        log "Result was falsey, seeking next condition."
                    ]
                )
            ]
        ]

        (more: 'yes)

        branch: [
            *in* block!
            |
            (fail "Code must be BLOCK! in SWITCH2 (for now)")
        ]
        opt some ',  ; skip commas between branch and next condition

        any [
            ; If we didn't find a matching condition, skip over this branch.
            ;
            [when (no? found) (log ["Skipping branch:" mold branch])]

            ; Otherwise, run the branch.  Keep going if we are using /MULTI,
            ; else return whatever that branch gives back.
            (
                result': ^ if ok (branch)  ; IF semantics for null, void
                if not multi [return unmeta result']
                found: 'no
            )
        ]
    ]]
]
ok)

(#i = switch2 1020 [match integer! => [#i], match tag! => [#t]])

(#t = switch2 <ren-c> [match integer! => [#i], match tag! => [#t]])

([<integer!> <any-value?>] = collect [
    switch2/multi 1 [
        match integer! => [keep <integer!>]
        match &any-value? => [keep <any-value?>]
    ]
])
]
