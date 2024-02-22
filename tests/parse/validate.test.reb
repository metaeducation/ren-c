; %validate.test.reb
;
; VALIDATE is a variant of PARSE that returns its input on match, else null.

[
    ("aaa" = validate "aaa" [some #a])
    (null = validate "aaa" [some #b])
]
