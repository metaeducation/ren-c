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
        Init_Core() routine more lightweight, it's possible to get the system
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


load-ext-module: function [
    "Loads an extension module from an extension object."
    ext [object!]
        "Extension object (from LOAD-EXTENSION, modified)"
][
    ; for ext obj: help system/standard/extensions
    ensure handle! ext/lib-base
    ensure binary! ext/lib-boot

    if word? set [hdr: code:] load-header/required ext/lib-boot [
        cause-error 'syntax hdr ext  ; word returned is error code
    ]
    ensure object! hdr
    ensure [block! blank!] hdr/options
    ensure [binary! block!] code

    loud-print ["Extension:" select hdr 'title]
    unless hdr/options [hdr/options: make block! 1]
    append hdr/options 'extension ; So make module! special cases it
    hdr/type: 'module             ; So load and do special case it
    ext/lib-boot: _            ; So it doesn't show up in the source
    tmp: body-of ext              ; Special extension words

    ; Define default extension initialization if needed:
    ; It is overridden when extension provides it's own COMMAND func.
    unless :ext/command [
        ;
        ; This is appending raw material to a block that will be used to
        ; make a MODULE!, so the function body will be bound first by the
        ; module and then by the FUNC.
        ;
        append tmp [
            cmd-index: 0
            command: func [
                "Define a new command for an extension."
                return: [function!]
                args [integer! block!]
            ][
                ; (contains module-local variables)
                make-command reduce [
                    ;
                    ; `self` isn't the self in effect for load-ext-module
                    ; (we're in the `sys` context, which doesn't have self).
                    ; It will be bound in the context of the module.
                    ;
                    args self also cmd-index (cmd-index: cmd-index + 1)
                ]
            ]
            protect/hide/words [cmd-index command]
        ]
    ]

    ; Convert the code to a block if not already:
    unless block? code [code: to block! code]

    ; Extension object fields and values must be first!
    insert code tmp

    reduce [hdr code] ; ready for make module!
]


load-boot-exts: function [
    "INIT: Load boot-based extensions."
    boot-exts [block! blank!]
][
    loud-print "Loading boot extensions..."

    ;loud-print ["boot-exts:" mold boot-exts]
    for-each [code impl error-base] boot-exts [
        code: load/header decompress code
        hdr: take code
        loud-print ["Found boot module" hdr/name]
        loud-print mold code
        tmp-ctx: make object!  [
            native: function [
                return: [function!]
                spec
                /export "this refinement is ignored here"
                /body
                code [block!]
                "Equivalent rebol code"
                <static> index (-1)
            ] compose [
                index: index + 1
                f: load-native/(all [body 'body]) spec (impl) index :code
                :f
            ]
        ]
        mod: make module! (length code) / 2
        set-meta mod hdr
        if errors: find code to set-word! 'errors [
            loud-print ["found errors in module" hdr/name]
            eo: construct make object! [
               code: error-base
               type: lowercase spaced [hdr/name "error"]
            ] second errors
            append system/catalog/errors reduce [to set-word! hdr/name eo]
            remove/part errors 2
        ]
        bind/only/set code mod
        bind hdr/exports mod
        bind code tmp-ctx
        if w: in mod 'words [protect/hide w]
        do code

        ; NOTE: This will error out if the code contains commands but
        ; no extension dispatcher (call) has been provided.
        if hdr/name [
            reduce/into [
                hdr/name mod either hdr/checksum [copy hdr/checksum][blank]
            ] system/modules
        ]

        case [
            not module? mod blank

            not block? select hdr 'exports blank

            empty? hdr/exports blank

            find hdr/options 'private [
                ; full export to user
                resolve/extend/only system/contexts/user mod hdr/exports
            ]

            'default [
                sys/export-words mod hdr/exports
            ]
        ]
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
        {If integer, host should exit with that status; else a REPL FUNCTION!}
    argv [block!]
        {Raw command line argument block received by main() as STRING!s}
    boot-embedded [binary! string! blank!]
        {Embedded user script inside this host instance (e.g. encapping)}
    boot-exts [block! blank!]
        {Extensions (modules) loaded at boot}
    <with> host-prot
    <static>
        o (system/options) ;-- shorthand since options are often read/written
][
    system/product: 'core

    sys/script-pre-load-hook: :host-script-pre-load

    do-string: _ ;-- will be set if a string is given with --do

    quit-when-done: _ ;-- by default run REPL

    o/home: what-dir ;-- save the current directory

    ; Process the option syntax out of the command line args in order to get
    ; the intended arguments.  TAKEs each option string as it goes so the
    ; array remainder can act as the args.

    unless tail? argv [ ;-- on most systems, argv[0] is the exe path
        o/boot: to file! take argv
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
            'else [insert first script-path o/path]   ; relative
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

;   lib/secure (case [
;       o/secure [
;           o/secure
;       ]
;       file? o/script [
;           compose [file throw (file) [allow read] (first script-path) allow]
;       ]
;       'else [
;           compose [file throw (file) [allow read] %. allow] ; default
;       ]
;   ])

    ;-- Evaluate rebol.r script:
    loud-print ["Checking for rebol.r file in" file]
    if exists? file/rebol.r [do file/rebol.r] ; bug#706

    ;boot-print ["Checking for user.r file in" file]
    ;if exists? file/user.r [do file/user.r]

    boot-print ""

    unless blank? boot-embedded [
        code: load/header/type boot-embedded 'unbound
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

    ;-- Evaluate script argument?
    either file? o/script [
        ; !!! Would be nice to use DO for this section. !!!
        ; NOTE: We can't use DO here because it calls the code it does with CATCH/quit
        ;   and we shouldn't catch QUIT in the top-level script, we should just quit.

        ; script-path holds: [dir file] for script
        ensure block! script-path
        ensure file! script-path/1
        ensure file! script-path/2

        ; /path dir is where our script gets started.
        change-dir first script-path
        if not exists? second script-path [
            cause-error 'access 'no-script o/script
        ]

        boot-print ["Evaluating:" o/script]
        trap/with [
            code: load/header/type second script-path 'unbound
        ] func [error <with> return] [
            print error
            return 1
        ]
        ; update system/script (Make into a function?)
        system/script: construct system/standard/script [
            title: select first code 'title
            header: first code
            parent: _
            path: what-dir
            args: script-args
        ]
        either 'module = select first code 'type [
            code: reduce [first+ code code]
            if object? tmp: sys/do-needs/no-user first code [
                append code tmp
            ]
            import do compose [module (code)]
        ][
            trap/with [
                sys/do-needs first+ code
                do lock intern code
            ] func [error <with> return] [
                print error
                return 1
            ]
        ]
    ]

    host-start: 'done

    if do-string [
        trap/with [
            do do-string
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

    ; Rather than have the host C code look up the REPL function by name, it
    ; is returned as a function value from calling the start.  It's a bit of
    ; a hack, and might be better with something like the SYS_FUNC table that
    ; lets the core call Rebol code.
    ;
    return :host-repl
]
