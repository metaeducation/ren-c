; %parse-issue.test.reb
;
; The migration of ISSUE! to be a unified type with CHAR! as TOKEN! is
; something that is moving along slowly, as the impacts are absorbed.
;
; They are case-sensitive matched in PARSE, unlike text strings by default.

; Don't leak internal detail that BINARY! or ANY-STRING! are 0-terminated
[
    (NUL = as issue! 0)

    (null = uparse "" [to NUL])
    (null = uparse "" [thru NUL])
    (null = uparse "" [to [NUL]])
    (null = uparse "" [thru [NUL]])

    (null = uparse #{} [to NUL])
    (null = uparse #{} [thru NUL])
    (null = uparse #{} [to [NUL]])
    (null = uparse #{} [thru [NUL]])
]
