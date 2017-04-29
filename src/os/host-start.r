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
    = Version:  system/version
    = Platform: system/platform
    = Build:    system/build
    = Commit:   system/commit
    -
    = Language: system/locale/language*
    = Locale:   system/locale/locale*
    = Home:     [to-local-file system/options/home]
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

    print trim/auto copy {
    Command line usage:

        REBOL |options| |script| |arguments|

    Standard options:

        --do expr        Evaluate expression (quoted)
        --help (-?)      Display this usage information
        --version tuple  Script must be this version or greater
        --               End of options

    Special options:

        --debug flags    For user scripts (system/options/debug)
        --halt (-h)      Leave console open when script is done
        --import file    Import a module prior to script
        --quiet (-q)     No startup banners or information
        --secure policy  Can be: none allow ask throw quit
        --trace (-t)     Enable trace mode during boot
        --verbose        Show detailed startup information

    Other quick options:

        -s               No security
        +s               Full security
        -v               Display version only (then quit)

    Examples:

        REBOL script.r
        REBOL -s script.r
        REBOL script.r 10:30 test@example.com
        REBOL --do "watch: on" script.r
    }
]

boot-help:
{Important notes:

  * Sandbox and security are not available.
  * Direct access to TCP HTTP required (no proxies).

Special functions:

  Help - show built-in help information}


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
    argv [block!]
        {Raw command line argument block received by main() as STRING!s}
    boot-exts [block! blank!]
        {Extensions (modules) loaded at boot}
    <with> host-prot
    <has>
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

    ; Set system/user home & rebol dirs if present
    ;
    ; User rebol directory (for local customisations)
    ;   * default (Linux, MacOS, Unix) - $HOME/.rebol
    ;   * Windows                      - $HOME/REBOL
    ;
    ; TBD - check perms are correct (SECURITY)
    ;
    if exists? home: dirize to-file get-env 'HOME [
        system/user/home: home
        rebol-dir: join-of home switch/default system/platform/1 [
            'Windows [%REBOL/]
        ][%.rebol/]              ;; default *nix (Linux, MacOS & UNIX)
        if exists? rebol-dir [system/user/rebol: rebol-dir]
    ]

    sys/script-pre-load-hook: :host-script-pre-load

    do-string: _ ;-- will be set if a string is given with --do

    quit-when-done: _ ;-- by default run CONSOLE

    o/home: what-dir ;-- save the current directory

    ; Process the option syntax out of the command line args in order to get
    ; the intended arguments.  TAKEs each option string as it goes so the
    ; array remainder can act as the args.

    unless tail? argv [ ;-- on most systems, argv[0] is the exe path
        o/boot: clean-path to-rebol-file take argv
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
            ["--cgi" | "-c"] end (
                o/quiet: true
                o/cgi: true
            )
        |
            "--debug" end (
                ;-- was coerced to BLOCK! before, but what did this do?
                ;
                take argv
                o/debug: to logic! argv/1
            )
        |
            "--do" end (
                o/quiet: true ;-- don't print banner, just run code string
                take argv
                do-string: argv/1
                quit-when-done: default true ;-- override blank, not false
            )
        |
            ["--halt" | "-h"] end (
                quit-when-done: false ;-- overrides true
            )
        |
            ["--help" | "-?"] end (
                usage
                quit-when-done: default true
            )
        |
            "--import" end (
                take argv
                lib/import to-rebol-file argv/1
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
            "--secure" end (
                take argv
                o/secure: to word! argv/1
                if secure != 'allow [
                    fail "SECURE is disabled (never finished for R3-Alpha)"
                ]
            )
        |
            "-s" end (
                o/secure: 'allow ;-- "secure-min"
            )
        |
            "+s" end (
                o/secure: 'quit ;-- "secure-max"
                fail "SECURE is disabled (never finished for R3-Alpha)"
            )
        |
            "--script" end (
                take argv
                o/script: argv/1
                quit-when-done: default true ;-- overrides blank, not false
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
                quit-when-done: default true
            )
        |
            "-w" end (
                ;-- No window; not currently applicable
            )
        |
            [["--" copy option to end] | ["-" copy option to end]] (
                fail ["Unknown command line option:" option]
            )
        ]

        if not is-option [break]

        take argv
    ]

    ; As long as there was no `--script` pased on the command line explicitly,
    ; the first item after the options is implicitly the script.
    ;
    if all [not o/script | not tail? argv] [
        o/script: to file! take argv
        quit-when-done: default true
    ]

    ; Whatever is left is the positional arguments, available to the script.
    ;
    o/args: argv ;-- whatever's left is positional args

    if all [
        not o/quiet
        o/verbose
    ][
        ; basic boot banner only
        ;
        boot-print unspaced [
            "REBOL 3.0 A" system/version/3 space system/build newline
        ]
    ]

    boot-embedded: get-encap system/options/boot

    if any [boot-embedded o/script] [o/quiet: true]

    ;-- Set up option/paths for /path, /boot, /home, and script path (for SECURE):
    o/path: dirize any [o/path o/home]

    ;-- !!! this was commented out.  Is it important?
    comment [
        if slash <> first o/boot [o/boot: clean-path o/boot]
    ]

    o/home: file: first split-path o/boot
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

    if o/verbose [print o]

    load-boot-exts boot-exts

    for-each [spec body] host-prot [module spec body]
    host-prot: 'done

    ;-- Setup SECURE configuration (a NO-OP for min boot)

comment [
    lib/secure (case [
        o/secure [
            o/secure
        ]
        file? o/script [
            compose [file throw (file) [allow read] (first script-path) allow]
        ]
    ] else [
        compose [file throw (file) [allow read] %. allow] ; default
    ])
]

    ;-- Evaluate rebol.r script:
    loud-print ["Checking for rebol.r file in" file]
    if exists? file/rebol.r [do file/rebol.r] ; bug#706

    ;boot-print ["Checking for user.r file in" file]
    ;if exists? file/user.r [do file/user.r]

    boot-print ""

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
                    fail "Could not find %main.reb in encapped zip file"
                ]
                code: load/header/type main 'unbound
            ]
        ] else [
            fail "Bad embedded boot data (not a BLOCK! or a BINARY!)"
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
    either file? o/script [
        trap/with [
            do/only o/script ;-- /ONLY so QUIT/WITH exit code bubbles out
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

    if any [
        o/verbose
        not any [o/quiet o/script]
    ][
        ;-- print fancy boot banner
        ;
        boot-print make-banner boot-banner
    ]

    boot-print boot-help


    ; CONSOLE skinning:
    ;
    ; Instantiate console! object into system/console
    ; This object can be updated from console!/skin-file 
    ;  - default is  %console-skin.reb
    ;  - if found in system/user/rebol
    ;
    ; See /os/host-console.r where this object is called from
    ;

    proto-skin: make console! [
        skin-file: if system/user/rebol [join-of system/user/rebol %console-skin.reb]
    ]

    if skin-file: proto-skin/skin-file [
        boot-print [newline "CONSOLE skinning:" newline]
        trap/with [
            ;; load & run skin if found
            if exists? skin-file [
                new-skin: do load skin-file

                ;; if loaded skin returns console! object then use as prototype
                if all [
                    object? new-skin
                    true? select new-skin 'repl    ;; quacks like a REPL so its a console!
                ][
                    proto-skin: make new-skin compose [
                        skin-file: (skin-file)
                        updated?: true
                    ]
                ]

                proto-skin/loaded?: true
            ]

            boot-print [
                space space
                either/only proto-skin/loaded? {Loaded skin} {Skin does not exist}
                "-" skin-file
                unspaced ["(" unless/only proto-skin/updated? {not } "updated CONSOLE)"]
            ]
        ] func [error] [
            boot-print [
                {  Error loading skin  -} skin-file | |
                error | |
                {  Fix error and restart CONSOLE}
            ]
        ]

        boot-print {}
    ]

    system/console: proto-skin


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
    repl: true      ;-- used to identify this as a console! object (quack!)
    skin-file: _
    loaded?:  false ;-- if true then this is a loaded (external) skin
    updated?: false ;-- if true then console! object found in loaded skin
    counter: 0
    last-result: _  ;-- last evaluated result (sent by HOST-CONSOLE)

    ; Called on every line of input by HOST-CONSOLE in %os/host-console.r
    ;
    cycle: does [
        if zero? ++ counter [print-greeting] ;-- only load skin on first cycle
        counter
    ]

    ;; APPEARANCE (can be overridden)

    prompt:   {>> }
    result:   {== }
    warning:  {!! }
    error:    {** }                ;; not used yet
    info:     to-string #{e29398}  ;; info sign!
    greeting: _
    print-prompt:   proc []  [print/only prompt]
    print-result:   proc []  [print unspaced [result last-result]]
    print-warning:  proc [s] [print unspaced [warning reduce s]]
    print-error:    proc [e] [print e]
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

    shortcuts: make object! [
        q: [quit]
        list-shortcuts: [print system/console/shortcuts]
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

