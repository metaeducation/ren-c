; better-than-nothing ENCLOSE tests

(
    /e-multiply: enclose multiply/ lambda [f [frame!]] [
        let diff: abs (f.value1 - f.value2)
        diff + eval-free f
    ]

    73 = e-multiply 7 10
)
(
    /n-add: enclose add/ lambda [f [frame!]] [
        if 10 <> f.value1 [
            f.value1: 5
            eval-free f
        ]
    ]

    all [
        void? n-add 10 20
        25 = n-add 20 20
    ]
)

; Enclose should be able to be invisible
[(
    var: #before
    inner: func [] [
        return var: 1020
    ]
    outer: enclose inner/ func [f] [
        assert [1020 = eval-free f]
        return ~,~
    ]
    all [
        304 = (304 outer)
        void? outer
        var = 1020
    ]
)(
    var: #before
    inner: func [return: [ghost!]] [
        var: 1020
        return ~,~
    ]
    outer: enclose inner/ func [return: [quoted! quasiform!] f] [
        return meta (eval-free:undecayed f)  ; don't unquote it here
    ]
    all [
        '~,~ = outer
        var = 1020
    ]
)(
    var: #before
    inner: func [return: [~,~]] [
        var: 1020
        return ~,~
    ]
    /outer: enclose inner/ func [return: [ghost! any-value?] f] [
        return eval-free:undecayed f  ; now try unquoting
    ]
    all [
        '~,~ = ^(outer)
        var = 1020
    ]
)]

(
    /wrapped: enclose (
        func [in] [return pack [~, in + 1]]
    ) f -> wrap [
        x: f.in
        [# o]: eval-free f
        o * 10
    ]
    110 = wrapped 10
)

~expired-frame~ !! (
    /wrapped: enclose (
        func [in] [return pack [~, in + 1]]
    ) f -> [
        let x: f.in
        eval-free f
        f.in
    ]

    wrapped 10
)
