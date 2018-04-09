; functions/control/quit.r
; In R3, DO of a script provided as a string! code catches QUIT, just as it
; would do for scripts in files.

(42 = do "quit/with 42")

(99 = do {do {quit/with 42} 99})

; Returning of Rebol values from called to calling script via QUIT/return.
(
    do-script-returning: func [value /local script] [
        save/header script: %tmp-inner.reb compose ['quit/return (value)] []
        do script
    ]
    all map-each value reduce [
        42
        {foo}
        #{CAFE}
        blank
        http://somewhere
        1900-01-30
        context [x: 42]
    ] [
        value = do-script-returning value
    ]
)

[#2190
    (error? try [catch/quit [attempt [quit]] 1 / 0])
]
