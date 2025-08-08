; %parse-set-word.test.r
;
; Mixing SET-WORD! with block returns the last value-bearing rule in the PARSE
; (Note: more on this later in this file; review when breaking out separate
; tests for UPARSE into separate files.)

[(
    x: ~
    all [
        "hello" = parse [1 "hello"] [x: [tag! | integer!] text!]
        x = 1  ; not [1]
    ]
)(
    x: ~
    all [
        "hello" = parse [1 "hello"] [x: [tag! integer! | integer! text!]]
        x = "hello"
    ]
)(
    x: ~
    all [
        null = parse [] [x: [try integer!]]
        x = null
    ]
)(
    x: <before>
    all [
        error? parse [] [x: [integer!]]
        x = <before>
    ]
)(
    x: ~
    all [
        null = parse [] [x: try [integer!]]
        x = null
    ]
)

(
    all [
        [1 2 3] = parse [1 2 3] [x: collect [some keep integer!]]
        x = [1 2 3]
    ]
)]


; SET-WORD! rules that do not match should not disrupt the variable, but if
; OPT is used with it then that indicates it should be set to NULL.
[(
    t: "t"
    i: "i"
    all [
        <foo> = parse [<foo>] [i: integer! | t: tag!]
        i = "i"  ; undisturbed
        t = <foo>
    ]
)(
    t: "t"
    i: "i"
    all [
        <foo> = parse [<foo>] [i: try integer!, t: tag!]
        i = null
        t = <foo>
    ]
)]

[https://github.com/red/red/issues/4318
    (
        x4318: 0
        warning? sys.util/recover [
            parse [] [x4318: across]
        ]
        zero? x4318
    )
    (
        x4318: 0
        warning? sys.util/recover [
            parse [] [x4318:]
        ]
        zero? x4318
    )
]

; Void assignments are legal
(
    x: ~, y: 10
    all [
        <result> = parse "a" [x: y: (^void) "a" (<result>)]
        void? x
        void? y
    ]
)(
    obj: make object! [x: ~, y: 10]
    all [
        <result> = parse "a" [obj.x: obj.y: (^void) "a" (<result>)]
        void? obj.x
        void? obj.y
    ]
)
