; functions/context/bindq.r
(
    o: make object! [a: null]
    same? o binding of has o 'a
)

(
    obj: make object! [x: 1020]
    for-each 'item bind obj [
        x 'x ''x '''x ''''x
        @x '@x ''@x '''@x ''''@x
        :x ':x '':x ''':x '''':x
        x: 'x: ''x: '''x: ''''x:
        ^x '^x ''^x '''^x ''''^x

        ; !!! Should BINDING OF work on PATH! and TUPLE!?
        ;
        ; /x '/x ''/x '''/x ''''/x
        ; .x '.x ''.x '''.x ''''.x
        ; x/ 'x/ ''x/ '''x/ ''''x/
        ; x. 'x. ''x. '''x. ''''x.
    ][
        if obj <> binding of item [
            panic ["Binding of" @item "is not to expected object"]
        ]
    ]
    ok
)
