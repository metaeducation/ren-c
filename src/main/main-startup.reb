REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Command line processing and startup code called by %main.c"
    File: %main-startup.r
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

; These used to be loaded by the core, but prot-tls depends on crypt, thus it
; needs to be loaded after crypt. It was not an issue when crypt was builtin.
; But when it's converted to a module, it breaks the dependency of prot-tls.
;
; Moving protocol loading from core to host fixes the problem.
;
; Should be initialized by make-host-init.r, but set a default just in case.
;
host-prot: default [_]

boot-print: redescribe [
    "Prints during boot when not quiet."
](
    enclose 'print func [f] [if not system/options/quiet [do f]]
)

loud-print: redescribe [
    "Prints during boot when verbose."
](
    enclose 'print func [f] [if system/options/verbose [do f]]
)

make-banner: function [
    "Build startup banner."
    fmt [block!]
][
    str: make text! 200
    star: append/dup make text! 74 #"*" 74
    spc: format ["**" 70 "**"] ""
    parse fmt [
        some [
            [
                set a: text! (s: format ["**  " 68 "**"] a)
              | '= set a: [text! | word! | set-word!] [
                        b:
                          path! (b: get b/1)
                        | word! (b: get b/1)
                        | block! (b: spaced b/1)
                        | text! (b: b/1)
                    ]
                    (s: format ["**    " 11 55 "**"] reduce [a b])
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
    = Copyright: "2012-2017 Ren-C Open Source Contributors"
    = "" "Apache 2.0 License, see LICENSE."
    = Website:  "http://github.com/metaeducation/ren-c"
    -
    = Version:   system/version
    = Platform:  system/platform
    = Build:     system/build
    = Commit:    system/commit
    -
    = Language:  system/locale/language*
    = Locale:    system/locale/locale*
    = Home:      system/options/home
    = Resources: system/options/resources
    = Console:   system/console/name
    -
    *
]

about: function [
    "Information about REBOL"
    return: <void>
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
usage: function [
    "Prints command-line arguments."
    return: <void>
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
        --script file    Implicitly provide script to run
        --version (-v)   Display version only (then quit)
        --               End of options (treat remainder as script args)

    Special options:

        --about          Prints full banner of information when console starts
        --debug flags    For user scripts (system/options/debug)
        --halt (-h)      Leave console open when script is done
        --import file    Import a module prior to script
        --quiet (-q)     No startup banners or information
        --resources dir  Manually set where Rebol resources directory lives
        --secure policy  Can be: none allow ask throw quit
        --suppress ""    Suppress any found start-up scripts  Use "*" to suppress all.
        --trace (-t)     Enable trace mode during boot
        --verbose        Show detailed startup information

    Other quick options:

        -s               No security
        +s               Full security
        -qs              Quiet and secure (combining other switches not implemented yet)

    Examples:

        REBOL script.reb
        REBOL -s script.reb
        REBOL script.reb 10:30 test@example.com
        REBOL --do "print 1 + 1"
        #!/sbin/REBOL -cs

    Console (no script/arguments or Standard option used):

        REBOL
        REBOL -q --about --suppress "%rebol.reb %user.reb"
    }
]

license: function [
    "Prints the REBOL/core license agreement."
    return: <void>
][
    print system/license
]

host-script-pre-load: function [
    {Code registered as a hook when a module or script are loaded}
    return: <void>
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
; We could use them via `lib/<whatever>`, but then each callsite would have to
; document the issue.  So we make them SET-WORD!s added to lib up front, so
; the lib modification gets picked up.
;
get-current-exec:
file-to-local:
local-to-file:
what-dir:
change-dir: ~undefined~


main-startup: function [
    "Usermode command-line processing: handles args, security, scripts"

    argv {Raw command line argument block received by main() as STRING!s}
        [block!]
    <with>
    main-startup host-prot  ; unset when finished with them
    about usage license  ; exported to lib, see notes
    <static>
        o (system/options)  ; shorthand since options are often read/written
][
    ; !!! The whole host startup/console is currently very manually loaded
    ; into its own isolated context by the C startup code.  This way, changes
    ; to functions the console loop depends on (like PRINT or INPUT) that the
    ; user makes will not break the console's functionality.  It would be
    ; better if it used the module system, but since it doesn't, it does not
    ; have a place to put "exports" to lib or user.  We'd like people to be
    ; able to access the ABOUT, WHY, and USAGE functions... so export them
    ; here to LIB.  Again--this should be done by making this a module!
    ;
    ensure action! :license
    ensure action! :about
    ensure action! :usage
    sys/export [about usage license]

    ; We hook the RETURN function so that it actually returns an instruction
    ; that the code can build up from multiple EMIT statements.

    instruction: copy []

    emit: function [
        {Builds up sandboxed code to submit to C, hooked RETURN will finalize}

        item "ISSUE! directive, TEXT! comment, (<*> composed) code BLOCK!"
            [block! issue! text!]
        <with> instruction
    ][
        switch type of item [
            issue! [
                if not empty? instruction [append/line instruction ',]
                insert instruction item
            ]
            text! [
                append/line instruction compose [comment (item)]
            ]
            block! [
                if not empty? instruction [append/line instruction ',]
                append/line instruction compose/deep <*> item
            ]
            unreachable
        ]
    ]

    return: function [
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
                emit [system/console/print-gap]
                emit [system/console/print-prompt]
                emit [reduce [
                    system/console/input-hook
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
            default [
                emit [fail [{Bad console instruction:} (<*> mold state)]]
            ]
        ]
    ]

    ; The core presumes no built-in I/O ability in the release build, hence
    ; during boot PANIC and PANIC-VALUE can only do printf() in the debug
    ; build.  While there's no way to hook the core panic() or panic_at()
    ; calls, the rebPanic() API dispatches to PANIC and PANIC-VALUE.  Hook
    ; them just to show we can...use I/O to print a message.
    ;
    hijack 'panic adapt (copy :panic) [
        print "PANIC ACTION! called (explicitly or by rebPanic() API)"
        ;
        ; ...adaptation falls through to our copy of the original PANIC
    ]
    hijack 'panic-value adapt (copy :panic-value) [
        print "PANIC-VALUE ACTION! called (explicitly or by rebPanic() API)"
        ;
        ; ...adaptation falls through to our copy of the original PANIC-VALUE
    ]

    ; can only output do not assume they have any ability to write out
    ; information to the user, because the

    ; Currently there is just one monolithic "initialize all schemes", e.g.
    ; FILE:// and HTTP:// and CONSOLE:// -- this will need to be broken down
    ; into finer granularity.  Formerly all of them were loaded at the end
    ; of Startup_Core(), but one small step is to push the decision into the
    ; host...which loads them all, but should be more selective.
    ;
    sys/init-schemes

    system/product: 'core

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
    switch type of system/options/boot: get-current-exec [
        file! []  ; found it
        null []  ; also okay (not foolproof!)
        fail
    ]

    === HELPER FUNCTIONS ===

    die: func [
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
            print either o/verbose [
                [error]
            ][
                "!! use --verbose for more detail"
            ]
        ]
        return <die>
    ]

    to-dir: function [
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

    get-home-path: function [
        {Return HOME path (e.g. $HOME on *nix)}
        return: [<opt> file!]
    ][
        get-env: attempt [:system/modules/Process/get-env] else [
            loud-print [
                "Interpreter not built with GET-ENV, can't detect HOME dir" LF
                "(Build with Process extension enabled to address this)"
            ]
            return null
        ]

        return to-dir try any [
            get-env 'HOME
            all [
                homedrive: get-env 'HOMEDRIVE
                homepath: get-env 'HOMEPATH
                join homedrive homepath
            ]
        ]
    ]

    get-resources-path: function [
        {Return platform specific resources path.}
        return: [<opt> file!]
    ][
        ; lives under systems/options/home

        path: join o/home switch system/platform/1 [
            'Windows [%REBOL/]
        ] else [
            %.rebol/  ; default *nix (covers Linux, MacOS (OS X) and Unix)
        ]

        return if exists? path [path]
    ]

    ; Set system/users/home (users HOME directory)
    ; Set system/options/home (ditto)
    ; Set system/options/resources (users Rebol resource directory)
    ; NB. Above can be overridden by --home option
    ; TBD - check perms are correct (SECURITY)
    all [
        home-dir: try get-home-path
        system/user/home: o/home: home-dir
        resources-dir: try get-resources-path
        o/resources: resources-dir
    ]

    sys/script-pre-load-hook: :host-script-pre-load

    do-string: _  ; will be set if a string is given with --do

    quit-when-done: _  ; by default run CONSOLE

    ; Process the option syntax out of the command line args in order to get
    ; the intended arguments.  TAKEs each option string as it goes so the
    ; array remainder can act as the args.

    ; The host executable may have initialized system/options/boot, using
    ; a platform-specific method, since argv[0] is *not* always exe path:
    ;
    ; https://stackoverflow.com/q/1023306/
    ; http://stackoverflow.com/a/933996/211160
    ;
    ; If it did not initialize it, fall back on argv[0], if available.
    ;
    if not tail? argv [
        o/boot: default [clean-path local-to-file first argv]
        take argv
    ]
    if o/boot [
        o/bin: first split-path o/boot
    ]

    param-or-die: func [
        {Take --option argv and then check if param arg is present, else die}
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

    is-script-implicit: true

    while [not tail? argv] [

        is-option: did parse/case argv/1 [

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
                o/about: true  ; show full banner (ABOUT) on startup
            )
        |
            "--breakpoint" end (
                c-debug-break-at to-integer param-or-die "BREAKPOINT"
            )
        |
            ["--cgi" | "-c"] end (
                o/quiet: true
                o/cgi: true
            )
        |
            "--debug" end (
                ; was coerced to BLOCK! before, but what did this do?
                ;
                o/debug: to logic! param-or-die "DEBUG"
            )
        |
            "--do" end (
                ;
                ; A string of code to run, e.g. `r3 --do "print {Hello}"`
                ;
                o/quiet: true  ; don't print banner, just run code string
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
                lib/import local-to-file param-or-die "IMPORT"
            )
        |
            ["--quiet" | "-q"] end (
                o/quiet: true
            )
        |
            "-qs" end (
                ; !!! historically you could combine switches when used with
                ; a single dash, but this feature should be part of a better
                ; thought out implementation.  For now, any historically
                ; significant combinations (e.g. used in make.r) will
                ; be supported manually.  This is "quiet unsecure"
                ;
                o/quiet: true
                o/secure: 'allow
            )
        |
            "-cs" end (
                ; every tutorial on Rebol CGI shows these flags.
                o/secure: 'allow
                o/quiet: true
                o/cgi: true
            )
        |
            "--resources" end (
                o/resources: (to-dir param-or-die "RESOURCES") else [
                    die "RESOURCES directory not found"
                ]
            )
        |
            "--suppress" end (
                param: param-or-die "SUPPRESS"
                o/suppress: if param = "*" [
                    ; suppress all known start-up files
                    [%rebol.reb %user.reb %console-skin.reb]
                ] else [
                    make block! param
                ]
            )
        |
            "--secure" end (
                o/secure: to word! param-or-die "SECURE"
                if o/secure <> 'allow [
                    die "SECURE is disabled (never finished for R3-Alpha)"
                ]
            )
        |
            "-s" end (
                o/secure: 'allow  ; "secure-min"
            )
        |
            "+s" end (
                o/secure: 'quit  ; "secure-max"
                die "SECURE is disabled (never finished for R3-Alpha)"
            )
        |
            "--script" end (
                o/script: local-to-file param-or-die "SCRIPT"
                quit-when-done: default [true]  ; overrides blank, not false

                is-script-implicit: false  ; not the first post-option arg
            )
        |
            ["-t" | "--trace"] end (
                trace on  ; did they mean trace just the script/DO code?
            )
        |
            "--verbose" end (
                o/verbose: true
            )
        |
            ["-v" | "-V" | "--version"] end (
                boot-print ["Rebol 3" system/version]  ; version tuple
                quit-when-done: default [true]
            )
        |
            "-w" end (
                ; No window; not currently applicable
            )
        |
            [copy cli-option: [["--" | "-" | "+"] to end ]] (
                die [
                    "Unknown command line option:" cli-option LF
                    {!! For a full list of command-line options use: --help}
                ]
            )
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
        o/script: local-to-file take argv
        quit-when-done: default [true]
    ]

    ; Whatever is left is the positional arguments, available to the script.
    ;
    o/args: argv  ; whatever's left is positional args


    boot-embedded: get-encap system/options/boot

    if any [boot-embedded o/script] [o/quiet: true]

    ; Set option/paths for /path, /boot, /home, and script path (for SECURE)
    o/path: what-dir  ;dirize any [o/path o/home]

    ; !!! this was commented out.  Is it important?
    comment [
        if slash <> first o/boot [o/boot: clean-path o/boot]
    ]

    if file? o/script [ ; Get the path (needed for SECURE setup)
        script-path: split-path o/script
        case [
            slash = first first script-path []      ; absolute
            %./ = first script-path [script-path/1: o/path]   ; curr dir
        ] else [
            insert first script-path o/path ; relative
        ]
    ]

    ; Convert command line arg strings as needed:
    script-args: o/args ; save for below

    ; version, import, secure are all of valid type or blank


    for-each [spec body] host-prot [module spec body]
    host-prot: 'done

    ;
    ; start-up scripts, o/loaded tracks which ones are loaded (with full path)
    ;

    ; Evaluate rebol.reb script:
    ; !!! see https://github.com/rebol/rebol-issues/issues/706
    ;
    all [
        not find o/suppress %rebol.reb
        elide (loud-print ["Checking for rebol.reb file in" o/bin])
        exists? o/bin/rebol.reb
    ] then [
        trap [
            do o/bin/rebol.reb
            append o/loaded o/bin/rebol.reb
            loud-print ["Finished evaluating script:" o/bin/rebol.reb]
        ] then e -> [
            die/error "Error found in rebol.reb script" e
        ]
    ]

    ; Evaluate user.reb script:
    ; !!! Should it query permissions to ensure RESOURCES is owner writable?
    ;
    all [
        o/resources
        not find o/suppress %user.reb
        elide (loud-print ["Checking for user.reb file in" o/resources])
        exists? o/resources/user.reb
    ] then [
        trap [
            do o/resources/user.reb
            append o/loaded o/resources/user.reb
            loud-print ["Finished evaluating script:" o/resources/user.reb]
        ] then e -> [
            die/error "Error found in user.reb script" e
        ]
    ]

    switch type of boot-embedded [
        blank! [
            false  ; signal that there's no embedded code
        ]
        binary! [ ; single script
            [code header]: load/type boot-embedded 'unbound
            true
        ]
        block! [
            ;
            ; The encapping is an embedded zip archive.  get-encap did
            ; the unzipping into a block, and this information must be
            ; made available somehow.  It shouldn't be part of the "core"
            ; but just responsibility of the host that supports encap
            ; based loading.
            ;
            o/encap: boot-embedded

            main: select boot-embedded %main.reb
            if not binary? main [
                die "Could not find %main.reb in encapped zip file"
            ]
            [code header]: load/type main 'unbound
            true
        ]

        die "Bad embedded boot data (not a BLOCK! or a BINARY!)"
    ] then execute -> [
        if execute [
            ;boot-print ["executing embedded script:" mold code]
            system/script: make system/standard/script [
                title: select first code 'title
                header: first code
                parent: _
                path: what-dir
                args: script-args
            ]
            if 'module = select first code 'type [
                code: reduce [first code, next code]
                if object? tmp: sys/do-needs/no-user first code [
                    append code tmp
                ]
                import do compose [module (code)]
            ] else [
                sys/do-needs first code
                do intern next code
            ]
            quit ;ignore user script and "--do" argument
        ]
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
    if file? o/script [
        emit {Use DO/ONLY so QUIT/WITH exits vs. being DO's return value}
        emit [do/only/args (<*> o/script) (<*> script-args)]
    ]

    main-startup: 'done

    if quit-when-done [
        emit [quit 0]
        return <unreachable>
    ]

    emit #start-console

    return <start-console>
]
