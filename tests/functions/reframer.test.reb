; reframer.test.reb
;
; REQUOTE is implemented as a REFRAMER, and tested with QUOTED!
; This is where to put additional tests.


; Simple test: make sure a reframer which does nothing but echo
; the built frame matches what we'd expect by building manually.
(
    f1: make frame! :append
    f1.return: null
    f1.series: [a b c]
    f1.value: <d>
    f1.part: null
    f1.dup: null
    f1.only: null
    f1.line: null

    mirror: reframer func [f [frame!]] [f]
    f2: mirror append [a b c] <d>
    f2.return: null
    f1 = f2
)


; Executing frames is the typical mode of a reframer.
; It may also execute frames more than once.
(
    two-times: reframer func [f [frame!]] [do copy f, do f]

    [a b c <d> <d>] = two-times append [a b c] <d>
)


; Reframers with their own arguments are possible
(
    data: []

    bracketer: reframer func [msg f] [
        append data msg
        do f
        append data msg
    ]

    bracketer "Aloha!" append data <middle>

    data = ["Aloha!" <middle> "Aloha!"]
)
