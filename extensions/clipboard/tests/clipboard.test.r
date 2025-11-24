; !!! The R3-Alpha clipboard returned bytes, because that's what READ
; returns.  Hence TO-STRING is needed in these cases.  It may be that LOAD
; could be more abstract, and get some cue from the metadata of what type
; to return.

; empty clipboard
(
    write clipboard:// ""
    c: read:string clipboard://
    (text? c) and (empty? c)
)

; ASCII string
(
    write clipboard:// c: "This is a test."
    d: read:string clipboard://
    equal? c d
)

; Separate open step
(
    p: open clipboard://
    write p c: "Clipboard port test"
    equal? c read:string p
)

; Unicode string
(
    write clipboard:// c: "Příliš žluťoučký kůň úpěl ďábelské ódy."
    equal? read:string clipboard:// c
)

; WRITE returns a PORT! in R3
;
(equal? read:string write clipboard:// c: "test" c)
