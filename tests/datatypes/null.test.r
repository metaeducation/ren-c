; null.test.r
;
; Note: "null" is the antiform state of the word "null", which has special
; consideration in the system, and is exposed in the API not via a typical
; value handle but the bound-to language's NULL abstraction.
;
; WORD! antiforms like ~null~ can be held by variables, but they cannot
; appear in BLOCK!s etc.  It's use is as a kind of "soft failure", and can
; be tested for and reacted to easily with things like DID, DIDN'T, THEN, ELSE.

(null? null)
~type-of-null~ !! (keyword! = type of null)
(null = try type of null)

(not null? 1)

; Early designs for NULL did not let you get or set them from plain WORD!
; Responsibility for kind of "ornery-ness" shifted to the antiform of void.
(
    a: ~
    all [
        null? a: null
        null? a
        null = a
    ]
)
(
    a: ~
    all [
        null = set $a null
        null? a
        null = a
    ]
)

; The specific role of heavy nulls is to be reactive with THEN and not
; ELSE, so that a taken branch may be purposefully NULL.
;
; HEAVY is probably not the best name for an operator that creates packs
; out of null and void and passes everything else through.  But it's what it
; was called, in line with the idea of "heavy isotopes".
[
    ((lift null) = lift null)
    ('~[~null~]~ = lift heavy null)

    (x: heavy 10, 10 = x)
    (x: heavy null, (lift null) = ^ x)
    (x: heavy null, (lift null) = ^ :x)

    (304 = (null then [1020] else [304]))
    (1020 = (heavy null then [1020] else [304]))

    ((lift null) = lift light heavy null)
    ((lift void) = lift light heavy void)
]

; Conditionals return NULL on failure, and ~[~null~]~ antiform on a branch that
; executes and evaluates to either NULL or ~[~null~]~ antiform.
[
    ('~[~null~]~ = ^ if ok [null])
    ('~[~null~]~ = ^ if ok [heavy null])
    ('~[]~ = ^ if ok [])

    ~illegal-keyword~ !! (if ok [~asdf~])  ; not all keywords legal
    (''~asdf~ = ^ if ok ['~asdf~])  ; but okay as quasiforms

    ((lift void) <> ^ ~()~)
    (not (lift void) = first [~()~])
    (not (lift void) = void)

    ('~null~ = if ok ^[null])
    ('~[~null~]~ = if ok ^[heavy null])
    ((lift void) = if ok ^[])

    ~illegal-keyword~ !! (if ok ^[~asdf~])
    (''~asdf~ = if ok ^['~asdf~])
]

; If your type constraint on return is NULL?, that doesn't accept heavy
; nulls (the NULL? test actually gives an ERROR! value back).  Typechecking
; interprets definitional errors as a failure to check, so this means the
; heavy null will decay in that case.
[
    (error? null? heavy null)
    (
        foo: func [return: [null?]] [
            return heavy null
        ]
        null? foo
    )
    (
        foo: func [return: [any-value?]] [
            return heavy null
        ]
        heavy-null? foo
    )
]
