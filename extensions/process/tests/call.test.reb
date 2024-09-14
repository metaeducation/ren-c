; call/call.test.reb

; === CALL/OUTPUT tests ===

https://github.com/metaeducation/ren-c/issues/537
https://github.com/metaeducation/ren-c/commit/298409f485420ecd03f0be4b465111be4ad829cd
https://github.com/metaeducation/ren-c/commit/e57c147465f3ed47f297e7a3ce3bb0319635f81f

(
    apply get $call/shell [
        [(system.options.boot) --suppress {"*"} print.reb 100]  ; small

        /input 'none  ; avoid child process eating pastes of length test
        /output data: {}
    ]

    100 = length of data
)
(
    apply get $call/shell [
        [(system.options.boot) --suppress {"*"} print.reb 9000]  ; medium

        /input 'none  ; avoid child process eating pastes of length test
        /output data: {}
    ]

    9000 = length of data
)
(
    apply get $call/shell [
        [(system.options.boot) --suppress {"*"} print.reb 80000]  ; large

        /input 'none  ; avoid child process eating pastes of length test
        /output data: {}
    ]

    80'000 = length of data
)
(
    ; extra large CALL/OUTPUT (500K+), test only run if can find git binary
    ;
    if not exists? %/usr/bin/git [okay] else [
        data: {}
        call/output [
            %/usr/bin/git log (spaced [
                "--pretty=format:'["
                    "commit: {%h}"
                    "author: {%an}"
                    "email: {%ae}"
                    "date-string: {%ai}"
                    "summary: {%s}"
                "]'"
            ])
        ] data
        did all [
            500'000 < length of data
            find data "summary: {Initial commit}"
        ]
    ]
)


; Tests feeding input and taking output from various sources
[
    (echoer: enclose specialize get $call/input/output [
        command: [
            (system.options.boot) --suppress {"*"} -q
            --do "write-stdout read system.ports.input"
        ]
    ] frame -> [
        out: frame.output
        eval frame
        out
    ], ok)

    ("Foo" = echoer "Foo" "")
    ("Rҽʋσʅυƚισɳ" = echoer "Rҽʋσʅυƚισɳ" "")
    ("One^/Two" = echoer "One^/Two" "")

    (#{466F6F} = echoer #{466F6F} #{})
    ("Foo" = echoer #{466F6F} "")
    (#{466F6F} = echoer "Foo" #{})
    (#{DECAFBAD} = echoer #{DECAFBAD} #{})

    ; !!! Different UTF-8 error results on Windows vs. Linux at the moment
    (
        e: sys.util/rescue [#{DECAFBAD} = echoer #{DECAFBAD} ""]
        any [e.id = 'bad-utf8, e.id = 'bad-utf8-bin-edit]
    )
]

; Both unix and windows echo text back, so this is a good test of the shell
; But line endings will vary because it's not redirected.  :-/
(
    call/shell/output "echo test" out: ""
    any [
        "test^M^/" = out
        "test^/" = out
    ]
)

(
    4 = call/relax [(system.options.boot) --do "quit 4"]
)
~???~ !! (
    call [(system.options.boot) --do "quit 4"]
)
