; %tracked-word.parse.test.reb
;
; This is a test which was inspired by trying to establish hooks to implement
; Brett Handley's LOAD-PARSE-TREE from Rebol2:
;
; http://www.rebol.org/documentation.r?script=load-parse-tree.r
;
; The concept is that if a rule is named via a WORD! that there is a means to
; receive a notification that such a rule triggers.  The idea in UPARSE was
; to actually build a hooked version of the WORD! combinator in order to
; effectively receive that "event" without needing to actually do surgery on
; the rule blocks themselves.
;
; So what you see is an ENCLOSE to run pre-and-post code each time a WORD! rule
; is dispatched.  This wrapped TRACKED-WORD! combinator actually wraps the
; `pending` result list with a prefix line and a postfix line of annotation
; to mention which rule ran.
;
; One mechanically interesting aspect of this particular way of doing things is
; that the tracking is not synchronously added to the stack.  The code waits
; to run until the entire parse finishes.  This way a rule that succeeds but
; is part of an alternate group that ultimately fails will not add its output,
; which was a problem with an earlier implementation of this idea:
;
; https://forum.rebol.info/t/getting-hooks-into-events-during-parse/1640/5

[(
    stack: ~

    tracked-word!: enclose get/groups $default-combinators.(word!) func [
        return: [~null~ pack?]
        f [frame!]
        <static> indent (0)
        <local> result' remainder subpending
    ][
        let input: f.input  ; save to use after EVAL F
        let name: f.value

        indent: me + 1
        [^result' remainder subpending]: eval f except e -> [
            indent: me - 1
            return raise e
        ]
        indent: me - 1

        let consumed: copy/part input remainder

        if blank? subpending [subpending: copy []]

        ; GROUP!s in the pending list are deferred until the parse finishes
        ; (or reaches a PHASE boundary).  We don't know if this rule will
        ; ultimately succeed or not, but if it does succeed we want to push
        ; a line to the stack.  Queue that up as code, bracketing the code
        ; that's bubbled up.

        insert subpending as group! compose [
            append stack (spaced collect [
                repeat indent * 4 [keep space], keep as text! name, keep "["
            ])
        ]
        append subpending as group! compose [
            append stack (spaced collect [
                repeat indent * 4 [keep space], keep "] =>", keep mold consumed
            ])
        ]

        return pack [unmeta result', remainder, subpending]
    ]

    tracked-combinators: copy default-combinators
    tracked-combinators.(word!): unrun :tracked-word!

    trackparse*: specialize parse/ [combinators: tracked-combinators]

    trackparse: enclose trackparse*/ lambda [f [frame!]] [
        stack: copy []
        eval f then [
            append stack ""  ; give final newline
            (delimit newline stack, elide clear stack)
        ]
    ]

    ok
)(
    foo-rule: [some "f"]
    bar-rule: [some "b" foo-rule]
    meta-rule: [some bar-rule]
    ok
)(
    (trackparse "fff" [foo-rule]) == trim/auto {
        foo-rule [
        ] => "fff"
    }
)(
    (trackparse "bbbfff" [bar-rule]) == trim/auto {
        bar-rule [
            foo-rule [
            ] => "fff"
        ] => "bbbfff"
    }
)(
    (trackparse "bbbfffbbbfff" [repeat 2 bar-rule]) == trim/auto {
        bar-rule [
            foo-rule [
            ] => "fff"
        ] => "bbbfff"
        bar-rule [
            foo-rule [
            ] => "fff"
        ] => "bbbfff"
    }
)(
    (trackparse "bbbfffbbbfff" [meta-rule]) == trim/auto {
        meta-rule [
            bar-rule [
                foo-rule [
                ] => "fff"
            ] => "bbbfff"
            bar-rule [
                foo-rule [
                ] => "fff"
            ] => "bbbfff"
        ] => "bbbfffbbbfff"
    }
)(
    ; This one is the first "impressive" one, it knows not to consider the
    ; first foo-rule an "ultimate match" because its alternate was not
    ; fulfilled!
    ;
    (trackparse "fffyyy" [foo-rule some "x" | foo-rule some "y"]) == trim/auto {
        foo-rule [
        ] => "fff"
    }
)]
