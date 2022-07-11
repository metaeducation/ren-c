; %parse-the-xxx.test.reb
;
; THE-XXX! types are based around matching things literally.  GROUP!s will
; evaluate, and BLOCK!s will use th synthesized value of the rule to be the
; thing that gets matched.

(
    x: ~
    tag: <hello>
    did all [
        <world> = parse [<hello> <world>] [x: @tag, '<world>]
        x = <hello>
    ]
)
(
    x: ~
    obj: make object! [field: <hello>]
    did all [
        <world> = parse [<hello> <world>] [x: @obj.field, '<world>]
        x = <hello>
    ]
)
(1 = parse [1 1 1] [some @(3 - 2)])
(2 = parse [1 1 1 2] [@[some @(3 - 2), (1 + 1)]])
("b" = parse "aaab" [@[some "x" ("y") | some "a" ("b")]])

[
    (data: [[some rule] [some rule]], true)

    (
        x: [some rule]
        [some rule] = parse data [some @x]
    )(
        [some rule] = parse data [some @([some rule])]
    )(
        obj: make object! [x: [some rule]]
        [some rule] = parse data [some @obj.x]
    )

    ([some rule] = parse data [@ block!])
]

(
    block: [some stuff]
    'stuff = parse [some stuff some stuff] [repeat (2) @((block))]
)
