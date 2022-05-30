; better-than-nothing ENCLOSE tests

(
    e-multiply: enclose :multiply function [f [frame!]] [
        let diff: abs (f.value1 - f.value2)
        diff + do f
    ]

    73 = e-multiply 7 10
)
(
    n-add: enclose :add function [f [frame!]] [
        if 10 <> f.value1 [
            f.value1: 5
            do f
        ]
    ]

    did all [
        '~void~ = ^ n-add 10 20
        25 = n-add 20 20
    ]
)

; Enclose should be able to be invisible
[(
    var: #before
    inner: func [] [
        var: 1020
    ]
    outer: enclose :inner func [f] [
        assert [1020 = do f]
        return void
    ]
    did all [
        304 = (304 maybe/value outer)
        _ = ^ (outer)
        var = 1020
    ]
)(
    var: #before
    inner: func [return: [<invisible>]] [
        var: 1020
        return void
    ]
    outer: enclose :inner func [return: [<opt> any-value!] f] [
        return ^(eval f)  ; don't unquote it here
    ]
    did all [
        '~void~ = outer
        var = 1020
    ]
)(
    var: #before
    inner: func [return: [<invisible>]] [
        var: 1020
        return void
    ]
    outer: enclose :inner func [return: [<opt> <invisible> any-value!] f] [
        return do f  ; now try unquoting
    ]
    did all [
        '~void~ = ^(outer)
        var = 1020
    ]
)]
