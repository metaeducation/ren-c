; %switch2.control.test.reb
;
; This is a test for building a usermode switch construct which is able to do
; creative matching of partial clauses

(
log: :elide

switch2: func [
    value [<opt> any-value!]
    cases [block!]
    /all
    <local> switch2-all more found result' condition branch
][
    (switch2-all: :all, all: :lib.all)  ; Should be a spec dialect feature!

    found: false
    result': @void
    more: true

    uparse cases [cycle [
        uwhile :(more) [
            ; Find next condition clause, or break loop if we hit => or end
            ;
            condition: between <here> any [
                ['| (more: true)]
                ['=> (more: false)]
                [<end> return (unmeta result')]
            ]

            any [
                ; If we've already found a condition that matched, skip any
                ; following alternates.
                ;
                [:(found) (log ["Skipping condition:" mold condition])]

                ; Otherwise, try building a frame for the condition.  If the
                ; frame is incomplete, slip the switch value into an ~end~ slot
                (
                    log ["Testing condition:" mold condition]

                    let f: make frame! make varargs! condition
                    for-each param (parameters of action of f) [
                        all [word? param, '~end~ = ^f.(param)] then [
                            f.(param): :value
                            break
                        ]
                    ]
                    log ["Built frame for condition:" mold f]

                    if found: to-logic do f [
                        log "Result was truthy, skipping to branch!"
                    ] else [
                        log "Result was falsey, seeking next condition."
                    ]
                )
            ]
        ]

        (more: true)

        branch: [block! | (fail "Code must be BLOCK! in SWITCH2 (for now)")]
        maybe some ',  ; skip commas between branch and next condition

        any [
            ; If we didn't find a matching condition, skip over this branch.
            ;
            [:(not found) (log ["Skipping branch:" mold branch])]

            ; Otherwise, run the branch.  Keep going if we are using /ALL,
            ; else return whatever that branch gives back.
            [
                (result': ^ if true :branch)  ; branch semantics for null, void

                [:(not switch2-all) return (unmeta result')]

                (found: false)
            ]
        ]
    ]]
]
true)

(#i = switch2 1020 [match integer! => [#i], match tag! => [#t]])

(#t = switch2 <ren-c> [match integer! => [#i], match tag! => [#t]])
