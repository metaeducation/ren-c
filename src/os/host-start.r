REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Command line processing and startup code called by %host-main.c"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        Codebases using the Rebol interpreter can vary widely, and might not
        have command line arguments or user interface at all.

        This is a beginning attempt to factor out what used to be in
        R3-Alpha's %sys-start.r and executed by RL_Start().  By making the
        Startup_Core() routine more lightweight, it's possible to get the system
        up to a point where it's possible to use Rebol code to do things like
        command-line processing.

        Still more factoring should be possible, so that different executables
        (R3/Core, R3/View, Ren Garden) might reuse large parts of the
        initialization, if they need to do things in common.
    }
]

; These used to be loaded by the core, but prot-tls depends on crypt, thus it
; needs to be loaded after crypt. It was not an issue when crypt was builtin.
; But when it's converted to a module, and loaded by load-boot-exts, it breaks
; the dependency of prot-tls.
;
; Moving protocol loading from core to host fixes the problem.
;
; This should be initialized by make-host-init.r, but set a default just in
; case
host-prot: default [_]

boot-print: procedure [
    "Prints during boot when not quiet."
    data
    /eval
][
    eval_BOOT_PRINT: eval
    eval: :lib/eval

    unless system/options/quiet [
        print/(all [any [eval_BOOT_PRINT | semiquoted? 'data] 'eval]) :data
    ]
]

loud-print: procedure [
    "Prints during boot when verbose."
    data
    /eval
][
    eval_BOOT_PRINT: eval
    eval: :lib/eval

    if system/options/verbose [
        print/(all [any [eval_BOOT_PRINT | semiquoted? 'data] 'eval]) :data
    ]
]

make-banner: function [
    "Build startup banner."
    fmt [block!]
][
    str: make string! 200
    star: append/dup make string! 74 #"*" 74
    spc: format ["**" 70 "**"] ""
    parse fmt [
        some [
            [
                set a: string! (s: format ["**  " 68 "**"] a)
              | '= set a: [string! | word! | set-word!] [
                        b:
                          path! (b: get b/1)
                        | word! (b: get b/1)
                        | block! (b: spaced b/1)
                        | string! (b: b/1)
                    ]
                    (s: format ["**    " 11 55 "**"] reduce [a b])
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
    = Copyright: "2012-2017 Rebol Open Source Contributors"
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

about: procedure [
    "Information about REBOL"
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
usage: procedure [
    "Prints command-line arguments."
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
        --               End of options (not implemented)

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

boot-welcome:
{Welcome to Rebol.  For more information please type in the commands below:

  HELP    - For starting information
  ABOUT   - Information about your Rebol
  CHANGES - What's different about this version}

license: procedure [
    "Prints the REBOL/core license agreement."
][
    print system/license
]

load-boot-exts: function [
    "INIT: Load boot-based extensions."
    boot-exts [block! blank!]
][
    loud-print "Loading boot extensions..."

    ;loud-print ["boot-exts:" mold boot-exts]
    for-each [init quit] boot-exts [
        load-extension init
    ]

    boot-exts: 'done
    set 'load-boot-exts 'done ; only once
]

host-script-pre-load: procedure [
    {Code registered as a hook when a module or script are loaded}
    is-module [logic!]
    hdr [blank! object!]
        {Header object (will be blank for DO of BINARY! with no header)}
][
    ; Print out the script info
    boot-print [
        (is-module ?? "Module:" !! "Script:") select hdr 'title
            "Version:" opt select hdr 'version
            "Date:" opt select hdr 'date
    ]
]


host-start: function [
    "Called by HOST-CONSOLE.  Loads extras, handles args, security, scripts."

    return: [block! group!]
        {Instruction for C code to run in a sandbox (FAILs ok if GROUP!)}
    exec-path [file! blank!]
        {Path to the executable file}
    argv [block!]
        {Raw command line argument block received by main() as STRING!s}
    boot-exts [block! blank!]
        {Extensions (modules) loaded at boot}
    <with> host-prot
    <static>
        o (system/options) ;-- shorthand since options are often read/written
][
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

    ; The text codecs should also probably be extensions as well.  But the
    ; old Register_Codec() function was C code taking up space in %b-init.c
    ; so this at least allows that function to be deleted...the registration
    ; as an extension would also be done like this in user-mode.
    ;
    (sys/register-codec*
        'text
        %.txt
        :identify-text?
        :decode-text
        :encode-text)

    (sys/register-codec*
        'utf-16le
        %.txt
        :identify-utf16le?
        :decode-utf16le
        :encode-utf16le)

    (sys/register-codec*
        'utf-16be
        %.txt
        :identify-utf16be?
        :decode-utf16be
        :encode-utf16be)

    system/product: 'core

    ; !!! If we don't load the extensions early, then we won't get the GET-ENV
    ; function (it's provided by the Process extension).  Though optional,
    ; knowing where the home directory is, is needed for running startup
    ; scripts.  This should be rethought because it may be that extensions
    ; can be influenced by command line parameters as well.
    ;
    load-boot-exts boot-exts

    ; !!! The debugger is a work in progress.  But the design attempts to make
    ; it an optional extension which doesn't need to be built into the EXE,
    ; and can be loaded dynamically into any Rebol-based binary.  But it has
    ; to spawn a console, and since the console is userspace and may vary
    ; between EXEs...it has to be told where that function is.
    ;
    if find system/contexts/user 'init-debugger [
        system/contexts/user/init-debugger :host-console
    ]

    ; helper functions
    ;
    die: func [
        {A graceful way to "FAIL" during startup}
        reason [string! block!]
            {Error message}
        /error e [error!]
            {Error object, shown if --verbose option used}
        <with> return
    ][
        print "Startup encountered an error!"
        print ["**" block? reason then [spaced reason] else [reason]]
        if error [
            print either o/verbose [e] ["!! use --verbose for more detail"]
        ]
        return [quit/with 1]
    ]

    to-dir: function [
        {Convert string path to absolute dir! path}
        return: [blank! file!]
            {Blank if not found}
        dir [string!]
    ][
        if empty? dir [return _]
        dir: clean-path/dir local-to-file dir
        all [exists? dir | dir]
    ]

    get-home-path: function [
        {Return HOME path (e.g. $HOME on *nix)}
        return: [blank! file!]
            {Blank if not found}
    ][
        unless get-env: attempt [:system/modules/Process/get-env] [
            loud-print [
                "Interpreter not built with GET-ENV, can't detect HOME dir"
                    |
                "(Build with Process extension enabled to address this)"
            ]
            return blank
        ]

        any [
            get-env 'HOME

            all [
                homedrive: get-env 'HOMEDRIVE
                homepath: get-env 'HOMEPATH
                join-of homedrive homepath
            ]
        ] then home -> [
            to-dir home
        ] else [
            blank
        ]
    ]

    get-resources-path: function [
        {Return platform specific resources path.}
        return: [blank! file!]
            {Blank if not found}
    ][
        ;; lives under systems/options/home

        path: join-of o/home switch/default system/platform/1 [
            'Windows [%REBOL/]
        ][
            %.rebol/     ;; default *nix (covers Linux, MacOS (OS X) and Unix)
        ]

        all [exists? path | path]
    ]

    ; Set system/users/home (users HOME directory)
    ; Set system/options/home (ditto)
    ; Set system/options/resources (users Rebol resource directory)
    ; NB. Above can be overridden by --home option
    ; TBD - check perms are correct (SECURITY)
    all [
        home-dir: get-home-path             ;; _ if doesn't exist
        system/user/home: o/home: home-dir
        resources-dir: get-resources-path   ;; _ if doesn't exist
        o/resources: resources-dir
    ]

    sys/script-pre-load-hook: :host-script-pre-load

    do-string: _ ;-- will be set if a string is given with --do

    quit-when-done: _ ;-- by default run CONSOLE

    ; Process the option syntax out of the command line args in order to get
    ; the intended arguments.  TAKEs each option string as it goes so the
    ; array remainder can act as the args.

    either tail? argv [
        if file? exec-path [
            o/boot: exec-path
            o/bin: first split-path o/boot
        ]
    ][
        either file? exec-path [
            o/boot: exec-path
            take argv ;consume argv[0] anyway
        ][ ;-- on most systems, argv[0] is the exe path
            o/boot: clean-path local-to-file take argv
        ]
        o/bin: first split-path o/boot
    ]

    param-or-die: func [
        {Take --option argv and then check if param arg is present, else die}
        option [string!] {Command-line option (switch) used}
    ][
        take argv
        return first argv or [die [option {parameter missing}]]
    ]

    ; As we process command line arguments, we build up an "instruction" block
    ; which is going to be passed back.  This way you can have multiple
    ; --do "..." or script arguments, and they will be run in a sequence.
    ;
    ; The instruction block is run in a sandbox which prevents cancellation
    ; or failure from crashing the interpreter.  (HOST-START is not allowed
    ; to cancel or fail--it is an implementation helper called from
    ; HOST-CONSOLE, which is special.  See notes in HOST-CONSOLE.)
    ;
    ; The directives at the start of the instruction dictate that Ctrl-C
    ; during the startup instruction will exit with code 130, and any errors
    ; that arise will be reported and result in exit code 1.
    ;
    instruction: copy [
        [#quit-if-halt #countdown-if-error]
            |
    ]

    while-not [tail? argv] [

        is-option: parse/case argv/1 [

            ["--" end] (
                ; Double-dash means end of command line arguments, and the
                ; rest of the arguments are going to be positional.  In
                ; Rebol's case, that means a file to run and its arguments
                ; (if anything following).
                ;
                ; Make the is-option rule fail, but take the "--" away so
                ; it isn't treated as the name of a script to run!
                ;
                take argv
            ) fail
        |
            "--about" end (
                o/about: true   ;; show full banner (ABOUT) on startup
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
                ;-- was coerced to BLOCK! before, but what did this do?
                ;
                o/debug: to logic! param-or-die "DEBUG"
            )
        |
            "--do" end (
                ;
                ; A string of code to run, e.g. `r3 --do "print {Hello}"`
                ;
                o/quiet: true ;-- don't print banner, just run code string
                quit-when-done: default [true] ;-- override blank, not false
                append instruction compose/only [
                    ;
                    ; Use /ONLY so that QUIT/WITH quits, vs. return DO value
                    ;
                    do/only (param-or-die "DO")
                        |
                    ; Use expression barrier for insulation
                ]

            )
        |
            ["--halt" | "-h"] end (
                quit-when-done: false ;-- overrides true
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
                if resource-dir: to-dir param-or-die "RESOURCES" [
                    ;; dir exists so will override earlier automated settings
                    o/resources: resource-dir
                ]
                else [die "RESOURCES directory not found"]
            )
        |
            "--suppress" end (
                param: param-or-die "SUPPRESS"
                o/suppress: if param == "*" [
                    ;; suppress all known start-up files
                    [%rebol.reb %user.reb %console-skin.reb]
                ] else [
                    to-block param
                ]
            )
        |
            "--secure" end (
                o/secure: to word! param-or-die "SECURE"
                if o/secure != 'allow [
                    die "SECURE is disabled (never finished for R3-Alpha)"
                ]
            )
        |
            "-s" end (
                o/secure: 'allow ;-- "secure-min"
            )
        |
            "+s" end (
                o/secure: 'quit ;-- "secure-max"
                die "SECURE is disabled (never finished for R3-Alpha)"
            )
        |
            "--script" end (
                o/script: local-to-file param-or-die "SCRIPT"
                quit-when-done: default [true] ;-- overrides blank, not false
            )
        |
            ["-t" | "--trace"] end (
                trace on ;-- did they mean trace just the script/DO code?
            )
        |
            "--verbose" end (
                o/verbose: true
            )
        |
            ["-v" | "-V" | "--version"] end (
                boot-print ["Rebol 3" system/version] ;-- version tuple
                quit-when-done: default [true]
            )
        |
            "-w" end (
                ;-- No window; not currently applicable
            )
        |
            [copy cli-option: [["--" | "-" | "+"] to end ]] (
                die [
                    "Unknown command line option:" cli-option
                        |
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
    trap [c-debug-break-at/compensate 1000] ;-- fails in release build

    ; As long as there was no `--script` pased on the command line explicitly,
    ; the first item after the options is implicitly the script.
    ;
    if not o/script and (not tail? argv) [
        o/script: local-to-file take argv
        quit-when-done: default [true]
    ]

    ; Whatever is left is the positional arguments, available to the script.
    ;
    o/args: argv ;-- whatever's left is positional args


    boot-embedded: get-encap system/options/boot

    if any [boot-embedded o/script] [o/quiet: true]

    ;-- Set option/paths for /path, /boot, /home, and script path (for SECURE)
    o/path: what-dir  ;dirize any [o/path o/home]

    ;-- !!! this was commented out.  Is it important?
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

    ;-- Convert command line arg strings as needed:
    script-args: o/args ; save for below

    ; version, import, secure are all of valid type or blank


    for-each [spec body] host-prot [module spec body]
    host-prot: 'done

    ;-- Setup SECURE configuration (a NO-OP for min boot)
    ;; Note: After refactoring `file` was removed from above.
    ;;       file (below) -> o/bin (would have been same)

comment [
    lib/secure case [
        o/secure [
            o/secure
        ]
        file? o/script [
            compose [file throw (file) [allow read] (first script-path) allow]
        ]
    ] else [
        compose [file throw (file) [allow read] %. allow] ; default
    ]
]

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
        trap/with [
            do o/bin/rebol.reb
            append o/loaded o/bin/rebol.reb
            loud-print ["Finished evaluating script:" o/bin/rebol.reb]
        ] func [error] [
            die/error "Error found in rebol.reb script" error
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
        trap/with [
            do o/resources/user.reb
            append o/loaded o/resources/user.reb
            loud-print ["Finished evaluating script:" o/resources/user.reb]
        ] func [error] [
            die/error "Error found in user.reb script" error
        ]
    ]

    unless blank? boot-embedded [
        case [
            binary? boot-embedded [ ; single script
                code: load/header/type boot-embedded 'unbound
            ]
            block? boot-embedded [
                ;
                ; The encapping is an embedded zip archive.  get-encap did
                ; the unzipping into a block, and this information must be
                ; made available somehow.  It shouldn't be part of the "core"
                ; but just responsibility of the host that supports encap
                ; based loading.
                ;
                o/encap: boot-embedded

                main: select boot-embedded %main.reb
                unless binary? main [
                    die "Could not find %main.reb in encapped zip file"
                ]
                code: load/header/type main 'unbound
            ]
        ] else [
            die "Bad embedded boot data (not a BLOCK! or a BINARY!)"
        ]

        ;boot-print ["executing embedded script:" mold code]
        system/script: construct system/standard/script [
            title: select first code 'title
            header: first code
            parent: _
            path: what-dir
            args: script-args
        ]
        either 'module = select first code 'type [
            code: reduce [first+ code code]
            if object? tmp: sys/do-needs/no-user first code [append code tmp]
            import do compose [module (code)]
        ][
            sys/do-needs first+ code
            do intern code
        ]
        quit ;ignore user script and "--do" argument
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
        append instruction compose/deep/only [
            ;
            ; Use DO/ONLY so QUIT/WITH exits vs. being DO's return value
            ;
            do/only/args (o/script) (script-args)
        ]
    ]

    host-start: 'done

    either quit-when-done [
        append instruction [quit/with 0]
    ][
        append instruction [
            start-console
                |
            <needs-prompt>
        ]
    ]

    return instruction
]
