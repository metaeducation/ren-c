; functions/control/remove-each.r
[
    remove-each i s: [1 2] [true]
    empty? s
]
[
    remove-each i s: [1 2] [false]
    [1 2] = s
]
