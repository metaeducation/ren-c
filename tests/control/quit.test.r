; functions/control/quit.r
; In R3, DO of a script provided as a text! code catches QUIT, just as it
; would do for scripts in files.

(
    e: rescue [do "Rebol [] quit 42"]
    e.exit-code = 42
)

(99 = do -[Rebol [] try do -[Rebol [] quit 42]- quit:value 99]-)

; Returning of Rebol values from called to calling script via QUIT w/arg.
(
    do-script-returning: lambda [value <local> script] [
        script: %tmp-inner.reb
        save:header script compose [quit:value (value)] []
        do script
        elide delete %tmp-inner.reb
    ]
    for-each 'value reduce [
        0
        42
        -[foo]-
        #{CAFE}
        space
        http://somewhere
        1900-01-30
    ][
        assert [value = do-script-returning value]
    ]
    ok
)

[#2190
    ~???~ !! (quit 0)  ; quit is definitional, context must provide
]
