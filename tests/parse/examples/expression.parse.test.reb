; %expression.parse.test.reb
;
; Expression parser (found in %parse-test.red)

[
    (
        expr: [term ["+" | "-"] expr | term]
        term: [factor ["*" | "/"] term | factor]
        factor: [primary "**" factor | primary]
        primary: [some digit | "(" expr ")"]
        digit: charset "0123456789"
        true
    )

    (not uparse? "123a+2" expr)
    (not uparse? "a+b" expr)
    (uparse? "4/5+3**2-(5*6+1)" expr)
    (uparse? "1+2*(3-2)/4" expr)
    (not uparse? "(+)" expr)
    (uparse? "(1*9)" expr)
    (uparse? "(1)" expr)
    (uparse? "1+2*(3-2)/4" expr)
    (not uparse? "1+2*(3-2)/" expr)
    (uparse? "1+2*(3-2)" expr)
    (not uparse? "1+2*(3-2" expr)
    (not uparse? "1+2*(3-" expr)
    (not uparse? "1+2*(3" expr)
    (not uparse? "1+2*(" expr)
    (not uparse? "1+2*" expr)
    (uparse? "1+2" expr)
    (not uparse? "1+" expr)
    (uparse? "1" expr)
]
