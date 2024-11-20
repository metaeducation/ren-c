; Unlike PROTECT which can be used to protect a series regardless of which
; value is viewing it, CONST is a trait of a value that views a series.
; The same series can have const references and mutable references to it.

(
    data: mutable [a b c]
    data-readonly: const data
    all [
        let e: sys.util/rescue [append data-readonly <readonly>]
        e.id = 'const-value
        append data <readwrite>
        data = [a b c <readwrite>]
        data-readonly = [a b c <readwrite>]
    ]
)(
    sum: 0
    repeat 5 code: [
        ()
        append code.1 sum: sum + 1
    ]
    all [
        sum = 5
        code.1 = '(1 2 3 4 5)
    ]
)
~const-value~ !! (
    sum: 0
    repeat 5 code: const [
        ()
        append code.1 sum: sum + 1
    ]
)
(
    sum: 0
    repeat 5 code: const [
        ()
        append mutable code.1 sum: sum + 1
    ]
    all [
        sum = 5
        code.1 = '(1 2 3 4 5)
    ]
)


; DO should be neutral...if the value it gets in is const, it should run that
; as const...it shouldn't be inheriting a "wave of constness" otherwise.
[
    ~const-value~ !! (
        d: ~
        repeat 2 [eval [append d: [] <item>]]
    )

    (
        d: ~
        block: [append d: [] <item>]
        [<item> <item>] = repeat 2 [eval block]
    )

    ~const-value~ !! (
        d: ~
        block: [append d: [] <item>]
        repeat 2 [eval const block]
    )
]


; While a value fetched from a WORD! during evaluation isn't subject to the
; wave of constness that a loop or function body puts on a frame, if you
; EVAL a COMPOSE then it looks the same from the evaluator's point of view.
;
; Hence, if you want to modify composed-in blocks, use explicit mutability.
[
    ([<legal> <legal>] = eval [repeat 2 [append mutable [] <legal>]])

    ~const-value~ !! (
        block: []
        eval compose:deep $() [repeat 2 [append (block) <illegal>]]
    )

    ~const-value~ !! (  ; CELL_FLAG_EXPLICITLY_MUTABLE killed off
        block: mutable []
        eval compose:deep $() [repeat 2 [append (block) <legal>]]
        block = [<legal> <legal>]
    )

    (  ; quotes can work around it
        block: []
        eval compose:deep $() [repeat 2 [append '(block) <legal>]]
        block = [<legal> <legal>]
    )

    (  ; mutable after the compose can work around it
        block: []
        eval compose:deep $() [repeat 2 [append mutable (block) <legal>]]
        block = [<legal> <legal>]
    )
]


; A shallow COPY of a literal value that the evaluator has made const will
; only make the outermost level mutable...referenced series will be const
; if they weren't copied (and weren't mutable explicitly)
(
    data: ~
    repeat 1 [data: copy [a [b [c]]]]
    append data <success>
    e2: sys.util/rescue [append data.2 <failure>]
    e22: sys.util/rescue [append data.2.2 <failure>]
    all [
        data = [a [b [c]] <success>]
        e2.id = 'const-value
        e22.id = 'const-value
    ]
)(
    data: ~
    repeat 1 [data: copy:deep [a [b [c]]]]
    append data <success>
    append data.2 <success>
    append data.2.2 <success>
    data = [a [b [c <success>] <success>] <success>]
)(
    sub: ~
    repeat 1 [sub: copy:deep [b [c]]]
    data: copy compose $() [a (sub)]
    append data <success>
    append data.2 <success>
    append data.2.2 <success>
    data = [a [b [c <success>] <success>] <success>]
)

[
    https://github.com/metaeducation/ren-c/issues/633

    ~const-value~ !! (
        foo: ~
        count-up 'x 1 [append foo: [] x]
    )
]


; Functions mark their body CONST by default
[
    (did /symbol-to-string: func [s] [
       return switch s [
           '+ ["plus"]
           '- ["minus"]
       ]
    ])

    ~const-value~ !! (
        p: symbol-to-string '+
        insert p "double-" append p "-good"
    )

    (
        p: symbol-to-string '+
        p: mutable p
        insert p "you-" append p "-asked-for-it"
        "you-plus-asked-for-it" = symbol-to-string '+
    )
]

[
    (did /symbol-to-string: func [s] [
       return case [
           s = '+ ["plus"]
           s = '- ["minus"]
       ]
    ])

    ~const-value~ !! (
        p: symbol-to-string '+
        insert p "double-" append p "-good"
    )

    (
        p: symbol-to-string '+
        p: mutable p
        insert p "you-" append p "-asked-for-it"
        "you-plus-asked-for-it" = symbol-to-string '+
    )
]



; Reskinning capabilities can remove the <const> default
; !!! RESKINNED is temporarily out of service, pending reworking of the way
; functions are built from frames.
;
;    /func-r2: reskinned [body [block!]] adapt func/ []
;    /aggregator: func-r2 [x] [data: [] append data x]
;    all [
;        [10] = aggregator 10
;        [10 20] = aggregator 20
;    ]
;

; If soft-quoted branches are allowed to exist, they should not allow
; breaking of rules that would apply to values in a block-based branch.
;
~const-value~ !! (
    repeat 2 [append if ok '-{y}- -{z}-]
)
