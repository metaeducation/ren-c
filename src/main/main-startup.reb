REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Command line processing and startup code called by %main.c"
    File: %main-startup.r
    Type: module
    Name: Ren-C-Startup
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This is the Rebol code called by %main.c that handles things like
        loading boot extensions, doing command-line processing, and getting
        things otherwise set up for running the console.

        Because it is run early, this is before several things have been
        established.  That includes a Ctrl-C handler.  It therefore should
        not be running any user code directly.  Instead it should return a
        request of code to be handed to the console extension to be provoked
        with (see the /PROVOKE refinement of CONSOLE for more information).
    }
]

boot-print: redescribe [
    "Prints during boot when not quiet."
](
    enclose :print func [f] [if not system.options.quiet [do f]]
)

loud-print: redescribe [
    "Prints during boot when verbose."
](
    enclose :print func [f] [if system.options.verbose [do f]]
)

make-banner: func [
    {Build startup banner}
    return: [text!]
    fmt [block!]
][
    let str: make text! 200
    let star: append/dup make text! 74 #"*" 74
    let spc: format ["**" 70 "**"] ""

    let [a b s]
    parse fmt [
        some [
            [
                set a: text! (s: format ["**  " 68 "**"] a)
              | '= set a: [text! | word! | set-word!] [
                        b: <here>
                          path! (b: get b.1)
                        | word! (b: get b.1)
                        | block! (b: spaced b.1)
                        | text! (b: b.1)
                    ]
                    (
                        b: default ["(none)"]  ; show (none) for NULL
                        s: format ["**    " 11 55 "**"] reduce [a b]
                    )
              | '* (s: star)
              | '- (s: spc)
            ]
            (append append str s newline)
        ]
        end
    ]
    return str
]


boot-banner: [
    *
    -
    "REBOL 3.0 (Ren-C branch)"
    -
    = Copyright: "2012 REBOL Technologies"
    = Copyright: "2012-2021 Ren-C Open Source Contributors"
    = "" "Licensed Under LGPL 3.0, see LICENSE."
    = Website:  "http://github.com/metaeducation/ren-c"
    -
    = Version:   system.version
    = Platform:  system.platform
    = Build:     system.build
    = Commit:    system.commit
    -
    = Language:  system.locale.language*
    = Locale:    system.locale.locale*
    = Home:      system.options.home
    = Resources: system.options.resources
    = Console:   system.console.name
    -
    *
]

about: func [
    "Information about REBOL"
    return: <none>
][
    print make-banner boot-banner
]


; The usage instructions should be automatically generated from a table,
; the same table used to generate parse rules for the command line processing.
;
; There has been some talk about generalizing command-line argument handling
; in a way that a module can declare what its arguments and types are, much
; like an ordinary ACTION!, and all the proxying is handled for the user.
; Work done on the dialect here could be shared in common.
;
usage: func [
    "Prints command-line arguments."
    return: <none>
][
;       --cgi (-c)       Load CGI utiliy module and modes
;       --version tuple  Script must be this version or greater
;       Perhaps add --reqired version-tuple for above TBD

    print trim/auto copy {
    Command line usage:

        REBOL [options] [script] [arguments]

    Standard options:

        --do expr        Evaluate expression (quoted)
        --help (-?)      Display this usage information
        --script file    Explicitly provide script to run, change working dir
        --fragment file  Run without changing directory, CR+LF ok on Windows
        --version (-v)   Display version only (then quit)
        --               End of options (treat remainder as script args)

    Special options:

        --about          Prints full banner of information when console starts
        --debug flags    For user scripts (system.options.debug)
        --halt (-h)      Leave console open when script is done
        --import file    Import a module prior to script
        --quiet (-q)     No startup banners or information
        --resources dir  Manually set where Rebol resources directory lives
        --suppress ""    Suppress any found start-up scripts  Use "*" to suppress all.
        --trace (-t)     Enable trace mode during boot
        --verbose        Show detailed startup information

    Examples:

        REBOL script.reb
        REBOL -s script.reb
        REBOL script.reb 10:30 test@example.com
        REBOL --do "print [1 + 1]"
        #!/sbin/REBOL -cs

    Console (no script/arguments or Standard option used):

        REBOL
        REBOL -q --about --suppress "%rebol.reb %user.reb"
    }
]

license: func [
    "Prints the REBOL/core license agreement."
    return: <none>
][
    print system.license
]

host-script-pre-load: func [
    {Code registered as a hook when a module or script are loaded}
    return: <none>
    is-module [logic!]
    hdr [blank! object!]
        {Header object (will be blank for DO of BINARY! with no header)}
][
    ; Print out the script info
    boot-print [
        (if is-module ["Module:"] else ["Script:"]) select hdr 'title
            "Version:" select hdr 'version
            "Date:" select hdr 'date
    ]
]

; !!! This file is bound into lib, along with adding its top-level SET-WORD!s
; to lib.  Due to the way the lib and user contexts work, these functions
; from the Process and Filesystem extensions would not be bound, because
; they are loaded after the code has started running:
;
; https://forum.rebol.info/t/the-real-story-about-user-and-lib-contexts/764
;
; We could use them via `lib.<whatever>`, but then each callsite would have to
; document the issue.  So we make them SET-WORD!s added to lib up front, so
; the lib modification gets picked up.
;
; NOTE: We depend on...
;
; [get-current-exec file-to-local local-to-file what-dir change-dir]
;
; These are implicitly picked up from LIB but would need to be done different


main-startup: func [
    "Usermode command-line processing: handles args, security, scripts"

    return: [<opt> any-value!] "!!! Narrow down return type?"
    argv {Raw command line argument block received by main() as STRING!s}
        [block!]
    <with>
    main-startup  ; unset when finished with itself
    about usage license  ; exported to lib, see notes
    <static>
        o (system.options)  ; shorthand since options are often read/written
][
    ; We hook the RETURN function so that it actually returns an instruction
    ; that the code can build up from multiple EMIT statements.

    let instruction: copy []

    let emit: func [
        {Builds up sandboxed code to submit to C, hooked RETURN will finalize}

        item "ISSUE! directive, TEXT! comment, (<*> composed) code BLOCK!"
            [block! issue! text!]
        <with> instruction
    ][
        switch type of item [
            issue! [
                if not empty? instruction [append/line instruction [,]]
                insert instruction item
            ]
            text! [
                append/line instruction compose [comment (item)]
            ]
            block! [
                if not empty? instruction [append/line instruction [,]]
                append/line instruction compose/deep <*> item
            ]
            unreachable
        ]
    ]

    return: func [
        {Hooked RETURN function which finalizes any gathered EMIT lines}

        state "Describes the RESULT that the next call to HOST-CONSOLE gets"
            [integer! tag! group! datatype!]
        <with> instruction prior
        <local> return-to-c (:return)  ; capture HOST-CONSOLE's RETURN
    ][
        switch state [
            <start-console> [
                ; Done actually via #start-console, but we return something
            ]
            <prompt> [
                emit [system.console.print-gap]
                emit [system.console.print-prompt]
                emit [reduce [
                    system.console.input-hook
                ]]  ; gather first line (or BLANK!), put in BLOCK!
            ]
            <halt> [
                emit [halt]
                emit [fail {^-- Shouldn't get here, due to HALT}]
            ]
            <die> [
                emit [quit 1]  ; catch-all bash code for general errors
                emit [fail {^-- Shouldn't get here, due to QUIT}]
            ]
            <bad> [
                emit #no-unskin-if-error
                emit [print mold '(<*> prior)]
                emit [fail ["Bad REPL continuation:" '(<*> result)]]
            ]
        ] then [
            return-to-c instruction
        ]

        return-to-c switch type of state [
            integer! [  ; just tells the calling C loop to exit() process
                assert [empty? instruction]
                state
            ]
            datatype! [  ; type assertion, how to enforce this?
                emit spaced ["^-- Result should be" an state]
                instruction
            ]
            group! [  ; means "submit user code"
                assert [empty? instruction]
                state
            ]
        ] else [
            emit [fail [{Bad console instruction:} (<*> mold state)]]
        ]
    ]

    ; The internal panic() and panic_at() calls in C code cannot be hooked.
    ; However, if you use the PANIC native in usermode, that *can* be hijacked.
    ; This prints a message to distinguish the source of the panic, which is
    ; useful to know that is what happened (and it demonstrates the ability
    ; to hook it, just to remind us that we can).
    ;
    hijack :panic adapt (copy :panic) [
        print "PANIC ACTION! is being triggered from a usermode call"
        print mold reason
        ;
        ; ...adaptation falls through to our copy of the original PANIC
    ]

    system.product: 'core

    ; !!! If we don't load the extensions early, then we won't get the GET-ENV
    ; function (it's provided by the Process extension).  Though optional,
    ; knowing where the home directory is, is needed for running startup
    ; scripts.  This should be rethought because it may be that extensions
    ; can be influenced by command line parameters as well.
    ;
    loud-print "Loading boot extensions..."
    for-each collation builtin-extensions [
        load-extension collation
    ]

    ; While some people may think that argv[0] in C contains the path to
    ; the running executable, this is not necessarily the case.  The actual
    ; method for getting the current executable path is OS-specific:
    ;
    ; https://stackoverflow.com/q/1023306/
    ; http://stackoverflow.com/a/933996/211160
    ;
    ; It's not foolproof, so it might come back null.  The console code can
    ; then decide if it wants to fall back on argv[0]
    ;
    switch type of system.options.boot: get-current-exec [
        file! []  ; found it
        null []  ; also okay (not foolproof!)
        fail
    ]

    === HELPER FUNCTIONS ===

    let die: func [
        {A graceful way to "FAIL" during startup}

        reason "Error message"
            [text! block!]
        /error "Error object, shown if --verbose option used"
            [error!]
        <with> return
    ][
        print "Startup encountered an error!"
        print ["**" if block? reason [spaced reason] else [reason]]
        if error [
            print either o.verbose [
                [error]
            ][
                "!! use --verbose for more detail"
            ]
        ]
        return <die>
    ]

    let to-dir: func [
        {Convert string path to absolute dir! path}

        return: "Null if not found"
            [<opt> file!]
        dir [<blank> text!]
    ][
        return all [
            not empty? dir
            exists? dir: clean-path/dir local-to-file dir
            dir
        ]
    ]

    let get-home-path: func [
        {Return HOME path (e.g. $HOME on *nix)}
        return: [<opt> file!]
    ][
        let get-env: attempt [:system.modules.Process.get-env] else [
            loud-print [
                "Interpreter not built with GET-ENV, can't detect HOME dir" LF
                "(Build with Process extension enabled to address this)"
            ]
            return null
        ]

        return to-dir try any [
            get-env 'HOME
            all [
                let homedrive: get-env 'HOMEDRIVE
                let homepath: get-env 'HOMEPATH
                join homedrive homepath
            ]
        ]
    ]

    let get-resources-path: func [
        {Return platform specific resources path.}
        return: [<opt> file!]
    ][
        ; lives under systems.options.home

        let path: join o.home switch system.platform.1 [
            'Windows [%REBOL/]
        ] else [
            %.rebol/  ; default *nix (covers Linux, MacOS (OS X) and Unix)
        ]

        return if exists? path [path]
    ]

    ; Set system.users.home (users HOME directory)
    ; Set system.options.home (ditto)
    ; Set system.options.resources (users Rebol resource directory)
    ;
    ; NB. Above can be overridden by --home option
    ;
    all [
        let home-dir: try get-home-path
        system.user.home: o.home: home-dir
        let resources-dir: try get-resources-path
        o.resources: resources-dir
    ]

    sys.script-pre-load-hook: :host-script-pre-load

    let do-string: _  ; will be set if a string is given with --do

    let quit-when-done: _  ; by default run CONSOLE

    ; Process the option syntax out of the command line args in order to get
    ; the intended arguments.  TAKEs each option string as it goes so the
    ; array remainder can act as the args.

    ; The host executable may have initialized system.options.boot, using
    ; a platform-specific method, since argv[0] is *not* always exe path:
    ;
    ; https://stackoverflow.com/q/1023306/
    ; http://stackoverflow.com/a/933996/211160
    ;
    ; If it did not initialize it, fall back on argv[0], if available.
    ;
    if not tail? argv [
        o.boot: default [clean-path local-to-file first argv]
        take argv
    ]
    if o.boot [
        o.bin: first split-path o.boot
    ]

    let param-or-die: func [
        {Take --option argv and then check if param arg is present, else die}
        return: [text!]
        option [text!] {Command-line option (switch) used}
    ][
        take argv
        return first argv else [die [option {parameter missing}]]
    ]

    ; As we process command line arguments, we build up an "instruction" block
    ; which is going to be passed back.  This way you can have multiple
    ; --do "..." or script arguments, and they will be run in a sequence.
    ;
    ; The instruction block is run in a sandbox which prevents cancellation
    ; or failure from crashing the interpreter.  (MAIN-STARTUP is not allowed
    ; to cancel or fail.  See notes in %src/main/README.md)
    ;
    ; The directives at the start of the instruction dictate that Ctrl-C
    ; during the startup instruction will exit with code 130, and any errors
    ; that arise will be reported and result in exit code 1.
    ;

    emit #quit-if-halt

    ; !!! Counting down on command line script errors was making the console
    ; extension dependent on EVENT!, which the WebAssembly build did not want.
    ; It wasn't the most popular feature to begin with, so it is disabled for
    ; the time being:
    ;
    ; https://github.com/metaeducation/ren-c/issues/1000
    ;
    comment [emit #countdown-if-error]
    emit #die-if-error

    let is-script-implicit: true
    let check-encap: true

    loop [not tail? argv] [

        let is-option: parse?/case argv.1 [

            ["--" end] (
                ; Double-dash means end of command line arguments, and the
                ; rest of the arguments are going to be positional.  In
                ; Rebol's case, that means a file to run (if --script or --do
                ; not explicit) and its arguments (if anything following).
                ;
                take argv
                break
            )
        |
            "--about" end (
                o.about: true  ; show full banner (ABOUT) on startup
            )
        |
            "--breakpoint" end (
                c-debug-break-at to-integer param-or-die "BREAKPOINT"
            )
        |
            ["--cgi" | "-c"] end (
                o.quiet: true
                o.cgi: true
            )
        |
            "--debug" end (
                ; was coerced to BLOCK! before, but what did this do?
                ;
                o.debug: to logic! param-or-die "DEBUG"
            )
        |
            "--do" end (
                ;
                ; A string of code to run, e.g. `r3 --do "print {Hello}"`
                ;
                o.quiet: true  ; don't print banner, just run code string
                quit-when-done: default [true]  ; override blank, not false

                is-script-implicit: false  ; must use --script

                emit {Use /ONLY so that QUIT/WITH quits, vs. return DO value}
                emit [do/only (<*> param-or-die "DO")]
            )
        |
            ["--halt" | "-h"] end (
                quit-when-done: false  ; overrides true
            )
        |
            ["--help" | "-?"] end (
                usage
                quit-when-done: default [true]
            )
        |
            "--import" end (
                lib.import local-to-file param-or-die "IMPORT"
            )
        |
            "--no-encap" end (
                check-encap: false
            )
        |
            ["--quiet" | "-q"] end (
                o.quiet: true
            )
        |
            "--resources" end (
                o.resources: (to-dir param-or-die "RESOURCES") else [
                    die "RESOURCES directory not found"
                ]
            )
        |
            "--suppress" end (
                let param: param-or-die "SUPPRESS"
                o.suppress: if param = "*" [
                    ; suppress all known start-up files
                    [%rebol.reb %user.reb %console-skin.reb]
                ] else [
                    make block! param
                ]
            )
        |
            "--script" end (
                o.script: param-or-die "SCRIPT"
                quit-when-done: default [true]  ; overrides blank, not false

                is-script-implicit: false  ; not the first post-option arg
            )
        |
            ; Added initially for GitHub CI.  Concept is that it takes a
            ; filename and runs it with "shell semantics", e.g. how bash would
            ; work.  The code is loaded from the file and run as a string, not
            ; through the DO %FILE mechanics that change the directory.
            ;
            "--fragment" end (
                let code: read local-to-file param-or-die "FRAGMENT"
                is-script-implicit: false  ; must use --script

                o.quiet: true  ; don't print banner, just run code string
                quit-when-done: default [true]  ; override blank, not false

                ; !!! Here we make a concession to Windows CR LF, only when
                ; running code fragments.  This was added because when you use
                ; a custom shell in GitHub CI, it takes a piece out of the
                ; yaml file (which has no CR LF) and puts it in a temporary
                ; file which does have CR LF on Windows.  This would be
                ; difficult to work around.
                ;
                if system.version.4 = 3 [  ; Windows
                    code: deline code  ; Removes CR or leaves as-is
                ] else [
                    code: as text! code
                ]
                emit {Use /ONLY so that QUIT/WITH quits, vs. return DO value}
                emit [do/only (code)]
            )
        |
            ["-t" | "--trace"] end (
                trace on  ; did they mean trace just the script/DO code?
            )
        |
            "--verbose" end (
                o.verbose: true
            )
        |
            ["-v" | "-V" | "--version"] end (
                boot-print ["Rebol 3" system.version]  ; version tuple
                quit-when-done: default [true]
            )
        |
            "-w" end (
                ; No window; not currently applicable
            )
        |
            [let cli-option copy cli-option: [["--" | "-" | "+"] to <end>] (
                die [
                    "Unknown command line option:" cli-option LF
                    {!! For a full list of command-line options use: --help}
                ]
            )]
        ]

        if not is-option [break]

        take argv
    ]

    ; Taking a command-line `--breakpoint NNN` parameter is helpful if a
    ; problem is reproducible, and you have a tick count in hand from a
    ; panic(), REBSER.tick, REBFRM.tick, REBVAL.extra.tick, etc.  But there's
    ; an entanglement issue, as any otherwise-deterministic tick from a prior
    ; run would be thrown off by the **ticks added by the userspace parameter
    ; processing of the command-line for `--breakpoint`**!  :-/
    ;
    ; The /COMPENSATE option addresses this problem.  Pass it a reasonable
    ; upper bound for how many ticks you think could have been added to the
    ; parse, if `--breakpoint` was processed (even though it might not have
    ; been processed).  Regardless of whether the switch was present or not,
    ; the tick count rounds up to a reproducible value, using this method:
    ;
    ; https://math.stackexchange.com/q/2521219/
    ;
    ; At time of writing, 1000 ticks should be *way* more than enough for both
    ; the PARSE steps and the evaluation steps `--breakpoint` adds.  Yet some
    ; things could affect this, e.g. a complex userspace TRACE which was
    ; run during boot.
    ;
    attempt [c-debug-break-at/compensate 1000]  ; fails in release build

    ; As long as there was no `--script` or `--do` passed on the command line
    ; explicitly, the first item after the options is implicitly the script.
    ;
    all [is-script-implicit, not tail? argv] then [
        o.script: take argv
        quit-when-done: default [true]
    ]

    if o.script [
        ; If people say `r3 http://example.com/script.r` we want to interpret
        ; that as a URL!, not a file.  This raises some questions about how
        ; you would deal with a path where `http:` was the name of a directory
        ; which is possible on some filesystems:
        ;
        ;    https://forum.rebol.info/t/1764
        ;
        alphanum: charset [#"A" - #"Z" #"a" - #"z" #"0" #"9"]
        o.script: parse o.script [some alphanum ":" to <end>] then [
            to url! o.script
        ]
        else [
            local-to-file o.script
        ]
    ]


    ; Whatever is left is the positional arguments, available to the script.
    ;
    o.args: argv  ; whatever's left is positional args


    let boot-embedded: if check-encap [get-encap system.options.boot]

    if any [boot-embedded, o.script] [o.quiet: true]

    ; Set option/paths for /path, /boot, /home, and script path
    ;
    o.path: what-dir  ;dirize any [o.path o.home]

    ; !!! this was commented out.  Is it important?
    comment [
        if slash <> first o.boot [o.boot: clean-path o.boot]
    ]

    if file? o.script [  ; Get the path
        let script-path: split-path o.script
        case [
            slash = first first script-path []      ; absolute
            %./ = first script-path [script-path.1: o.path]   ; curr dir
        ] else [
            insert first script-path o.path ; relative
        ]
    ]

    ; Convert command line arg strings as needed:
    let script-args: o.args  ; save for below

    ;
    ; start-up scripts, o.loaded tracks which ones are loaded (with full path)
    ;

    ; Evaluate rebol.reb script:
    ; !!! see https://github.com/rebol/rebol-issues/issues/706
    ;
    all [
        not find o.suppress %rebol.reb
        elide (loud-print ["Checking for rebol.reb file in" o.bin])
        exists? join o.bin %rebol.reb
    ] then [
        trap [
            do (join o.bin %rebol.reb)
            append o.loaded (join o.bin %rebol.reb)
            loud-print ["Finished evaluating script:" (join o.bin %rebol.reb)]
        ] then e -> [
            die/error "Error found in rebol.reb script" e
        ]
    ]

    ; Evaluate user.reb script:
    ; !!! Should it query permissions to ensure RESOURCES is owner writable?
    ;
    all [
        o.resources
        not find o.suppress %user.reb
        elide (loud-print ["Checking for user.reb file in" o.resources])
        exists? join o.resources %user.reb
    ] then [
        trap [
            do join o.resources %user.reb
            append o.loaded join o.resources %user.reb
            loud-print ["Finished evaluating:" join o.resources %user.reb]
        ] then e -> [
            die/error "Error found in user.reb script" e
        ]
    ]

    let main
    all [
        o.encap: boot-embedded  ; null if no encapping

        ; The encapping is an embedded zip archive.  get-encap did
        ; the unzipping into a block, and this information must be
        ; made available somehow.  It shouldn't be part of the "core"
        ; but just responsibility of the host that supports encap
        ; based loading.  We put it in o.encap, and see if it contains a
        ; %main.reb...if it does, we run it.

        main: select boot-embedded %main.reb
    ]
    then [
        if not binary? main [
            die "%main.reb not a BINARY! in encapped data"
        ]
        let [code header]: load main

        ; !!! This needs to be thought through better, in terms of whether
        ; it's a module and handling HEADER correctly.  Also, any scripts
        ; should be passed as arguments...not executed.  And the active
        ; directory should be inside the ZIP, so that FILE! paths are
        ; resolved relative to %main.reb's location.  But for now, just do
        ; a proof of concept by showing execution of a main.reb if that is
        ; found in the encapping.

        emit [do/only (<*> code)]
        quit-when-done: default [true]
    ]

    ; Evaluate any script argument, e.g. `r3 test.r` or `r3 --script test.r`
    ;
    ; Note: We can't do this by appending the instruction as we go along
    ; processing the arguments, as `--do` does, because the arguments aren't
    ; known at the moment of hitting the `--script` enough to fill in the
    ; slots of the COMPOSE.
    ;
    ; This can be worked around with multiple do statements in a row, e.g.:
    ;
    ;     r3 --do "do %script1.reb" --do "do %script2.reb"
    ;
    any [
        file? o.script
        url? o.script
    ] then [
        emit {Use DO/ONLY so QUIT/WITH exits vs. being DO's return value}
        emit [do/only/args (<*> o.script) (<*> script-args)]
    ]

    main-startup: '~main-startup-done~  ; free function for GC

    if quit-when-done [
        emit [quit 0]
        return <unreachable>
    ]

    emit #start-console

    return <start-console>
]

export [main-startup about usage license]
