; reframer.test.reb
;
; REQUOTE is implemented as a REFRAMER, and tested with QUOTED!
; This is where to put additional tests.


; Simple test: make sure a reframer which does nothing but echo
; the built frame matches what we'd expect by building manually.
(
    f1: make frame! append/
    assert [not try get $f1.return]
    assert [unset? $f1.series]
    assert [unset? $f1.value]
    assert [unset? $f1.part]
    assert [unset? $f1.dup]
    assert [unset? $f1.line]
    f1.series: [a b c]
    f1.value: <d>
    f1.part: f1.dup: f1.line: null

    /mirror: reframer lambda [f [frame!]] [f]
    f2: mirror append [a b c] <d>
    f1 = f2
)


; Executing frames is the typical mode of a reframer.
; It may also execute frames more than once.
(
    two-times: reframer func [f [frame!]] [eval copy f, return eval f]

    [a b c <d> <d>] = two-times append [a b c] <d>
)


; Reframers with their own arguments are possible
(
    data: []

    bracketer: reframer lambda [msg f] [
        append data msg
        eval f
        append data msg
    ]

    bracketer "Aloha!" append data <middle>

    data = ["Aloha!" <middle> "Aloha!"]
)

; Test of using REFRAMER to make it possible to omit clauses when they have
; NULL in them causing an error in REDUCE/COMPOSE type situations
[
    (
        ver: 1.2.3
        date: null

        x: spaced [
            curtail spaced ["Version:" @ver] curtail spaced ["Date:" date]
        ]
        x = "Version: 1.2.3"
    )

    ~need-non-null~ !! (
        a: 1, b: null, c: 3
        date: null
        get-ver: func [] [join tuple! [a b c]]

        spaced [
            curtail spaced ["Version:" get-ver]
            curtail spaced ["Date:" date]
        ]
    )
    (void? curtail compose [benefit of no nulls! (find [a b c] 'd)])
]
