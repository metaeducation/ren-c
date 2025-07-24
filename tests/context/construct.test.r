; %construct.test.r
;
; CONSTRUCT is the default of what happens when you run FENCE! {...}
;
; It differs from traditional Redbol MAKE OBJECT! because the fields of the
; object being constructed are not visible in the code block.  So you can
; refer to variables with the same name as fields:
;
;     x: 10
;     construct [x: x]  ; legal
;

(
    parse [
        {a: 1}
        a = 1

        {, a: 1}
        a = 1

        {, a: 1, ,}
        a = 1 

        {a: 1, b: 2}
        a = 1
        b = 2

        {a: 1, b: c: 2}
        a = 1
        b = 2
        c = 2
    ][
      some [
        let spec: fence!
        let code: across [to [fence! | <end>]] ( 
            let obj: construct spec
            for-each [field eq value] code [
                assert [equal? eq '=]
                if value <> get (bind obj field) [
                    panic ["Bad CONSTRUCT:" mold spec]
                ]
            ]
        )
      ]
    ]
)
