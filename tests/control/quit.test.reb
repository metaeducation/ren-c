; functions/control/quit.r
; In R3, DO of a script provided as a text! code catches QUIT, just as it
; would do for scripts in files.

(42 = do "Rebol [] quit/with 42")

(99 = do {Rebol [] do {Rebol [] quit/with 42} 99})

; Returning of Rebol values from called to calling script via QUIT w/arg.
(
    do-script-returning: lambda [value <local> script] [
        script: %tmp-inner.reb
        save/header script compose [quit/with (value)] []
        do script
        elide delete %tmp-inner.reb
    ]
    for-each value reduce [
        42
        {foo}
        #{CAFE}
        blank  ; forces quoting in save, see note
        http://somewhere
        1900-01-30
        make object! [x: 42]
    ][
        assert [value = do-script-returning value]
    ]
    true
)

[#2190
    (error? trap [catch/quit [attempt [quit]] 1 / 0])
]
