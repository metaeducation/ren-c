; %parse3-inside.test.reb
;
; PARSE/INSIDE is an experimental feature to demonstrate the possibility of
; using virtual binding together with PARSE.
;
; While a typical IN operation would only affect a block through its embedded
; arrays, making PARSE complicit means that PARSE can add the context when
; it fetches GROUP!s or BLOCK!s via variables.

(
    data: <unmodified>  ; we want the DATA inside OBJ to be changed

    rule: [copy data [some integer!]]

    obj: make object! [data: _]

    did all [
        did parse3/inside [1 2 3] [some rule] obj
        obj.data = [1 2 3]
        data = <unmodified>
    ]
)

(
    rule: [(foo "hi")]

    obj: make object! [
        stuff: copy []
        foo: func [x] [append stuff x]
    ]

    did all [
        did parse3/inside "aaa" [some ["a" rule]] obj
        obj.stuff = ["hi" "hi" "hi"]
    ]
)
