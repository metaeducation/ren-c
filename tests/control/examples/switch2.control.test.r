; %switch2.control.test.r
;
; This is a test for building a usermode switch construct which is able to do
; creative matching of partial clauses

[(
log: elide/

switch2: lambda [
    value [any-stable?]
    cases [block!]
    :multi
    {more (okay) found (null) ^result condition branch}
][
    parse cases [cycle [
        while cond (more) [
            ; Find next condition clause, or break loop if we hit => or end
            ;
            condition: *in* between <here> any [
                ['| (more: okay)]
                ['=> (more: null)]
                [<end> accept (^result)]
            ]

            any [
                ; If we've already found a condition that matched, skip any
                ; following alternates.
                ;
                [cond (found) (log ["Skipping condition:" mold condition])]

                ; Otherwise, try building a frame for the condition.  If the
                ; frame is incomplete, slip switch value into antiform ~ slot
                (
                    log ["Testing condition:" mold condition]

                    let f: make frame! make varargs! condition
                    for-each [key ^val] f [
                        if unset? $val [
                            f.(key): value
                            break
                        ]
                    ]
                    log ["Built frame for condition:" mold f]

                    if found: did eval f [
                        log "Result was truthy, skipping to branch!"
                    ] else [
                        log "Result was falsey, seeking next condition."
                    ]
                )
            ]
        ]

        (more: okay)

        branch: [
            *in* block!
            |
            panic "Code must be BLOCK! in SWITCH2 (for now)"
        ]
        opt some ',  ; skip commas between branch and next condition

        any [
            ; If we didn't find a matching condition, skip over this branch.
            ;
            [cond (not found) (log ["Skipping branch:" mold branch])]

            ; Otherwise, run the branch.  Keep going if we are using :MULTI,
            ; else return whatever that branch gives back.
            (
                ^result: heavy eval branch  ; branch semantics for null
                if not multi [return ^result]
                found: null
            )
        ]
    ]]
]
ok)

(#i = switch2 1020 [match integer! => [#i], match tag! => [#t]])

(#t = switch2 <ren-c> [match integer! => [#i], match tag! => [#t]])

([<integer!> <any-stable?>] = collect [
    switch2:multi 1 [
        match integer! => [keep <integer!>]
        match any-stable?/ => [keep <any-stable?>]
    ]
])
]
