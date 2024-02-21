; functions/define/func.r
; recursive safety
(
    f: func [return: [action?]] [
        return lambda [x] [
            either x = 1 [
                reeval f 2
                x = 1
            ][
                false
            ]
        ]
    ]
    reeval f 1
)

; Check parameter processing produces correct labeling.  These were written in
; response to a specific bug, but should be expanded:
;
; https://github.com/metaeducation/ren-c/issues/1113
;
; (Though there are several issues in flux at time of writing regarding how
; DATATYPE!s and type checking work...)
[(
    all [  ; try with no RETURN:
        foo: func ["description" a "a" b "b"] []
        m: adjunct-of :foo
        m.description = "description"
        (select :foo 'return).spec = null
        (select :foo 'return).text = null
        (select :foo 'a).spec = null
        (select :foo 'a).text = "a"
        (select :foo 'b).spec = null
        (select :foo 'b).text = "b"
    ]
)(
    all [  ; try RETURN: with no type
        foo: func ["description" return: "returns" a "a" b "b"] []
        m: adjunct-of :foo
        m.description = "description"
        (select :foo 'return).spec = null
        (select :foo 'return).text = "returns"
        (select :foo 'a).spec = null
        (select :foo 'a).text = "a"
        (select :foo 'b).spec = null
        (select :foo 'b).text = "b"
    ]
)(
    all [  ; try RETURN: with type
        foo: func ["description" return: [integer!] "returns" a "a" b "b"] []
        m: adjunct-of :foo
        m.description = "description"
        (select :foo 'return).spec = [integer!]
        (select :foo 'return).text = "returns"
        (select :foo 'a).spec = null
        (select :foo 'a).text = "a"
        (select :foo 'b).spec = null
        (select :foo 'b).text = "b"
    ]
)(
    all [  ; try without description
        foo: func [return: [integer!] "returns" a "a" /b "b"] []
        if m: adjunct-of :foo [
            m.description = null
        ]
        (select :foo 'return).spec = [integer!]
        (select :foo 'return).text = "returns"
        (select :foo 'a).spec = null
        (select :foo 'a).text = "a"
        (select :foo 'b).spec = null
        (select :foo 'b).text = "b"
    ]
)]
