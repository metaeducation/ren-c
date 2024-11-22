REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Command line processing and startup code called by %main.c"
    File: %main-startup.r
    Type: module
    Name: Ren-C-Startup
    Rights: --{
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Description: --{
        This is the Rebol code called by %main.c that handles things like
        loading boot extensions, doing command-line processing, and getting
        things otherwise set up for running the console.

        Because it is run early, this is before several things have been
        established.  That includes a Ctrl-C handler.  It therefore should
        not be running any user code directly.  Instead it should return a
        request of code to be handed to the console extension to be provoked
        with (see the /PROVOKE refinement of CONSOLE for more information).
    }--
]

boot-print: redescribe [
    "Prints during boot when not quiet."
](
    enclose print/ f -> [if no? system.options.quiet [eval f]]
)

loud-print: redescribe [
    "Prints during boot when verbose."
](
    enclose print/ f -> [if yes? system.options.verbose [eval f]]
)

/make-banner: func [
    "Build startup banner"
    return: [text!]
    fmt [block!]
][
    let str: make text! 200
    let star: append:dup make text! 74 #"*" 74
    let spc: format ["**" 70 "**"] ""

    let [a b s]
    parse3 fmt [
        some [
            [
                a: text! (s: format ["**  " 68 "**"] a)
              | '= a: [text! | word! | &set-word?] [
                        b: <here>
                          tuple! (b: get inside fmt b.1)
                        | word! (b: get inside fmt b.1)
                        | block! (b: spaced inside fmt b.1)
                        | text! (b: b.1)
                    ]
                    (
                        b: default ["~null~"]
                        s: format ["**    " 11 55 "**"] reduce [a b]
                    )
              | '* (s: star)
              | '- (s: spc)
            ]
            (append append str s newline)
        ]
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

/about: func [
    "Information about REBOL"
    return: [~]
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
/usage: func [
    "Prints command-line arguments."
    return: [~]
][
;       --cgi (-c)       Load CGI utiliy module and modes
;       --version tuple  Script must be this version or greater
;       Perhaps add --reqired version-tuple for above TBD

    print trim:auto copy --{
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
    }--
]

/license: func [
    "Prints the REBOL/core license agreement."
    return: [~]
][
    print system.license
]

/host-script-pre-load: func [
    "Code registered as a hook when a module or script are loaded"
    return: [~]
    is-module [yesno?]
    hdr "Header object (missing for DO of BLOB! with no header)"
        [~null~ object!]
][
    ; Print out the script info
    boot-print [
        (if yes? is-module ["Module:"] else ["Script:"])
            @(any [try hdr.title, "(anonymous)"])
            "Version:" @(any [try hdr.version, "null"])
            "Date:" @(any [try hdr.date, "null"])
    ]
]

; !!! This file is bound into lib, along with adding its top-level SET-WORD!s
; to lib.  Due to the way the lib and user contexts work, these functions
; from the Process and Filesystem extensions would not be bound, because
; they are loaded after the code has started running:
;
; https://forum.rebol.info/t/the-real-story-about-user-and-lib-contexts/764
;
; We could use them via `lib/<whatever>`, but then each callsite would have to
; document the issue.  So we make them SET-WORD!s added to lib up front, so
; the lib modification gets picked up.
;
; NOTE: We depend on...
;
; [get-current-exec file-to-local local-to-file what-dir change-dir]
;
; These are implicitly picked up from LIB but would need to be done different


/main-startup: func [
    "Usermode command-line processing: handles args, security, scripts"

    return: [any-value?] "!!! Narrow down return type?"
    argv "Raw command line argument block received by main() as TEXT!s"
        [block!]
]
bind construct [
    o: system.options  ; shorthand since options are often read or written
][
    ; We hook the RETURN function so that it actually returns an instruction
    ; that the code can build up from multiple EMIT statements.
    ;
    ; 1. Each module or script gets its own QUIT, and every console instruction
    ;    execution also gets its own quit.  If we didn't use quoting to leave
    ;    the instruction unbound, then QUIT would bind to the main-startup's
    ;    QUIT...which is not available after the module initialization has
    ;    finished running.  Leaving it unbound means that the console engine
    ;    (which processes these instructions) will supply it.

    let instruction: copy '[]  ; quote for no binding, want console binding [1]

    let /emit: func [
        "Builds up sandboxed code to submit to C, hooked RETURN will finalize"

        return: [~]
        item "ISSUE! directive, TEXT! comment, (<*> composed) code BLOCK!"
            [block! issue! text!]
        <with> instruction
    ][
        switch:type item [
            issue! [
                if not empty? instruction [append:line instruction ',]
                insert instruction item
            ]
            text! [
                append:line instruction spread compose $() '[comment (item)]
            ]
            block! [
                if not empty? instruction [append:line instruction ',]
                let pattern: inside item '(<*>)
                append:line instruction spread compose:deep pattern item
            ]
            fail ~<unreachable>~
        ]
    ]

    /return: func [
        "Hooked RETURN function which finalizes any gathered EMIT lines"

        return: []
        state "Describes the RESULT that the next call to HOST-CONSOLE gets"
            [integer! tag! group! type-block!]
        <with> instruction
        <local> /return-to-c (return/)  ; capture HOST-CONSOLE's RETURN
    ][
        switch state [
            <die> [
                emit [quit 1]  ; must leave unbound to get console's QUIT [1]
                emit [fail ~<unreachable>~]
            ]
            <quit> [
                emit [quit 0]  ; must leave unbound to get console's QUIT [1]
                emit [fail ~<unreachable>~]
            ]
            <start-console> [
                ; Done actually via #start-console, but we return something
            ]
        ] then [
            run return-to-c instruction
        ]

        return-to-c switch:type state [
            integer! [  ; just tells the calling C loop to exit() process
                if not empty? instruction [
                    print mold instruction
                ]
                assert [empty? instruction]
                state
            ]
            type-block! [  ; type assertion, how to enforce this?
                emit spaced ["^^-- Result should be" @state]
                instruction
            ]
            group! [  ; means "submit user code"
                assert [empty? instruction]
                state
            ]
        ] else [
            emit [fail ["Bad console instruction:" (<*> mold state)]]
        ]
    ]

    ; The internal panic() and panic_at() calls in C code cannot be hooked.
    ; However, if you use the PANIC native in usermode, that *can* be hijacked.
    ; This prints a message to distinguish the source of the panic, which is
    ; useful to know that is what happened (and it demonstrates the ability
    ; to hook it, just to remind us that we can).
    ;
    hijack panic/ adapt (copy unrun panic/) [
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
    for-each 'collation builtin-extensions [
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
    if defined? $get-current-exec [
        switch:type system.options.boot: get-current-exec [
            file! []  ; found it
            null?! []  ; also okay (not foolproof!)
            fail "GET-CURRENT-EXEC returned unexpected datatype"
        ]
    ] else [
        system.options.boot: null
    ]

    === HELPER FUNCTIONS ===

    let /die: lambda [
        "A graceful way to "FAIL" during startup"

        reason "Error message"
            [text! block!]
        :error "Error object, shown if --verbose option used"
            [error!]
    ][
        print "Startup encountered an error!"
        print ["**" if block? reason [spaced reason] else [reason]]
        if error [
            print either yes? o.verbose [
                [error]
            ][
                "!! use --verbose for more detail"
            ]
        ]
        return <die>
    ]

    let /to-dir: func [
        "Convert string path to absolute dir! path"

        return: "Null if not found"
            [~null~ file!]
        dir [<maybe> text!]
    ][
        return all [
            not empty? dir
            exists? dir: clean-path:dir local-to-file dir
            dir
        ]
    ]

    let /get-home-path: func [
        "Return HOME path (e.g. $HOME on *nix)"
        return: [~null~ element? file!]
    ][
        let get-env: if select system.modules 'Process [
            runs :system.modules.Process.get-env
        ] else [
            loud-print [
                "Interpreter not built with GET-ENV, can't detect HOME dir" LF
                "(Build with Process extension enabled to address this)"
            ]
            return null
        ]

        return to-dir maybe any [
            get-env 'HOME
            all [
                let homedrive: get-env 'HOMEDRIVE
                let homepath: get-env 'HOMEPATH
                join homedrive homepath
            ]
        ]
    ]

    let /get-resources-path: func [
        "Return platform specific resources path"
        return: [~null~ file!]
    ][
        ; lives under systems.options.home

        let path: join o.home switch system.platform.1 [
            'Windows [%REBOL/]
        ] else [
            %.rebol/  ; default *nix (covers Linux, MacOS (OS X) and Unix)
        ]

        return if exists? path [path] else [null]
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

    sys.util.script-pre-load-hook: host-script-pre-load/

    let quit-when-done: null  ; by default run CONSOLE

    ; Process the option syntax out of the command line args in order to get
    ; the intended arguments.  TAKEs each option string as it goes so the
    ; block remainder can act as the args.

    ; The host executable may have initialized system.options.boot, using
    ; a platform-specific method, since argv[0] is *not* always exe path:
    ;
    ; https://stackoverflow.com/q/1023306/
    ; http://stackoverflow.com/a/933996/211160
    ;
    ; If it did not initialize it, fall back on argv[0], if available.
    ;
    if not tail? argv [
        if defined? $local-to-file [
            o.boot: default [
                clean-path local-to-file first argv
            ]
        ]
        take argv
    ]
    if o.boot [
        o.bin: split-path o.boot
    ] else [
        o.bin: null
    ]

    let /param-missing: func [
        "Take --option argv and then check if param arg is present, else die"
        return: []
        option [text!] "Name of command-line option (switch) used"
    ][
        die [option "parameter missing"]
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

    let is-script-implicit: 'yes
    let check-encap: 'yes

    let param

    o.args: copy parse3:case argv [opt some [ ; COPY to drop processed argv

        ; Double-dash means end of command line arguments, and the rest of the
        ; arguments are going to be positional.  In Rebol's case, that means a
        ; file to run (if --script or --do not explicit) and its arguments (if
        ; anything following).

        ["--" | <end>]
        accept <here>  ; rest of command line arguments (or none)
    |
        [ahead text! | (panic "ARGV element not TEXT!")]  ; argv.N must be text

        "--about" (
            o.about: 'yes  ; show full banner (ABOUT) on startup
        )
    |
        ["--cgi" | "-c"] (
            o.quiet: 'yes
            o.cgi: 'yes
        )
    |
        "--debug" [param: text! | (param-missing "DEBUG")] (
            o.debug: transcode param  ; !!! didn't have any references
        )
    |
        "--do" [param: text! | (param-missing "DO")] (
            ;
            ; A string of code to run, e.g. `r3 --do "print -{Hello}-"`
            ;
            o.quiet: 'yes  ; don't print banner, just run code string
            quit-when-done: default ['yes]

            is-script-implicit: 'no  ; must use --script

            emit [
                do (<*> spaced ["Rebol []" param]) except e -> [
                    quit e.exit-code
                ]
            ]
        )
    |
        ["--halt" | "-h"] (
            quit-when-done: 'no  ; overrides yes
        )
    |
        ["--help" | "-?"] (
            usage
            quit-when-done: default ['yes]
        )
    |
        "--import" [param: text! | (param-missing "IMPORT")] (
            lib/import local-to-file param
        )
    |
        "--no-encap" (
            check-encap: 'no
        )
    |
        ["--quiet" | "-q"] (
            o.quiet: 'yes
        )
    |
        "--resources" [param: text! | (param-missing "RESOURCES")] (
            o.resources: (to-dir param) else [
                die "RESOURCES directory not found"
            ]
        )
    |
        "--suppress" [param: text! | (param-missing "SUPPRESS")] (
            o.suppress: if param = "*" [
                ; suppress all known start-up files
                [%rebol.reb %user.reb %console-skin.reb]
            ] else [
                make block! param
            ]
        )
    |
        "--script" [param: text! | (param-missing "SCRIPT")] (
            o.script: param
            quit-when-done: default ['yes]  ; overrides null, not `no`

            is-script-implicit: 'no  ; not the first post-option arg
        )
    |
        ; Added initially for GitHub CI.  Concept is that it takes a
        ; filename and runs it with "shell semantics", e.g. how bash would
        ; work.  The code is loaded from the file and run as a string, not
        ; through the DO %FILE mechanics that change the directory.
        ;
        "--fragment" [param: text! | (param-missing "FRAGMENT")] (
            let code: read local-to-file param
            is-script-implicit: 'no  ; must use --script

            o.quiet: 'yes  ; don't print banner, just run code string
            quit-when-done: default ['yes]  ; overrides null, not `no`

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
            emit [
                do (<*> spaced ["Rebol []" code]) except e -> [
                    quit e.exit-code
                ]
            ]
        )
    |
        ["-t" | "--trace"] (
            trace on  ; did they mean trace just the script/DO code?
        )
    |
        "--verbose" (
            o.verbose: 'yes
        )
    |
        ["-v" | "-V" | "--version"] (
            boot-print ["Rebol 3" system.version]  ; version tuple
            quit-when-done: default ['yes]
        )
    |
        "-w" (
            ; No window; not currently applicable
        )
    |
        let cli-option: [["--" | "-" | "+"] to <end>] (
            die [
                "Unknown command line option:" cli-option LF
                "!! For a full list of command-line options use: --help"
            ]
        )
    |
        accept <here>  ; rest of command line arguments
    ]]

    ; As long as there was no `--script` or `--do` passed on the command line
    ; explicitly, the first item after the options is implicitly the script.
    ;
    ; Whatever is left is the positional arguments, available to the script.
    ;
    all [yes? is-script-implicit, not tail? o.args] then [
        o.script: take o.args
        quit-when-done: default ['yes]
    ]

    if o.script [
        ; If people say `r3 http://example.com/script.r` we want to interpret
        ; that as a URL!, not a file.  This raises some questions about how
        ; you would deal with a path where `http:` was the name of a directory
        ; which is possible on some filesystems:
        ;
        ;    https://forum.rebol.info/t/1764
        ;
        ; Also on Windows, things like `C:` represent drive letters, so the
        ; heuristic is to check for more than one letter.
        ;
        let alphanum: charset [#"A" - #"Z" #"a" - #"z" #"0" #"9"]
        o.script: parse3 o.script [
            some alphanum ":"  ; SOME, e.g. more than one letter
            accept (to url! o.script)
        ] except [
            local-to-file o.script
        ]
    ]

    let boot-embedded: all [
        yes? check-encap
        system.options.boot
        get-encap system.options.boot
    ]

    if any [boot-embedded, o.script] [o.quiet: 'yes]

    ; Set option/paths for /path, /boot, /home, and script path
    ;
    o.path: what-dir  ;dirize any [o.path o.home]

    ; !!! this was commented out.  Is it important?
    comment [
        if slash <> first o.boot [o.boot: clean-path o.boot]
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
        o.bin
        not find maybe o.suppress %rebol.reb
        elide (loud-print ["Checking for rebol.reb file in" o.bin])
        exists? join o.bin %rebol.reb
    ] then [
        trap [
            do (join o.bin %rebol.reb)
            append o.loaded (join o.bin %rebol.reb)
            loud-print ["Finished evaluating script:" (join o.bin %rebol.reb)]
        ] then e -> [
            die:error "Error found in rebol.reb script" e
        ]
    ]

    ; Evaluate user.reb script:
    ; !!! Should it query permissions to ensure RESOURCES is owner writable?
    ;
    all [
        o.resources
        not find maybe o.suppress %user.reb
        elide (loud-print ["Checking for user.reb file in" o.resources])
        exists? join o.resources %user.reb
    ] then [
        trap [
            do join o.resources %user.reb
            append o.loaded join o.resources %user.reb
            loud-print ["Finished evaluating:" join o.resources %user.reb]
        ] then e -> [
            die:error "Error found in user.reb script" e
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
        if not blob? main [
            die "%main.reb not a BLOB! in encapped data"
        ]
        let [code header]: load main

        ; !!! This needs to be thought through better, in terms of whether
        ; it's a module and handling HEADER correctly.  Also, scripts should
        ; be passed as arguments...not executed.  And the active directory
        ; should be in the ZIP, so that FILE! paths are resolved relative to
        ; %main.reb's location.  But for now, just do a proof of concept by
        ; showing execution of a main.reb if that is found in the encapping.

        emit [
            do (<*> code) except e -> [
                quit e.exit-code
            ]
        ]
        quit-when-done: default ['yes]
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
    ;     r3 --do "eval %script1.reb" --do "eval %script2.reb"
    ;
    any [
        file? o.script
        url? o.script
    ] then [
        emit [
            (do:args (<*> o.script) (<*> script-args)) except e -> [
                quit e.exit-code
            ]
        ]
    ]

    main-startup: ~<MAIN-STARTUP done>~  ; free function for GC

    if 'yes = quit-when-done [  ; can be null, YES? would complain...
        return <quit>  ; quits after instructions done
    ]

    emit #start-console

    return <start-console>
]

export [main-startup about usage license]
