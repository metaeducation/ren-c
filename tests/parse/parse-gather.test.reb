; %parse-gather.test.reb
;
; EMIT is a new idea to try and make it easier to use PARSE rules to bubble
; up objects.  It works with a GATHER and SET-WORD!

(
    did all [
        uparse? [* * * 1 <foo> * * *] [
            some '*
            g: gather [
                emit i: integer! emit t: text! | emit i: integer! emit t: tag!
            ]
            some '*
        ]
        g.i = 1
        g.t = <foo>
    ]
)

(
    let result
    uparse "aaabbb" [
        result: gather [
            emit x: collect some ["a", keep (<a>)]
            emit y: collect some ["b", keep (<b>)]
        ]
    ] else [
       fail "Parse failure"
    ]
    did all [
        result.x = [<a> <a> <a>]
        result.y = [<b> <b> <b>]
    ]
)


; If you EMIT with no GATHER, it's theorized we'd want to make the UPARSE
; itself emit variable definitions, much like LET.  It's a somewhat sketchy
; idea since it doesn't abstract well.  It worked until UPARSE was refactored
; to be built on top of UPARSE* via ENCLOSE, at whic point the lack of
; abstractability of ADD-LET-BINDING was noticed...so deeper macro magic
; would be involved.  For now a leser version is accomplished by just emitting
; an object that you can USE ("using" for now).  Review.
[(
    i: #i
    t: #t
    if true [
        using uparse [1 <foo>] [emit i: integer!, emit t: tag!]
        assert [i = 1, t = <foo>]
    ]
    did all [
        i = #i
        t = #t
    ]
)(
    base: #base
    extension: #extension
    if true [
       let filename: "demo.txt"
       using uparse filename [
            emit base: between <here> "."
            emit extension: across [thru <end>]
        ] else [
            fail "Not a file with an extension"
        ]
        assert [base = "demo"]
        assert [extension = "txt"]
    ]
    did all [
        base = #base
        extension = #extension
    ]
)]

