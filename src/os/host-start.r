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
host-prot: default _

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
; like an ordinary FUNCTION!, and all the proxying is handled for the user.
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
{Welcome to the Rebol console.  For more information please type in the commands below:

  ^[[1;33;49mHELP    ^[[0m- For starting information
  ^[[1;33;49mABOUT   ^[[0m- Information about your Rebol
  ^[[1;33;49mCHANGES ^[[0m- What's different about this version}

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
        (either is-module "Module:" "Script:") select hdr 'title
            "Version:" opt select hdr 'version
            "Date:" opt select hdr 'date
    ]
]


host-start: function [
    "Loads extras, handles args, security, scripts."
    return: [integer! function!]
        {If integer, host should exit with that status; else a CONSOLE FUNCTION!}
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

    ;
    ; helper functions
    ;
    die: func [
        {A gracefully way to FAIL during startup}
        reason [string!]
            {Error message}
        /error e [error!]
            {Error object, shown if --verbose option used}
        <with> return
    ][
        print "Startup encountered an error!"
        print ["**" reason]
        if error [
            print either o/verbose [e] ["!! use --verbose for more detail"]
        ]
        return 1
    ]

    to-dir: function [
        {Convert string path to absolute dir! path}
        return: [blank! file!]
            {Blank if not found}
        dir [string!]
    ][
        if empty? dir [return _]
        dir: clean-path/dir to-rebol-file dir
        all [exists? dir | dir]
    ]

    get-home-path: function [
        {Return HOME path (e.g. $HOME on *nix)}
        return: [blank! file!]
            {Blank if not found}
    ][
        home: attempt [
            any [
                get-env 'HOME
                ; join-of could fail because it doesn't accept blank
                attempt [join-of get-env 'HOMEDRIVE get-env 'HOMEPATH]
            ]
        ]

        if blank? home [return _]
        to-dir home
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
            o/boot: clean-path to-rebol-file take argv
        ]
        o/bin: first split-path o/boot
    ]

    param-or-die: func [
        {Take --option argv and then check if param arg is present, else die}
        switch-arg [string!] {Command-line option (switch) used}
    ][
        take argv
        any [
            first argv
            die join-all [switch-arg { parameter missing}]
        ]
    ]

    until [tail? argv] [

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
                o/quiet: true ;-- don't print banner, just run code string
                do-string: param-or-die "DO"
                quit-when-done: default [true] ;-- override blank, not false
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
                lib/import to-rebol-file param-or-die "IMPORT"
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
                ; significant combinations (e.g. used in make-make.r) will
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
                o/script: to-rebol-file param-or-die "SCRIPT"
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
                die join-all [
                    "Unknown command line option: " cli-option
                    newline
                    {!! For a full list of command-line options use:  --help}
                ]
            )
        ]

        if not is-option [break]

        take argv
    ]

    ; As long as there was no `--script` pased on the command line explicitly,
    ; the first item after the options is implicitly the script.
    ;
    if all [not o/script | not tail? argv] [
        o/script: to-rebol-file take argv
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


    load-boot-exts boot-exts

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

    ;-- Evaluate rebol.reb script:
    unless find o/suppress %rebol.reb [
        loud-print ["Checking for rebol.reb file in" o/bin]
        if exists? o/bin/rebol.reb [ ; bug#706 ??
            trap/with [
                do o/bin/rebol.reb
                append o/loaded o/bin/rebol.reb
                loud-print ["Finished evaluating script:" o/bin/rebol.reb]
            ] func [error] [
                die/error "Error found in rebol.reb script" error
            ]
        ]
    ]

    ;-- Evaluate user.reb script:
    if all [
        o/resources
        not find o/suppress %user.reb
    ][
        loud-print ["Checking for user.reb file in" o/resources]
        if exists? o/resources/user.reb [
            trap/with [
                ;
                ; ideally this would query permissions to make sure RESOURCES
                ; is owner writable only
                ;
                do o/resources/user.reb
                append o/loaded o/resources/user.reb
                loud-print ["Finished evaluating script:" o/resources/user.reb]
            ] func [error] [
                die/error "Error found in user.reb script" error
            ]
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
    if file? o/script [
        trap/with [
            do/only/args o/script script-args ;-- /ONLY so QUIT/WITH exit code bubbles out
        ] func [error <with> return] [
            print error
            return 1
        ]
    ]

    host-start: 'done

    ; Evaluate the DO string, e.g. `r3 --do "print {Hello}"`
    ;
    if do-string [
        trap/with [
            do/only do-string ;-- /ONLY so QUIT/WITH exit code bubbles out
        ] func [error <with> return] [
            print error
            return 1
        ]
    ]

    if quit-when-done [return 0]

    ; Start CONSOLE if got this far.
    ;
    ; Instantiate console! object into system/console for skinning.  This
    ; object can be updated %console-skin.reb if in system/options/resources
    ;
    ; See /os/host-console.r where this object is called from
    ;

    loud-print "Starting console..."
    loud-print ""
    proto-skin: make console! []
    skin-error: _

    if all [
        skin-file: %console-skin.reb
        not find o/suppress skin-file
        o/resources
        exists? skin-file: join-of o/resources skin-file
    ][
        trap/with [
            new-skin: do load skin-file

            ;; if loaded skin returns console! object then use as prototype
            if all [
                object? new-skin
                select new-skin 'repl ;; quacks like REPL, say it's a console!
            ][
                proto-skin: new-skin
                proto-skin/updated?: true
                proto-skin/name: any [proto-skin/name "updated"]
            ]

            proto-skin/loaded?: true
            proto-skin/name: any [proto-skin/name "loaded"]
            append o/loaded skin-file

        ] func [error] [
            skin-error: error       ;; show error later if --verbose
            proto-skin/name: "error"
        ]
    ]

    proto-skin/name: any [proto-skin/name | "default"]

    system/console: proto-skin

    ; Make the error hook store the error as the last one printed, so the
    ; WHY command can access it.  Also inform people of the existence of
    ; the WHY function on the first error delivery.
    ;
    proto-skin/print-error: adapt :proto-skin/print-error [
        unless system/state/last-error [
            system/console/print-info "Note: use WHY for error information"
        ]

        system/state/last-error: e
    ]

    ;
    ; banner time
    ;
    if o/about [
        ;-- print fancy boot banner
        ;
        boot-print make-banner boot-banner
    ] else [
        boot-print [
            "Rebol 3 (Ren/C branch)"
            mold compose [version: (system/version) build: (system/build)]
            newline
        ]
    ]

    boot-print boot-welcome

    ; verbose console skinning messages
    loud-print [newline {Console skinning:} newline]
    if skin-error [
        loud-print [
            {  Error loading console skin  -} skin-file | |
            skin-error | |
            {  Fix error and restart CONSOLE}
        ]
    ] else [
       loud-print [
            space space
            either proto-skin/loaded? {Loaded skin} {Skin does not exist}
            "-" skin-file
            spaced ["(CONSOLE" unless proto-skin/updated? {not} "updated)"]
        ]
    ]

    ; Just to get things started, tell the debugger extension to use the
    ; same console function that the top level uses.
    ;
    if find system/contexts/user 'init-debugger [
        system/contexts/user/init-debugger :host-console
    ]

    ; Rather than have the host C code look up the CONSOLE function by name, it
    ; is returned as a function value from calling the start.  It's a bit of
    ; a hack, and might be better with something like the SYS_FUNC table that
    ; lets the core call Rebol code.
    ;
    return :host-console
]


; Define console! object for skinning - stub for elsewhere?
;

console!: make object! [
    name: _
    repl: true      ;-- used to identify this as a console! object (quack!)
    loaded?:  false ;-- if true then this is a loaded (external) skin
    updated?: false ;-- if true then console! object found in loaded skin
    last-result: _  ;-- last evaluated result (sent by HOST-CONSOLE)

    ;; APPEARANCE (can be overridden)

    prompt:   {^[[1;31;49m>> ^[[0m}
    result:   {^[[1;37;49m== ^[[0m}
    warning:  {^[[1;35;49m!! ^[[0m}
    error:    {^[[1;31;49m** }                ;; not used yet
    info:     to-string #{e29398}  ;; info sign!
    greeting: _
    print-prompt:   proc []  [print/only prompt]
    print-result:   proc []  [print unspaced [result last-result]]
    print-warning:  proc [s] [print unspaced [warning reduce s]]
    print-error:    proc [e [error!]] [print e]
    print-info:     proc [s] [print [info space space reduce s]]
    print-greeting: proc []  [boot-print greeting]
    print-gap:      proc []  [print-newline]

    ;; BEHAVIOR (can be overridden)

    input-hook: func [
        {Receives line input, parse/transform, send back to CONSOLE eval}
        s
    ][
        s
    ]

    dialect-hook: func [
        {Receives code block, parse/transform, send back to CONSOLE eval}
        s
    ][
        s
    ]

    shortcuts: make object! compose/deep [
        d: [dump]
        h: [help]
        q: [quit]
        list-shortcuts: [print system/console/shortcuts]
        changes: [
            say-browser
            browse (join-all [
                https://github.com/metaeducation/ren-c/blob/master/CHANGES.md#
                join-all ["" system/version/1 system/version/2 system/version/3]
            ])
        ]
        topics: [
            say-browser
            browse https://r3n.github.io/topics/
        ]
    ]

    ;; HELPERS (could be overridden!)

    add-shortcut: proc [
        {Add/Change console shortcut}
        name  [any-word!]
            {shortcut name}
        block [block!]
            {command(s) expanded to}
    ][
        extend shortcuts name block
    ]
]
