; %parse-set-word.test.reb
;
; Mixing SET-WORD! with block returns the last value-bearing rule in the PARSE
; (Note: more on this later in this file; review when breaking out separate
; tests for UPARSE into separate files.)

[(
    x: ~
    did all [
        "hello" == parse [1 "hello"] [x: [tag! | integer!] text!]
        x = 1  ; not [1]
    ]
)(
    x: ~
    did all [
        "hello" == parse [1 "hello"] [x: [tag! integer! | integer! text!]]
        x = "hello"
    ]
)(
    x: ~
    did all [
        '~[~null~]~ == meta parse [] [x: [try integer!]]
        x = null
    ]
)(
    x: <before>
    did all [
        didn't parse [] [x: [integer!]]
        x = <before>
    ]
)(
    x: ~
    did all [
        '~[~null~]~ == meta parse [] [x: try [integer!]]
        x = null
    ]
)

(
    did all [
        [1 2 3] == parse [1 2 3] [x: collect [some keep integer!]]
        x = [1 2 3]
    ]
)]


; SET-WORD! rules that do not match should not disrupt the variable, but if
; TRY is used with it then that indicates it should be set to NULL.
[(
    t: "t"
    i: "i"
    did all [
        <foo> == parse [<foo>] [i: integer! | t: tag!]
        i = "i"  ; undisturbed
        t = <foo>
    ]
)(
    t: "t"
    i: "i"
    did all [
        <foo> == parse [<foo>] [i: try integer!, t: tag!]
        i = null
        t = <foo>
    ]
)]

[https://github.com/red/red/issues/4318
    (
        x4318: 0
        error? sys.util.rescue [
            parse [] [x4318: across]
        ]
        zero? x4318
    )
    (
        x4318: 0
        error? sys.util.rescue [
            parse [] [x4318:]
        ]
        zero? x4318
    )
]

; Void assignments are legal
(
    x: ~, y: 10
    did all [
        <result> = parse "a" [x: y: (void) "a" (<result>)]
        voided? 'x
        void? y
    ]
)(
    obj: make object! [x: ~, y: 10]
    did all [
        <result> = parse "a" [obj.x: obj.y: (void) "a" (<result>)]
        voided? 'obj.x
        void? obj.y
    ]
)
