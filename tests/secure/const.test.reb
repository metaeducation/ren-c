; Unlike PROTECT which can be used to protect a series regardless of which
; value is viewing it, CONST is a trait of a value that views a series.
; The same series can have const references and mutable references to it.

(
    data: mutable [a b c]
    data-readonly: const data
    did all [
        (e: trap [append data-readonly <readonly>] e.id = 'const-value)
        append data <readwrite>
        data = [a b c <readwrite>]
        data-readonly = [a b c <readwrite>]
    ]
)(
    sum: 0
    repeat 5 code: [
        ()
        append code/1 sum: sum + 1
    ]
    did all [
        sum = 5
        code/1 = '(1 2 3 4 5)
    ]
)(
    sum: 0
    e: trap [
        repeat 5 code: const [
            ()
            append code/1 sum: sum + 1
        ]
    ]
    e.id = 'const-value
)(
    sum: 0
    repeat 5 code: const [
        ()
        append mutable code/1 sum: sum + 1
    ]
    did all [
        sum = 5
        code/1 = '(1 2 3 4 5)
    ]
)


; DO should be neutral...if the value it gets in is const, it should run that
; as const...it shouldn't be inheriting a "wave of constness" otherwise.
(
    e: trap [repeat 2 [do [append d: [] <item>]]]
    e.id = 'const-value
)(
    block: [append d: [] <item>]
    [<item> <item>] = repeat 2 [do block]
)(
    block: [append d: [] <item>]
    e: trap [repeat 2 [do const block]]
    e.id = 'const-value
)


; While a value fetched from a WORD! during evaluation isn't subject to the
; wave of constness that a loop or function body puts on a frame, if you
; do a COMPOSE then it looks the same from the evaluator's point of view.
; Hence, if you want to modify composed-in blocks, use explicit mutability.
(
    [<legal> <legal>] = do compose [repeat 2 [append mutable [] <legal>]]
)(
    block: []
    e: trap [
        do compose/deep [repeat 2 [append (block) <fail>]]
    ]
    e.id = 'const-value
)(
    block: mutable []
    do compose/deep [repeat 2 [append (block) <legal>]]
    block = [<legal> <legal>]
)


; A shallow COPY of a literal value that the evaluator has made const will
; only make the outermost level mutable...referenced series will be const
; if they weren't copied (and weren't mutable explicitly)
(
    repeat 1 [data: copy [a [b [c]]]]
    append data <success>
    e2: trap [append data/2 <fail>]
    e22: trap [append data/2/2 <fail>]
    did all [
        data = [a [b [c]] <success>]
        e2.id = 'const-value
        e22.id = 'const-value
    ]
)(
    repeat 1 [data: copy/deep [a [b [c]]]]
    append data <success>
    append data/2 <success>
    append data/2/2 <success>
    data = [a [b [c <success>] <success>] <success>]
)(
    repeat 1 [sub: copy/deep [b [c]]]
    data: copy compose [a (sub)]
    append data <success>
    append data/2 <success>
    append data/2/2 <success>
    data = [a [b [c <success>] <success>] <success>]
)

[https://github.com/metaeducation/ren-c/issues/633 (
    e: trap [count-up x 1 [append foo: [] x]]
    e.id = 'const-value
)]


; Functions mark their body CONST by default
[
    (did symbol-to-string: function [s] [
       switch s [
           '+ ["plus"]
           '- ["minus"]
       ]
    ])

    (
        p: symbol-to-string '+
        e: trap [insert p "double-" append p "-good"]
        e.id = 'const-value
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
;    func-r2: reskinned [body [block!]] adapt :func []
;    aggregator: func-r2 [x] [data: [] append data x]
;    did all [
;        [10] = aggregator 10
;        [10 20] = aggregator 20
;    ]
;

; COMPOSE should splice with awareness of const/mutability
(
    e: trap [repeat 2 compose [append ([1 2 3]) <bad>]]
    e.id = 'const-value
)(
    block: repeat 2 compose [append (mutable [1 2 3]) <legal>]
    block = [1 2 3 <legal> <legal>]
)


; If soft-quoted branches are allowed to exist, they should not allow
; breaking of rules that would apply to values in a block-based branch.
(
    e: trap [repeat 2 [append if true '{y} {z}]]
    e.id = 'const-value
)
