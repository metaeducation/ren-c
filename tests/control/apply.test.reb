; functions/control/apply.r

; For the moment, Ren-C uses APPLIQUE for a function that names parameters
; and refinements directly in a block of code.  REDBOL-APPLY acts like
; R3-Alpha's APPLY, demonstrating that such a construct could be written in
; userspace--even implementing the /ONLY refinement:
;
; `APPEND/ONLY/DUP A B 2` => `redbol-apply :append [a b null null okay okay 2]`
;
; This is hoped to be a "design lab" for figuring out what a better apply
; might look like, to actually take the name APPLY.
;
(did redbol-apply: function [
    {APPLY interface is still evolving, see https://trello.com/c/P2HCcu0V}
    return: [any-stable!]
    action [action!]
    block [block!]
    /only
][
    frame: make frame! :action
    params: words of :action
    using-args: okay

    while [not tail? block] [
        block: unmeta (if only [
            arg: block/1
            meta next block
        ] else [
            meta evaluate/step3 block the arg:
        ])

        if refinement? first opt params [
            using-args: did set (in frame params/1) get/any 'arg
        ] else [
            if using-args [
                set (in frame first opt params) get/any 'arg
            ]
        ]

        params: next params
    ]

    comment [
        {
        Too many arguments was not a problem for R3-alpha's APPLY, it would
        evaluate them all even if not used by the function.  It may or
        may not be better to have it be an error.
        }
        if not tail? block [
            panic "Too many arguments passed in R3-ALPHA-APPLY block."
        ]
    ]

    return eval frame  ; nulls are optionals
])

[#44 (
    error? sys/util/rescue [redbol-apply 'append/only [copy [a b] 'c]]
)]
(1 = redbol-apply :subtract [2 1])
(1 = (redbol-apply :- [2 1]))
(error? sys/util/rescue [redbol-apply lambda [a] [a] []])
(error? sys/util/rescue [redbol-apply/only lambda [a] [a] []])

; CC#2237
(error? sys/util/rescue [redbol-apply lambda [a] [a] [1 2]])
(error? sys/util/rescue [redbol-apply/only lambda [a] [a] [1 2]])

(error? redbol-apply :make [error! ""])

(/a = redbol-apply lambda [/a] [a] [okay])
(null = redbol-apply lambda [/a] [a] [null])
(null = redbol-apply lambda [/a] [a] [])
(/a = redbol-apply/only lambda [/a] [a] [okay])
; the word 'false
(/a = redbol-apply/only lambda [/a] [a] [null])
(null = redbol-apply/only lambda [/a] [a] [])
(use [a] [a: okay /a = redbol-apply lambda [/a] [a] [a]])
(use [a] [a: null null = redbol-apply lambda [/a] [a] [a]])
(use [a] [a: null /a = redbol-apply lambda [/a] [a] ['a]])
(use [a] [a: null /a = redbol-apply lambda [/a] [a] [/a]])
(use [a] [a: null /a = redbol-apply/only lambda [/a] [a] [a]])
(group! = redbol-apply/only (specialize 'of [property: 'type]) [()])
([1] = head of redbol-apply :insert [copy [] [1] null null null])
([1] = head of redbol-apply :insert [copy [] [1] null null null])
([[1]] = head of redbol-apply :insert [copy [] [1] null null okay])
(action! = redbol-apply (specialize 'of [property: 'type]) [:print])
(get-word! = redbol-apply/only (specialize 'of [property: 'type]) [:print])

;-- #1760 --

(
    1 = reeval func [] [redbol-apply does [] [return 1] return 2]
)
(
    1 = reeval func [] [redbol-apply lambda [a] [a] [return 1] return 2]
)
(
    1 = reeval func [] [redbol-apply does [] [return 1]]
)
(
    1 = reeval func [] [redbol-apply lambda [a] [a] [return 1]]
)
(
    1 = reeval func [] [redbol-apply lambda [a b] [a] [return 1 return 2]]
)
(
    1 = reeval func [] [redbol-apply lambda [a b] [a] [2 return 1]]
)

(
    trash? redbol-apply lambda [
        x [any-stable! trash!]
    ][
        get/any 'x
    ][
        ~
    ]
)
(
    trash? redbol-apply lambda [
        'x [any-stable! trash!]
    ][
        get/any 'x
    ][
        ~
    ]
)
(
    trash? redbol-apply func [
        return: [any-stable!]
        x [any-stable! trash!]
    ][
        return get/any 'x
    ][
        ~
    ]
)
(
    trash? redbol-apply func [
        return: [any-stable!]
        'x [any-stable! trash!]
    ][
        return get/any 'x
    ][
        ~
    ]
)
(
    error? redbol-apply func ['x [any-stable! trash!]] [
        return get 'x
    ][
        make error! ""
    ]
)
(use [x] [x: 1 equal? 1 redbol-apply lambda ['x] [:x] [:x]])
(use [x] [x: 1 equal? 1 redbol-apply lambda ['x] [:x] [:x]])
(
    use [x] [
        x: 1
        equal? first [:x] redbol-apply/only lambda [:x] [:x] [:x]
    ]
)
(
    use [x] [
        x: null
        equal? first [:x] redbol-apply/only func ['x [any-stable!]] [
            return get 'x
        ] [:x]
    ]
)
(use [x] [x: 1 equal? 1 redbol-apply lambda [:x] [:x] [x]])
(use [x] [x: 1 equal? 'x redbol-apply lambda [:x] [:x] ['x]])
(use [x] [x: 1 equal? 'x redbol-apply/only lambda [:x] [:x] [x]])
(use [x] [x: 1 equal? 'x redbol-apply/only func [:x] [return :x] [x]])
(
    use [x] [
        x: null
        equal? 'x redbol-apply/only func ['x [any-stable!]] [
            return get 'x
        ] [x]
    ]
)

; MAKE FRAME! :RETURN should preserve binding in the FUNCTION OF the frame
;
(1 = reeval func [] [redbol-apply :return [1] 2])

; !!! This shows a weak spot: how would REDBOL-APPLY/ONLY work on antiforms?
; It could degrade them, which would be an argument for not using quasars much.
;
('~null~ = redbol-apply/only lambda [/a] [a] [~null~])

(group! = redbol-apply/only :type-of [()])
