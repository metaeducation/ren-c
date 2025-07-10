; %validate-enclosure.test.r
;
; At one point, VALIDATE was a combinator meaning "SUBPARSE-MATCH"
;
; But for the time when VALIDATE existed as a combinator for a parse variant,
; there was also a usermode VALIDATE written as an enclosure of PARSE.  It was
; tested so we keep the test to make sure that works.
;
; Note: Users could also write `parse data [...rules... || <input>]` and get
; the same effect. It might be tempting to write this as an ADAPT which changes
; the rules to be:
;
;    rules: reduce [rules <input>]
;
; But if someone changed the meaning of <input> with different :COMBINATORS
; that would not work.  ENCLOSE will work regardless.  But really, just use
; PARSE-MATCH

[
    (validate: enclose parse*/ func [f [frame!]] [
        eval f except [return null]
        return f.input
    ], ok)

    ("aaa" = validate "aaa" [some #a])
    (null = validate "aaa" [some #b])

    (null = validate "aaa" [#a])
    ("aaa" = validate:relax "aaa" [#a])
]
