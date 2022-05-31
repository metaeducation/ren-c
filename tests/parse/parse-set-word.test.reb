; %parse-set-word.test.reb
;
; Mixing SET-WORD! with block returns the last value-bearing rule in the PARSE
; (Note: more on this later in this file; review when breaking out separate
; tests for UPARSE into separate files.)

[(
    x: ~
    did all [
        "hello" == uparse [1 "hello"] [x: [tag! | integer!] text!]
        x = 1  ; not [1]
    ]
)(
    x: ~
    did all [
        "hello" == uparse [1 "hello"] [x: [tag! integer! | integer! text!]]
        x = "hello"
    ]
)(
    x: ~
    did all [
        '~null~ == meta uparse [] [x: [opt integer!]]
        x = null
    ]
)(
    x: <before>
    did all [
        didn't uparse [] [x: [integer!]]
        x = <before>
    ]
)(
    x: ~
    did all [
        '~null~ == meta uparse [] [x: opt [integer!]]
        x = null
    ]
)

(
    did all [
        [1 2 3] == uparse [1 2 3] [x: collect [some keep integer!]]
        x = [1 2 3]
    ]
)]


; SET-WORD! rules that do not match should not disrupt the variable, but if
; OPT is used with it then that indicates it should be set to NULL.
[(
    t: "t"
    i: "i"
    did all [
        <foo> == uparse [<foo>] [i: integer! | t: tag!]
        i = "i"  ; undisturbed
        t = <foo>
    ]
)(
    t: "t"
    i: "i"
    did all [
        <foo> == uparse [<foo>] [i: opt integer!, t: tag!]
        i = null
        t = <foo>
    ]
)]

[https://github.com/red/red/issues/4318
    (
        x4318: 0
        did all [
            error? trap [uparse [] [x4318: across]]
            error? trap [uparse [] [x4318:]]
            zero? x4318
        ]
    )
]

; Invisible assignments will leave the variable's existing content alone, which
; is a new rule being pushed through systemically.
;
; https://forum.rebol.info/t/1582/5
;
; !!! Concept being reviewed, may be a function purely of enfix MAYBE
(
    x: ~, y: 10
    did all [
        <result> = uparse "a" [x: y: elide "a" (<result>)]
        unset? 'x
        unset? 'y
    ]
)(
    obj: make object! [x: ~, y: 10]
    did all [
        <result> = uparse "a" [obj.x: obj.y: elide "a" (<result>)]
        unset? 'obj.x
        unset? 'obj.y
    ]
)
