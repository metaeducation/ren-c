; %topaz-expression.parse.reb
;
; Adapted from:
;
; https://github.com/giesse/red-topaz-parse/blob/master/examples/expression-parser.red
;
;    This is mostly just an example of TOPAZ-PARSE
;    Compare to http://www.rebol.org/ml-display-thread.r?m=rmlXJHS
;
; Changes:
;
; * Topaz's version of GATHER is called OBJECT, and it assumes any SET-WORD!
;   are intended to be emitted as fields of the gathered object.  UPARSE needs
;   an EMIT instruction to presume a field emission intended.
;
; * Topaz's OBJECT returned a Red MAP! and not an OBJECT!, for some reason
;   (appears that map/key for missing key was #[none] vs. error for object)
;
; * Ren-C uses TUPLE! for field selection of objects, not PATH!
;
; * Auto-gathering FUNCTION removed from Ren-C as a concept (spurious locals
;   created too easily when SET-WORD! can mean anything in dialects)
;
; * FUNC must use a RETURN statement to return a value, otherwise a not-set
;   state will be returned.

;
; License:
;
; Copyright 2019 Gabriele Santilli
;
; Permission is hereby granted, free of charge, to any person obtaining
; a copy of this software and associated documentation files
; (the "Software"), to deal in the Software without restriction, including
; without limitation the rights to use, copy, modify, merge, publish,
; distribute, sublicense, and/or sell copies of the Software, and to
; permit persons to whom the Software is furnished to do so, subject
; to the following conditions:
;
; The above copyright notice and this permission notice shall be included
; in all copies or substantial portions of the Software.

[(
    factorial: func [n [integer!]] [
        if n < 2 [return 1]
        let res: 1
        for i n - 1 [res: i + 1 * res]
        return res
    ]

    expression: [
        gather [emit left: term, emit op: ['+ | '-], emit right: expression]
        |
        term
    ]
    term: [
        gather [emit left: pow, emit op: ['* | '/], emit right: term]
        |
        pow
    ]
    pow: [
        gather [emit left: unary, emit op: '**, emit right: unary]
        |
        unary
    ]
    unary: [
        gather [emit op: '- , emit argument: fact]
        |
        fact
    ]
    fact: [
        gather [emit argument: primary, emit op: '!]
        |
        primary
    ]
    primary: [
        subparse group! expression | &any-number? | word!
    ]

    emit-node: func [
        return: [~]
        output [block!]
        node [object! any-number? word!]
    ][
        either object? node [
            either select node 'argument [
                append output select [
                    - negate
                    ! factorial
                ] node.op
                emit-node output node.argument
            ][
                append output select [
                    + add
                    - subtract
                    * multiply
                    / divide
                    ** power
                ] node.op
                emit-node output node.left
                emit-node output node.right
            ]
        ][
            append output node
        ]
    ]

    tree-to-code: func [
        return: [block!]
        tree [object! any-number? word!]
    ][
        let output: make block! 0
        emit-node output tree
        return output
    ]

    parse-expression: func [
        return: [block!]
        expr [block!]
    ][
        let tree: parse expr expression  ; may fail
        return tree-to-code tree
    ]

    ok
)

(
    [subtract power b 2 multiply 4 multiply a c]
    == parse-expression [b ** 2 - 4 * a * c]
)
(
    [divide 1 factorial k]
    == parse-expression [1 / k !]
)
(
    [multiply a negate b]
    == parse-expression [a * - b]
)
(
    [power e divide i multiply h subtract multiply p x multiply E t]
    == parse-expression [e ** (i / h * (p * x - E * t))]
)
]
