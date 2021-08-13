; functions/define/func.r
; recursive safety
(
    f: func [return: [action!]] [
        return func [x] [
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
    did all [  ; try with no RETURN:
        foo: func ["description" a "a" b "b"] []
        m: meta-of :foo
        m.description = "description"
        m.parameter-types = null
        m.parameter-notes.return = null
        m.parameter-notes.a = "a"
        m.parameter-notes.b = "b"
    ]
)(
    did all [  ; try RETURN: with no type
        foo: func ["description" return: "returns" a "a" b "b"] []
        m: meta-of :foo
        m.description = "description"
        m.parameter-types = null
        m.parameter-notes.return = "returns"
        m.parameter-notes.a = "a"
        m.parameter-notes.b = "b"
    ]
)(
    did all [  ; try RETURN: with type
        foo: func ["description" return: [integer!] "returns" a "a" b "b"] []
        m: meta-of :foo
        m.description = "description"
        m.parameter-types.return = [integer!]
        m.parameter-types.a = null
        m.parameter-types.b = null
        m.parameter-notes.return = "returns"
        m.parameter-notes.a = "a"
        m.parameter-notes.b = "b"
    ]
)(
    did all [  ; try without description
        foo: func [return: [integer!] "returns" a "a" /b "b"] []
        m: meta-of :foo
        m.description = null
        m.parameter-types.return = [integer!]
        m.parameter-types.a = null
        m.parameter-types.b = null
        m.parameter-notes.return = "returns"
        m.parameter-notes.a = "a"
        m.parameter-notes.b = "b"
    ]
)]
