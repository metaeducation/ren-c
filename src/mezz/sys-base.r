REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Top Context Functions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Context: sys
    Note: {
        Follows the BASE lib init that provides a basic set of functions
        to be able to evaluate this code.

        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!
    }
]

; It is desirable to express the logic of PRINT as user code, but it is
; also desirable to use PRINT from the C code.  This should likely be
; optimized as a native, but is easier to explore at the moment like this.
;
print*: :print


;-- If the host wants to know if a script or module is loaded, e.g. to
;   print out a message.  (Printing directly from this code would be
;   presumptuous.)
;
script-pre-load-hook: _


; DO of functions, blocks, paths, and other do-able types is done directly by
; C code in REBNATIVE(do).  But that code delegates to this Rebol function
; for ANY-STRING! and BINARY! types (presumably because it would be laborious
; to express as C).
;
do*: function [
    {SYS: Called by system for DO on datatypes that require special handling.}
    return: [<opt> any-value!]
    source [file! url! string! binary! tag!]
        {Files, urls and modules evaluate as scripts, other strings don't.}
    args [logic!]
        "Positional workaround of /ARGS"
    arg [any-value!]
        "Args passed as system/script/args to a script (normally a string)"
    next [logic!]
        "Positional workaround of /NEXT"
    var [blank! word!]
        "If do next expression only, variable updated with new block position"
    only [logic!]
        "Do not catch quits...propagate them."
][
    next_DO*: next
    next: :lib/next

    ; !!! These were refinements on the original DO* which were called from
    ; the system using positional order.  Under the Ren-C model you cannot
    ; select refinements positionally, nor can you pass "void" cells in
    ; a variadic invocation (because variadics may be reified to blocks which
    ; are user-exposed, and arrays with voids in them are only allowed for
    ; cases like the internal varlist of objects).
    ;
    ; It would be *possible* to keep these going as refinements and have the
    ; system build a path to make a call, but this is easier.
    ;
    if not args [unset 'args]
    if not next_DO* [unset 'var]

    ; !!! DEMONSTRATION OF CONCEPT... this translates a tag into a URL!, but
    ; it should be using a more "official" URL instead of on individuals
    ; websites.  There should also be some kind of local caching facility.
    ;
    if tag? source [
        if source = <r3-legacy> [
            ; Special compatibility tag... Rebol2 and R3-Alpha will ignore the
            ; DO of a <tag>, so this is a no-op in them.
            ;
            return r3-legacy* ;-- calls function defined in %mezz-legacy.r
        ]

        ; Convert value into a URL!
        source: switch source [
            ; Encodings and data formats
            <json> [https://raw.githubusercontent.com/rgchris/Scripts/master/ren-c/altjson.reb]
            <xml> [http://reb4.me/r3/altxml.reb]

            ; Web services
            <amazon-s3> [http://reb4.me/r3/s3.reb]
            <httpd> [https://raw.githubusercontent.com/rgchris/Scripts/master/ren-c/httpd.reb]
            <twitter> [https://raw.githubusercontent.com/gchiu/rebolbot/master/twitter.r3]
            <trello> [http://codeconscious.com/rebol-scripts/trello.r]
            <upgrade> [https://raw.githubusercontent.com/gchiu/rebol-misc/master/upgrade.reb]

            ; Dialects
            <rebmu> [https://raw.githubusercontent.com/hostilefork/rebmu/master/rebmu.reb]
        ] else [
            fail [
                {Module} source {not in "rebol.org index" (hardcoded for now)}
            ]
        ]
    ]

    ; Note that DO of file path evaluates in the directory of the target file.
    ;
    original-path: what-dir
    original-script: _

    finalizer: func [
        value [<opt> any-value!]
        name [any-value!] ;-- can be a FUNCTION!
        <with> return
    ][
        ; Restore system/script and the dir if they were changed

        if original-script [system/script: original-script]
        if original-path [change-dir original-path]

        either :name = :quit [
            if only [
                quit/with :value ;-- "rethrow" the QUIT if DO/ONLY
            ]
        ][
            assert [:name = blank]
        ]

        return :value ;-- returns from DO* not FINALIZER, due to <with> return
    ]

    ; If a file is being mentioned as a DO location and the "current path"
    ; is a URL!, then adjust the source to be a URL! based from that path.
    ;
    if all [url? original-path | file? source] [
         source: join-of original-path source
    ]

    ; Load the code (do this before CHANGE-DIR so if there's an error in the
    ; LOAD it will trigger before the failure of changing the working dir)
    ; It is loaded as UNBOUND so that DO-NEEDS runs before INTERN.
    ;
    code: ensure block! (load/header/type source 'unbound)

    ; LOAD/header returns a block with the header object in the first
    ; position, or will cause an error.  No exceptions, not even for
    ; directories or media.  "Load of URL has no special block forms." <-- ???
    ;
    ; !!! Should the header always be locked by LOAD?
    ;
    hdr: lock to-value ensure [object! blank!] first code
    is-module: 'module = select hdr 'type
    code: next code

    either all [string? source | not is-module] [
        ;
        ; Return result without "script overhead" (e.g. don't change the
        ; working directory to the base of the file path supplied)
        ;
        do-needs hdr  ; Load the script requirements
        intern code   ; Bind the user script
        result: catch/quit/with [
            ;
            ; The source string may have been mutable or immutable, but the
            ; loaded code is not locked for this case.  So this works:
            ;
            ;     do "append {abc} {de}"
            ;
            do/next code :var ;-- If var is void, /NEXT is revoked
        ] :finalizer
    ][
        ; Otherwise we are in script mode.  When we run a script, the
        ; "current" directory is changed to the directory of that script.
        ; This way, relative path lookups to find dependent files will look
        ; relative to the script.
        ;
        ; We want this behavior for both FILE! and for URL!, which means
        ; that the "current" path may become a URL!.  This can be processed
        ; with change-dir commands, but it will be protocol dependent as
        ; to whether a directory listing would be possible (HTTP does not
        ; define a standard for that)
        ;
        if all [
            maybe? [file! url!] source
            file: find/last/tail source slash
        ][
            change-dir copy/part source file
        ]

        ; Also in script mode, the code is immutable by default.
        ;
        ; !!! Note that this does not currently protect the code from binding
        ; changes, and it gets INTERNed below, or by "module/mixin" (?!)
        ;
        lock code

        ; Make the new script object
        original-script: system/script  ; and save old one
        system/script: construct system/standard/script [
            title: select hdr 'title
            header: hdr
            parent: :original-script
            path: what-dir
            args: to-value :arg
        ]

        all [
            set? 'script-pre-load-hook
        |
            script-pre-load-hook is-module hdr ;-- chance to print it out
        ]

        ; Eval the block or make the module, returned
        either is-module [ ; Import the module and set the var
            result: import catch/quit/with [
                module/mixin hdr code (opt do-needs/no-user hdr)
            ] :finalizer
            if next_DO* [set var tail code]
        ][
            do-needs hdr  ; Load the script requirements
            intern code   ; Bind the user script
            result: catch/quit/with [
                do/next code :var ;-- If var is void, /NEXT is revoked
            ] :finalizer
        ]
    ]

    finalizer :result blank
]

export: func [
    "Low level export of values (e.g. functions) to lib."
    words [block!] "Block of words (already defined in local context)"
][
    for-each word words [join lib [word get word]]
]

assert-utf8: function [
    "If binary data is UTF-8, returns it, else throws an error."
    data [binary!]
][
    unless find [0 8] tmp: utf? data [ ; Not UTF-8
        cause-error 'script 'no-decode unspaced ["UTF-" abs tmp]
    ]
    data
]
