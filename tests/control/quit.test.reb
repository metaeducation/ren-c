; functions/control/quit.r
; In R3, DO of a script provided as a text! code catches QUIT, just as it
; would do for scripts in files.

(42 = do "quit 42")

(99 = do {do {quit 42} 99})

; Returning of Rebol values from called to calling script via QUIT/WITH.
(
    do-script-returning: func [value <local> script] [
        script: %tmp-inner.reb
        save/header script compose [quit (value)] []
        do script
        elide delete %tmp-inner.reb
    ]
    all map-each value reduce [
        42
        {foo}
        #{CAFE}
        blank
        http://somewhere
        1900-01-30
        make object! [x: 42]
    ][
        value = do-script-returning value
    ]
)

[#2190
    (error? trap [catch/quit [attempt [quit]] 1 / 0])
]
