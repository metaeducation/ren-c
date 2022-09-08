; null.test.reb
;
; Note: NULL is not technically a datatype.
;
; It is a transitional state that can be held by variables, but it cannot
; appear in BLOCK!s etc.  It's use is as a kind of "soft failure", and can
; be tested for and reacted to easily with things like DID, DIDN'T, THEN, ELSE.

(null? null)
(null? type of null)
(not null? 1)

; Early designs for NULL did not let you get or set them from plain WORD!
; Responsibility for kind of "ornery-ness" this shifted to isotopes, as NULL
; took on increasing roles as the "true NONE!" and became the value for
; unused refinements.
(
    a: ~
    did all [
        null? a: _
        null? a
        null = a
    ]
)
(
    a: ~
    did all [
        null = set 'a null
        null? a
        null = a
    ]
)

; The specific role of ~_~ isotopes is to be reactive with THEN and not
; ELSE, so that a taken branch may be purposefully NULL.
;
; HEAVY is probably not the best name for an operator that creates blank
; isotopes out of NULL and passes everything else through.  But it's what it
; was called, in line with the idea of "heavy isotopes".
[
    (null' = ^ null)
    ('~[_]~ = ^ heavy null)

    (x: heavy 10, 10 = x)
    (x: heavy null, null' = ^ x)
    (x: heavy null, null' = ^ :x)

    (304 = (null then [1020] else [304]))
    (1020 = (heavy null then [1020] else [304]))
]

; Conditionals return VOID on failure, and ~_~ isotope on a branch that
; executes and evaluates to either NULL or ~_~ isotope.
[
    ('~[_]~ = ^ if true [null])
    ('~[_]~ = ^ if true [heavy null])
    ('~[~]~ = ^ if true [])
    ('~custom~ = ^ if true [~custom~])
    (''~custom~ = ^ if true ['~custom~])

    (void' <> ^ ~()~)  ; tests for isotopes
    (not void' = first [~()~])  ; plain QUASI!s do not count
    (not void' = ^ 'void)  ; ...nor do words, strings, etc

    ('_ = if true ^[null])
    ('~[_]~ = if true ^[heavy null])
    ('~ = if true ^[])
    ('~custom~ = if true ^[~custom~])
    (''~custom~ = if true ^['~custom~])
]
