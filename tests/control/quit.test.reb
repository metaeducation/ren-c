; functions/control/quit.r
; In R3, DO of a script provided as a text! code catches QUIT, just as it
; would do for scripts in files.

(42 = do "quit 42")

(99 = do {do {quit 42} 99})

; Returning of Rebol values from called to calling script via QUIT w/arg.
;
; Note: this R3-Alpha test was originally written for NONE, which would
; round-trip in a strange way due to the #[none] molding as the word none.
; With BLANK! now evaluating to a NULL state, this means the COMPOSE has to
; put a quote on.  But that breaks the strange incidental object round
; tripping, because (quit 'make object! [...]) just gives back the word
; "make".  For the moment, bias it to the blank case since the object
; molding is something that can't generally round trip at all.
(
    do-script-returning: lambda [value <local> script] [
        script: %tmp-inner.reb
        save/header script compose [quit '(value)] []  ; see note RE: quoting
        do script
        elide delete %tmp-inner.reb
    ]
    all map-each value reduce [
        42
        {foo}
        #{CAFE}
        blank  ; forces quoting in save, see note
        http://somewhere
        1900-01-30
        ; make object! [x: 42]  ; !!! can't round-trip quoted, see note
    ][
        value = do-script-returning value
    ]
)

[#2190
    (error? trap [catch/quit [attempt [quit]] 1 / 0])
]
