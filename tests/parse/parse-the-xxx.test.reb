; %parse-the-xxx.test.reb
;
; THE-XXX! types are based around matching things literally.  GROUP!s will
; evaluate, and BLOCK!s will use the synthesized value of the rule to be the
; thing that gets matched.

(
    x: ~
    tag: <hello>
    all [
        <world> = parse [<hello> <world>] [x: @tag, '<world>]
        x = <hello>
    ]
)
(
    x: ~
    obj: make object! [field: <hello>]
    all [
        <world> = parse [<hello> <world>] [x: @obj.field, '<world>]
        x = <hello>
    ]
)
(1 = parse [1 1 1] [some @(3 - 2)])
(2 = parse [1 1 1 2] [@[some @(3 - 2), (1 + 1)]])
("b" = parse "aaab" [@[some "x" ("y") | some "a" ("b")]])

[
    (data: [[some rule] [some rule]], ok)

    (
        x: [some rule]
        [some rule] = parse data [some @x]
    )(
        [some rule] = parse data [some @([some rule])]
    )(
        obj: make object! [x: [some rule]]
        [some rule] = parse data [some @obj.x]
    )
]

(
    block: [a b]
    ^(spread [a b]) = ^ parse [a b a b] [repeat 2 @(spread block)]
)
(
    block: [a b]
    [a b] = parse [[a b] [a b]] [repeat 2 @(block)]
)

(
    [a b c 1 2 3]
    == append [a b c] parse [1 2 3] [block! | spread across some integer!]
)
(
    [a b c [1 2 3]]
    == append [a b c] parse [[1 2 3]] [block! | spread across some integer!]
)
