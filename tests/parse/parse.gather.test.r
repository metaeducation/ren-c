; %parse-gather.test.r
;
; EMIT is a new idea to try and make it easier to use PARSE rules to bubble
; up objects.  It works with a GATHER and SET-WORD!

(
    all [
        let g
        '* = parse [* * * 1 <foo> * * *] [
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
    parse "aaabbb" [
        result: gather [
            emit x: collect some ["a", keep (<a>)]
            emit y: collect some ["b", keep (<b>)]
        ]
    ] else [
       panic "Parse failure"
    ]
    all [
        result.x = [<a> <a> <a>]
        result.y = [<b> <b> <b>]
    ]
)


; One idea for using GATHER is to mix it with a construct that would import
; the resulting object variables into scope.  This is done with USING at the
; moment, but it is theorized this will take over the term USE at some point.
[(
    i: #i
    t: #t
    if ok [
        using parse [1 <foo>] [gather [emit i: integer!, emit t: tag!]]
        assert [i = 1, t = <foo>]
    ]
    all [
        i = #i
        t = #t
    ]
)(
    base: #base
    extension: #extension
    if ok [
        let filename: "demo.txt"
        using parse filename [
            gather [
                emit base: between <here> "."
                emit extension: across [thru <end>]
            ]
        ] else [
            panic "Not a file with an extension"
        ]
        assert [base = "demo"]
        assert [extension = "txt"]
    ]
    all [
        base = #base
        extension = #extension
    ]
)]

; SET-GROUP! can be used to synthesize variable names to set, ANY-WORD? legal
[
    (
        word: 'xxx
        obj: parse "a" [gather [emit (word): one]]
        obj.xxx = #a
    )(
        word: @xxx
        obj: parse "a" [gather [emit (word): one]]
        obj.xxx = #a
    )
]
