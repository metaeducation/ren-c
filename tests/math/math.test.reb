; %math.test.reb
;
; This MATH implementation is adapted Gabrielle Santilli circa 2001, found
; via http://www.rebol.org/ml-display-thread.r?m=rmlXJHS. It implements the
; much-requested (by new users) idea of * and / running before + and - in
; math expressions. Expanded to include functions.
;
; It is not working at time of writing but is moved into tests.

(math: func [
    {Process expression taking "usual" operator precedence into account.}

    expr [block!]
        {Block to evaluate}
    /only
        {Translate operators to their prefix calls, but don't execute}

    ; !!! This creation of static rules helps avoid creating those rules
    ; every time, but has the problem that the references to what should
    ; be locals are bound to statics as well (e.g. everything below which
    ; is assigned with BLANK! really should be relatively bound to the
    ; function, so that it will refer to the specific call.)  It's not
    ; technically obvious how to do that, not the least of the problem is
    ; that statics are currently a usermode feature...and injecting relative
    ; binding information into something that's not the function body itself
    ; isn't implemented.

    <static>

    slash (the /)

    expr-val (null)

    expr-op (null)

    expression  ([
        term (expr-val: term-val)
        try some [
            ['+ (expr-op: 'add) | '- (expr-op: 'subtract)]
            term (expr-val: compose [(expr-op) (expr-val) (term-val)])
        ]
        <end>
    ])

    term-val (null)

    term-op (null)

    term ([
        pow (term-val: power-val)
        try some [
            ['* (term-op: 'multiply) | slash (term-op: 'divide)]
            pow (term-val: compose [(term-op) (term-val) (power-val)])
        ]
    ])

    power-val (null)

    pow ([
        unary (power-val: unary-val)
        try ['** unary (power-val: compose [power (power-val) (unary-val)])]
    ])

    unary-val (null)

    pre-uop (null)

    post-uop (null)

    unary ([
        (post-uop: pre-uop: [])
        try ['- (pre-uop: 'negate)]
        primary
        try ['! (post-uop: 'factorial)]
        (unary-val: compose [(post-uop) (pre-uop) (prim-val)])
    ])

    prim-val (null)

    primary ([
        set prim-val any-number!
        | set prim-val [word! | path!] (prim-val: reduce [prim-val])
            ; might be a funtion call, looking for arguments
            try some [
                nested-expression (append prim-val take nested-expr-val)
            ]
        | ahead group! into nested-expression (prim-val: take nested-expr-val)
    ])

    p-recursion (null)

    nested-expr-val ([])

    save-vars (func [][
            p-recursion: reduce [
                :p-recursion :expr-val :expr-op :term-val :term-op :power-val :unary-val
                :pre-uop :post-uop :prim-val
            ]
        ])

    restore-vars (func [][
            set [
                p-recursion expr-val expr-op term-val term-op power-val unary-val
                pre-uop post-uop prim-val
            ] p-recursion
        ])

    nested-expression ([
            ;all of the static variables have to be saved
            (save-vars)
            expression
            (
                ; This rule can be recursively called as well,
                ; so result has to be passed via a stack
                insert nested-expr-val expr-val
                restore-vars
            )
            ; vars could be changed even it failed, so restore them and fail
            | (restore-vars) fail

    ])
][
    clear nested-expr-val
    let res: if try parse3 expr expression [expr-val] else [blank]

    either only [
        return res
    ][
        ret: reduce res
        all [
            1 = length of ret
            any-number? ret.1
        ] else [
            fail [
                unspaced ["Cannot be REDUCED to a number (" mold ret ")"]
                ":" mold res
            ]
        ]
        return ret.1
    ]
], true)
