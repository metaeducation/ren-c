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


; If the host wants to know if a script or module is loaded, e.g. to print out
; a message.  (Printing directly from this code would be presumptuous.)
;
script-pre-load-hook: _


module: func [
    {Creates a new module}

    return: [module!]
    product: "The result of running the body"
        [<opt> any-value!]

    spec "The header block of the module (modified)"
        [block! object!]
    body "The body block of the module (all bindings will be overwritten)"
        [block!]
    /mixin "Bind body to this additional object before running with DO"
        [object!]
    /into "Add data to existing MODULE! context (vs making a new one)"
        [module!]
    /deep "Bind SET-WORD!s deeply (temp for making DO transition easier)"
][
    ; Originally, UNBIND/DEEP was run on the body as a first step.  We now use
    ; INTERN or the implicit interning done by TRANSCODE to set the baseline of
    ; binding to the module (and things it inherits, like LIB).

    ; Convert header block to standard header object:
    ;
    if block? spec [
        unbind/deep spec
        spec: try attempt [construct/with/only spec system/standard/header]
    ]

    ; Historically, the Name: and Type: fields would tolerate either LIT-WORD!
    ; or WORD! equally well.  This is because it used R3-Alpha's CONSTRUCT,
    ; (which was non-evaluative by default, unlike Ren-C's construct) but
    ; without the /ONLY switch.  In that mode, it decayed LIT-WORD! to WORD!.
    ; To try and standardize the variance, Ren-C does not accept LIT-WORD!
    ; in these slots.
    ;
    ; !!! Although this is a goal, it creates some friction.  Backing off of
    ; it temporarily.
    ;
    if lit-word? spec/name [
        spec/name: as word! noquote spec/name
        ;fail ["Ren-C module Name:" (spec/name) "must be WORD!, not LIT-WORD!"]
    ]
    if lit-word? spec/type [
        spec/type: as word! noquote spec/type
        ;fail ["Ren-C module Type:" (spec/type) "must be WORD!, not LIT-WORD!"]
    ]

    ; Validate the important fields of header:
    ;
    ; !!! This should be an informative error instead of asserts!
    ;
    for-each [var types] [
        spec object!
        body block!
        mixin [<opt> object!]
        spec/name [word! blank!]
        spec/type [word! blank!]
        spec/version [tuple! blank!]
        spec/options [block! blank!]
    ][
        do compose [ensure (types) (var)]  ; names to show if fails
    ]

    ; !!! The plan for MAKE MODULE! is not to heed the size, because it uses
    ; the global hash table of words to access its variables.

    into: default [
        make module! 7  ; arbitrary starting size
    ]
    let mod: into

    ; We give each module a dedicated IMPORT command that knows the identity
    ; of the module, and hence knows where to import to.  (The lower-level
    ; IMPORT* is still available, to specify a "where" to import.)
    ;
    append mod compose [
        import: (specialize :sys/import* [where: mod])
        export: (specialize :sys/export* [where: mod])
        intern: (specialize :intern* [where: mod])
    ]

    if not spec/type [spec/type: 'module]  ; in case not set earlier

    ; Default to having an Exports block in the spec.
    ;
    if not block? select spec 'Exports [
        append spec compose [Exports: (make block! 10)]
    ]

    set-meta mod spec

    ; The INTERN process makes *all* the words "opportunistically bound" to
    ; mod, with inheritance of lib.  While words bound in this way can read
    ; from LIB, they cannot write to lib.  This process does not create any
    ; new storage space for variables.
    ;
    intern* mod body

    ; Historically, modules had a rule of only creating storage space for the
    ; top-level SET-WORD!s in the body.  You might argue that is too much (and
    ; top level words need to be marked with a LET-like construct).  Or it
    ; may be too little (historical DO would bind all ANY-WORD!s into the
    ; user context).
    ;
    ; As a transition to DO of all strings/file/url to getting their own
    ; context, offer a /DEEP switch used by DO to say that SET-WORD! at any
    ; depth gets bound.
    ;
    either deep [
        bind/set body mod
    ][
        bind/only/set body mod
    ]

    if object? mixin [bind body mixin]

    set (product: default [#]) do body

    return mod
]


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
        ^value' [<opt> any-value!]
        /quit
        <with> return
    ][
        let quit_FINALIZER: quit
        quit: :lib/quit

        ; Restore system/script and the dir if they were changed

        if original-script [system/script: original-script]
        if original-path [change-dir original-path]

        if quit_FINALIZER and (only) [
            quit unmeta value'  ; "rethrow" the QUIT if DO/ONLY
        ]

        set 'force-remote-import old-force-remote-import
        return unmeta value'  ; returns from DO*, because of <with> return
    ]

    ; If a file is being mentioned as a DO location and the "current path"
    ; is a URL!, then adjust the source to be a URL! based from that path.
    ;
    if all [url? original-path, file? source] [
         source: join original-path source
    ]

    ; Load the code (do this before CHANGE-DIR so if there's an error in the
    ; LOAD it will trigger before the failure of changing the working dir)
    ;
    ; !!! This said "It is loaded as UNBOUND so that DO-NEEDS runs before
    ; INTERN."  Now that DO-NEEDS no longer exists, what does it mean?
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

    ; When we run code from a file, the "current" directory is changed to the
    ; directory of that script.  This way, relative path lookups to find
    ; dependent files will look relative to the script.  This is believed to
    ; be the best interpretation of shorthands like `read %foo.dat` or
    ; `do %utilities.r`.
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
    ] then [
        === PRINT SCRIPT INFO IF EXECUTING FROM FILE OR URL ===

        ; !!! What should govern the decision to do this?

        if set? 'script-pre-load-hook [
            script-pre-load-hook is-module hdr
        ]
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

    let result
    catch/quit [
        either is-module [
            ;
            ; !!! DO of code that one is not certain if it is a module or a
            ; script is a sketchy concept.  It should be reviewed, since
            ; modules are not supposed to have side-effects.
            ;
            module hdr code

            ; !!! It would be nice if you could modularize a script and
            ; still be able to get a result.  Until you can, make module
            ; execution return void so that it doesn't give a verbose
            ; output when you DO it (so you can see whatever the script
            ; might have PRINT-ed)
            ;
            ; https://github.com/rebol/rebol-issues/issues/2373
            ;
            result: ~void~  ; console won't show VOID!s named ~void~
        ][
            ; !!! A module expects to be able to UNBIND/DEEP the spec it is
            ; given, because it doesn't want stray bindings on any of the words
            ; to leak into the module header.  We COPY here, but as with most
            ; things related to binding, a better solution is needed.
            ;
            ; !!! Using a /DEEP switch to bind all the SET-WORD!s deeply, not
            ; just top-level ones.  This is to ease work on transitioning to
            ; isolated DO.  Review once this basic step is taken.
            ;
            [# result]: module/deep copy [] code
        ]
    ] then :finalizer/quit

    return finalizer get/any 'result
]

