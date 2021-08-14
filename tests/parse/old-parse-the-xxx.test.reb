; %parse-the-xxx.test.reb
;
; THE-XXX! types are based around matching things literally.  GROUP!s will
; evaluate, and BLOCK!s will use th synthesized value of the rule to be the
; thing that gets matched.

(
    x: ~
    tag: <hello>
    did all [
        <world> = uparse [<hello> <world>] [x: @tag, '<world>]
        x = <hello>
    ]
)
(
    x: ~
    obj: make object! [field: <hello>]
    did all [
        <world> = uparse [<hello> <world>] [x: @obj.field, '<world>]
        x = <hello>
    ]
)
(1 = uparse [1 1 1] [some @(3 - 2)])
(2 = uparse [1 1 1 2] [@[some @(3 - 2), (1 + 1)]])
("b" = uparse "aaab" [@[some "x" ("y") | some "a" ("b")]])

[
    (data: [[some rule] [some rule]], true)

    (
        x: [some rule]
        uparse? data [some @x]
    )(
        uparse? data [some @([some rule])]
    )(
        obj: make object! [x: [some rule]]
        uparse? data [some @obj.x]
    )
]
