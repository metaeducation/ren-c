; %bar-bar.test.r
;
; At one time, `|` was an "expression barrier" as an early version of
; what today is done with COMMA!.  However, commas won out as something
; that could be used in PARSE as well, allowing | to be alternates.
;
; `||` was conceived as something that could act as an expression
; barrier in usermode.
;

(
    3 = (1 + 2,)  ; COMMA! barrier
)(
    3 = (1 + 2 ||)  ; usermode expression barrier
)

; Non-variadic
[
    (
        left-normal: infix /right-normal: (
            func [return: [null? word!] x [word!]] [return x]
        )
        left-normal*: infix /right-normal*: (
            func [return: [null? word!] x [word! <end>]] [return x]
        )

        left-defer: infix:defer left-normal/
        left-defer*: infix:defer left-normal/

        left-soft: infix /right-soft: (
            func [return: [null? word!] @(x) [word!]] [return x]
        )
        left-soft*: infix /right-soft*: (
            func [return: [null? word!] @(x) [word! <end>]] [return x]
        )

        left-hard: infix /right-hard: (
            func [return: [null? word!] 'x [word!]] [return x]
        )
        left-hard*: infix /right-hard*: (
            func [return: [null? word!] 'x [word! <end>]] [return x]
        )

        ok
    )

    ~no-arg~ !! (right-normal ||)
    (null? eval [right-normal* ||])
    (null? eval [right-normal*])

    ~no-arg~ !! (|| left-normal)
    (null? eval [|| left-normal*])
    (null? eval [left-normal*])

    ~no-arg~ !! (|| left-defer)
    (null? eval [|| left-defer*])
    (null? eval [left-defer*])

    ('|| = eval [right-soft ||])
    ('|| = eval [right-soft* ||])
    (null? eval [right-soft*])

    ~no-arg~ !! (eval [|| left-soft])
    (null? eval [|| left-soft*])
    (null? eval [left-soft*])

    ('|| = eval [right-hard ||])
    ('|| = eval [right-hard* ||])
    (null? eval [right-hard*])

    ~no-arg~ !! (eval [|| left-hard])
    (null? eval [|| left-hard*])
    (null? eval [left-hard*])
]


; Variadic
[
    (
        left-normal: infix /right-normal:
            func [return: [null? word!] x [word! <variadic>]] [
                return take x
            ]
        left-normal*: infix /right-normal*:
            func [return: [null? word!] x [word! <variadic> <end>]] [
                return try take x
            ]

        left-defer: infix:defer left-normal/
        left-defer*: infix:defer left-normal/

        left-soft: infix /right-soft:
            func [return: [null? word!] @(x) [word! <variadic>]] [
                return take x
            ]
        left-soft*: infix /right-soft*:
            func [return: [null? word!] @(x) [word! <variadic> <end>]] [
                return try take x
            ]

        left-hard: infix /right-hard:
            func [return: [null? word!] 'x [word! <variadic>]] [
                return take x
            ]
        left-hard*: infix /right-hard*:
            func [return: [null? word!] 'x [word! <variadic> <end>]] [
                return try take x
            ]

        ok
    )

    ; !!! A previous distinction between TAKE and TAKE* made errors on cases of
    ; trying to TAKE from a non-endable parameter.  The definition has gotten
    ; fuzzy:
    ; https://github.com/metaeducation/ren-c/issues/1057
    ;
    ~nothing-to-take~ !! (eval [right-normal ||])
    ~nothing-to-take~ !! (eval [|| left-normal])

    (null? eval [right-normal* ||])
    (null? eval [right-normal*])

    (null? eval [|| left-normal*])
    (null? eval [left-normal*])

    ~nothing-to-take~ !! (eval [|| left-defer])
    (null? eval [|| left-defer*])
    (null? eval [left-defer*])

    ('|| = eval [right-soft ||])
    ('|| = eval [right-soft* ||])
    (null? eval [right-soft*])

    ~nothing-to-take~ !! (eval [|| left-soft])
    (null? eval [|| left-soft*])
    (null? eval [left-soft*])

    ~nothing-to-take~ !! (eval [right-hard])
    ('|| = eval [right-hard ||])
    ('|| = eval [right-hard* ||])
    (null? eval [right-hard*])

    ~nothing-to-take~ !! (eval [|| left-hard])
    (null? eval [|| left-hard*])
    (null? eval [left-hard*])
]
