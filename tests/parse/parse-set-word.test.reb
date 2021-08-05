; %parse-set-word.test.reb
;
; Mixing SET-WORD! with block returns the last value-bearing rule in the PARSE
; (Note: more on this later in this file; review when breaking out separate
; tests for UPARSE into separate files.)

[(
    x: ~
    did all [
        uparse? [1 "hello"] [x: [tag! | integer!] text!]
        x = 1  ; not [1]
    ]
)(
    x: ~
    did all [
        uparse? [1 "hello"] [x: [tag! integer! | integer! text!]]
        x = "hello"
    ]
)(
    x: ~
    did all [
        uparse? [] [x: [opt integer!]]
        x = null
    ]
)(
    x: <before>
    did all [
        not uparse? [] [x: [integer!]]
        x = <before>
    ]
)(
    x: ~
    did all [
        uparse? [] [x: opt [integer!]]
        x = null
    ]
)

(
    did all [
        uparse? [1 2 3] [x: collect [some keep integer!]]
        x = [1 2 3]
    ]
)]


; SET-WORD! rules that do not match should not disrupt the variable, but if
; OPT is used with it then that indicates it should be set to NULL.
[(
    t: "t"
    i: "i"
    did all [
        uparse? [<foo>] [i: integer! | t: tag!]
        i = "i"  ; undisturbed
        t = <foo>
    ]
)(
    t: "t"
    i: "i"
    did all [
        uparse? [<foo>] [i: opt integer!, t: tag!]
        i = null
        t = <foo>
    ]
)]