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


; If the host wants to know if a script or module is loaded, e.g. to print out
; a message.  (Printing directly from this code would be presumptuous.)
;
script-pre-load-hook: _


; DO of functions, blocks, paths, and other do-able types is done directly by
; C code in REBNATIVE(do).  But that code delegates to this Rebol function
; for ANY-STRING! and BINARY! types (presumably because it would be laborious
; to express as C).
;
do*: func [
    {SYS: Called by system for DO on datatypes that require special handling}

    return: [<opt> any-value!]
    source "Files, urls and modules evaluate as scripts, other strings don't"
        [file! url! text! binary! tag!]
    args "Args passed as system/script/args to a script (normally a string)"
        [<opt> any-value!]
    only "Do not catch quits...propagate them"
        [logic!]
][
    ; !!! DEMONSTRATION OF CONCEPT... this translates a tag into a URL!, but
    ; it should be using a more "official" URL instead of on individuals
    ; websites.  There should also be some kind of local caching facility.
    ;
    ; force-remote-import is defined in sys-load.r
    ;
    let old-force-remote-import: force-remote-import

    if tag? source [
        set 'force-remote-import true
        ; Convert value into a URL!
        source: switch source
            (load system/locale/library/utilities)
        else [
            fail [
                {Module} source {not in system/locale/library}
            ]
        ]
    ]

    ; Note that DO of file path evaluates in the directory of the target file.
    ;
    ; !!! There are some issues with this idea of preserving the path--one of
    ; which is that WHAT-DIR may return null.
    ;
    let original-path: try what-dir
    let original-script: _

    let finalizer: func [
        value [<opt> any-value!]
        /quit
        <with> return
    ][
        let quit_FINALIZER: quit
        quit: :lib/quit

        ; Restore system/script and the dir if they were changed

        if original-script [system/script: original-script]
        if original-path [change-dir original-path]

        if quit_FINALIZER and only [
            quit get/any 'value  ; "rethrow" the QUIT if DO/ONLY
        ]

        set 'force-remote-import old-force-remote-import
        return get/any 'value  ; returns from DO*, because of <with> return
    ]

    ; If a file is being mentioned as a DO location and the "current path"
    ; is a URL!, then adjust the source to be a URL! based from that path.
    ;
    if all [url? original-path, file? source] [
         source: join original-path source
    ]

    ; Load the code (do this before CHANGE-DIR so if there's an error in the
    ; LOAD it will trigger before the failure of changing the working dir)
    ; It is loaded as UNBOUND so that DO-NEEDS runs before INTERN.
    ;
    let hdr
    let code
    [code hdr]: load/type source 'unbound

    ; !!! This used to LOCK the header, but the module processing wants to
    ; do some manipulation to it.  Review.  In the meantime, in order to
    ; allow mutation of the OBJECT! we have to actually TAKE the hdr out
    ; of the returned result to avoid LOCKing it when the code array is locked
    ; because even with series not at their head, LOCK NEXT CODE will lock it.
    ;
    ensure block! code
    ensure [object! blank!] hdr: default [_]
    let is-module: 'module = select hdr 'type

    let result
    all [
        text? source
        not is-module
    ] then [
        ; Return result without "script overhead" (e.g. don't change the
        ; working directory to the base of the file path supplied)
        ;
        do-needs hdr  ; Load the script requirements
        intern code   ; Bind the user script
        catch/quit [
            ;
            ; The source string may have been mutable or immutable, but the
            ; loaded code is not locked for this case.  So this works:
            ;
            ;     do "append {abc} {de}"
            ;
            result: do code  ; !!! pass args implicitly?
        ] then :finalizer/quit
    ] else [
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
        all [
            match [file! url!] source
            let file: find-last/tail source slash
            elide change-dir copy/part source file
        ]

        ; Make the new script object
        original-script: system/script  ; and save old one
        system/script: make system/standard/script compose [
            title: try select hdr 'title
            header: hdr
            parent: :original-script
            path: what-dir
            args: (try :args)
        ]

        if set? 'script-pre-load-hook [
            script-pre-load-hook is-module hdr  ; chance to print it out
        ]

        ; Eval the block or make the module, returned
        either is-module [ ; Import the module and set the var
            catch/quit [
                import module/mixin hdr code (opt do-needs/no-user hdr)

                ; !!! It would be nice if you could modularize a script and
                ; still be able to get a result.  Until you can, make module
                ; execution return void so that it doesn't give a verbose
                ; output when you DO it (so you can see whatever the script
                ; might have PRINT-ed)
                ;
                ; https://github.com/rebol/rebol-issues/issues/2373
                ;
                result: ~
            ] then :finalizer/quit
        ][
            do-needs hdr  ; Load the script requirements
            intern code   ; Bind the user script
            catch/quit [
                result: do code
            ] then :finalizer/quit
        ]
    ]

    return finalizer get/any 'result
]

export: func [
    "Low level export of values (e.g. functions) to lib."
    words [block!] "Block of words (already defined in local context)"
][
    for-each word words [
        append lib reduce [word get word]
    ]
]
