REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Load, Import, Modules"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Context: sys
    Note: {
        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!

        These functions are kept in a single file because they
        are inter-related.

        The fledgling module system in R3-Alpha was never widely used or
        tested, but here is some information:

        http://www.rebol.com/r3/docs/concepts/modules-defining.html
        https://github.com/revault/rebol-wiki/wiki/Module-Design-Details
    }
]

; BASICS:
;
; Code gets loaded in two ways:
;   1. As user code/data - residing in user context
;   2. As module code/data - residing in its own context
;
; Module loading can be delayed. This allows special modules like CGI,
; protocols, or HTML formatters to be available, but not require extra space.
; The system/modules list holds modules for fully init'd modules, otherwise it
; holds their headers, along with the binary or block that will be used to
; init them.

intern: function [
    "Imports (internalizes) words/values from the lib into the user context."
    data [block! any-word!] "Word or block of words to be added (deeply)"
][
    ; for optimization below (index for resolve)
    index: 1 + length of usr: system/contexts/user

    ; Extend the user context with new words
    data: bind/new :data usr

    ; Copy only the new values into the user context
    resolve/only usr lib index

    :data
]


bind-lib: func [
    "Bind only the top words of the block to the lib context (mezzanine load)."
    block [block!]
][
    bind/only/set block lib ; Note: not bind/new !
    bind block lib
    block
]


export-words: func [
    {Exports words of a context into both the system lib and user contexts.}

    ctx "Module context"
        [module! object!]
    words "The exports words block of the module"
        [block! blank!]
][
    if words [
        ; words already set in lib are not overriden
        resolve/extend/only lib ctx words

        ; lib, because of above
        resolve/extend/only system/contexts/user lib words
    ]
]


load-header: function [
    {Loads script header object and body binary (not loaded).}

    return: "[header OBJECT!, body BINARY!, end] or error WORD!"
        [block! word!]
    source "Source code (text! will be UTF-8 encoded)"
        [binary! text!]
    /only "Only process header, don't decompress body"
    /required "Script header is required"

    <static>
    non-ws (make bitset! [not 1 - 32])
][
    ; This function decodes the script header from the script body.  It checks
    ; the header 'compress and 'content options, and supports length-specified
    ; or script-in-a-block embedding.
    ;
    ; It will set the 'content field to the binary source if 'content is true.
    ; The 'content will be set to the source at the position of the beginning
    ; of the script header, skipping anything before it. For multi-scripts it
    ; doesn't copy the portion of the content that relates to the current
    ; script, or at all, so be careful with the source data you get.
    ;
    ; If the 'compress option is set then the body will be decompressed.
    ; Binary vs. script encoded compression will be autodetected.
    ;
    ; Normally, returns the header object, the body text (as binary), and the
    ; the end of the script or script-in-a-block. The end position can be used
    ; to determine where to stop decoding the body text. After the end is the
    ; rest of the binary data, which can contain anything you like. This can
    ; support multiple scripts in the same binary data, multi-scripts.
    ;
    ; If not /only and the script is embedded in a block and not compressed
    ; then the body text will be a decoded block instead of binary, to avoid
    ; the overhead of decoding the body twice.
    ;
    ; Syntax errors are returned as words:
    ;    no-header
    ;    bad-header
    ;    bad-compress
    ;
    end: _ ;-- locals are now void by default, added after that change

    if binary? source [
        ;
        ; Used to "assert this was UTF-8", which was a weak check.
        ; If it's not UTF-8 the decoding will find that out.
        ;
        tmp: source
    ]

    if text? source [tmp: to binary! source]

    data: script? tmp else [ ; no script header found
        return either required ['no-header] [
            reduce [
                '~null~  ;-- no header object
                tmp  ;-- body text
                1  ;-- line number
                tail of tmp  ;-- end of script
            ]
        ]
    ]

    ; The TRANSCODE function returns a BLOCK! containing the transcoded
    ; elements as well as a BINARY! indicating any remainder.  Convention
    ; is also that block has a LINE OF with the line number of the *end*
    ; of the transcoding so far, to sync line numbering across transcodes.

    ; get 'rebol keyword
    ;
    keyrest: transcode/only data
    line: line of keyrest
    set [key: rest:] keyrest

    ; get header block
    ;
    hdrrest: transcode/next/relax/line rest line
    line: line of hdrrest
    set [hdr: rest:] hdrrest

    if not block? :hdr [
        ; header block is incomplete
        return 'no-header
    ]

    if not attempt [hdr: construct/only system/standard/header :hdr] [
        return 'bad-header
    ]

    if :hdr/options [
        if not block? :hdr/options [
            return 'bad-header
        ]
    ]

    if find maybe hdr/options 'content [
        append hdr reduce ['content data]  ; as of start of header
    ]

    if 13 = rest/1 [rest: next rest] ; skip CR
    if 10 = rest/1 [rest: next rest | line: me + 1] ; skip LF

    if integer? tmp: select hdr 'length [
        end: skip rest tmp
    ]

    end: default [tail of data]

    if only [
        ; decompress not done
        return reduce [hdr rest end]
    ]

    if :key = 'rebol [
        ; regular script, binary or script encoded compression supported
        case [
            find maybe hdr/options 'compress [
                rest: any [
                    attempt [
                        ; Raw bits.  whitespace *could* be tolerated; if
                        ; you know the kind of compression and are looking
                        ; for its signature (gzip is 0x1f8b)
                        ;
                        gunzip/part rest end
                    ]
                    attempt [
                        ; BINARY! literal ("'SCRIPT encoded").  Since it
                        ; uses transcode, leading whitespace and comments
                        ; are tolerated before the literal.
                        ;
                        gunzip first transcode/next rest
                    ]
                ] or [
                    return 'bad-compress
                ]
            ] ; else assumed not compressed
        ]
    ] else [
        ; block-embedded script, only script compression, ignore hdr/length

        ; decode embedded script
        rest: skip first set [data: end:] transcode/next data 2

        case [
            find maybe hdr/options 'compress [ ; script encoded only
                rest: attempt [gunzip first rest] or [
                    return 'bad-compress
                ]
            ]
        ]
    ]

    ; Return a BLOCK! with 4 elements in it
    ;
    return reduce [
        ensure object! hdr
        elide (
            ensure [<opt> block!] hdr/options
        )
        ensure [binary! block!] rest
        ensure integer! line
        ensure binary! end
    ]
]


no-all: construct [all] [all: _]
protect 'no-all/all

load: function [
    {Loads code or data from a file, URL, text string, or binary.}

    source "Source or block of sources"
        [tag! file! url! text! binary!]
    /header "Result includes REBOL header object "
    /all "Load all values (cannot be used with /HEADER)"
    /type "Override default file-type"
    ftype "E.g. rebol, text, markup, jpeg... (by default, auto-detected)"
        [word!]
    <in> no-all ;-- temporary fake of <unbind> option
][
    hdr: null

    self: binding of 'return ;-- so you can say SELF/ALL

    ; TAG! means load new script relative to current system/script/path
    ;
    if tag? source [
        if not system/script/path [
            fail ["Can't relatively load" source "- system/script/path not set"]
        ]
        source: join system/script/path to text! source
    ]

    ; NOTES:
    ; Note that code/data can be embedded in other datatypes, including
    ; not just text, but any binary data, including images, etc. The type
    ; argument can be used to control how the raw source is converted.
    ; Pass a /type of blank or 'unbound if you want embedded code or data.
    ; Scripts are normally bound to the user context, but no binding will
    ; happen for a module or if the /type is 'unbound. This allows the result
    ; to be handled properly by DO (keeping it out of user context.)
    ; Extensions will still be loaded properly if /type is 'unbound.
    ; Note that IMPORT has its own loader, and does not use LOAD directly.
    ; /type with anything other than 'extension disables extension loading.

    if header and [self/all] [
        fail "Cannot use /ALL and /HEADER refinements together"
    ]

    ;-- What type of file? Decode it too:
    if match [file! url!] source [
        file: source
        line: 1
        ftype: default [file-type? source else ['rebol]] ; !!! rebol default?

        if ftype = 'extension [
            if not file? source [
                fail ["Can only load extensions from FILE!, not" source]
            ]
            return ensure module! load-extension source ;-- DO embedded script
        ]

        data: read source

        if block? data [
            ;
            ; !!! R3-Alpha's READ is nebulous, comment said "can be string,
            ; binary, block".  Current leaning is that READ always be a
            ; binary protocol, and that LOAD would be higher level--and be
            ; based on decoding BINARY! or some higher level method that
            ; never goes through a binary.  In any case, `read %./` would
            ; return a BLOCK! of directory contents, and LOAD was expected
            ; to return that block...do that for now, for compatibility with
            ; the tests until more work is done.
            ;
            return data
        ]
    ]
    else [
        file: line: null
        data: source
        ftype: default ['rebol]

        if ftype = 'extension [
            fail "Extensions can only be loaded from a FILE! (.DLL, .so)"
        ]
    ]

    if not find [unbound rebol] ftype [
        if find system/options/file-types ftype [
            return decode ftype :data
        ]

        fail ["No" ftype "LOADer found for" type of source]
    ]

    ensure [text! binary!] data

    if block? data [
        return data ;-- !!! Things break if you don't pass through; review
    ]

    ;-- Try to load the header, handle error:
    if not self/all [
        set [hdr: data: line:] load-header data
        hdr: degrade hdr

        if word? hdr [cause-error 'syntax hdr source]
    ]

    ensure [<opt> object!] hdr
    ensure [binary! block! text!] data

    ;-- Convert code to block, insert header if requested:
    if not block? data [
        if text? data [
            data: to binary! data ;-- !!! inefficient, might be UTF8
        ]
        assert [binary? data]
        data: transcode/file/line data (maybe file) (maybe line)
        take/last data ;-- !!! always the residual, a #{}... why?
    ]

    if header [
        insert data reify hdr
    ]

    ;-- Bind code to user context:
    none [
        'unbound = ftype
        'module = select maybe hdr 'type
        find (maybe select maybe hdr 'options) 'unbound
    ] then [
        data: intern data
    ]

    ;-- If appropriate and possible, return singular data value:
    any [
        self/all
        header
        empty? data
        1 < length of data
    ] or [
        data: first data
    ]

    return :data
]


load-module: function [
    {Loads a module and inserts it into the system module list.}

    source {Source (file, URL, binary, etc.) or block of sources}
        [word! file! url! text! binary! module! block!]
    /version "Module must be this version or greater"
    ver [tuple!]
    /no-share "Force module to use its own non-shared global namespace"
    /no-lib "Don't export to the runtime library (lib)"
    /import "Do module import now, overriding /delay and 'delay option"
    /as "New name for the module (not valid for reloads)"
    name [word!]
    /delay "Delay module init until later (ignored if source is module!)"
    /into "Load into an existing module (e.g. populated with some natives)"
    existing [module!]
    /exports "Add exports on top of those in the EXPORTS: section"
    export-list [block!]
][
    as_LOAD_MODULE: :as
    as: :lib/as

    hdr: null

    ; NOTES:
    ;
    ; This is a variation of LOAD that is used by IMPORT. Unlike LOAD, the
    ; module init may be delayed. The module may be stored as binary or as an
    ; unbound block, then init'd later, as needed.
    ;
    ; /no-share and /delay are ignored for module! source because it's too late.
    ; A name is required for all imported modules, delayed or not; /as can be
    ; specified for unnamed modules. If you don't want to name it, don't import.
    ; If source is a module that is loaded already, /as name is an error.
    ;
    ; Returns block of name, and either built module or blank if delayed.
    ; Returns blank if source is word and no module of that name is loaded.
    ; Returns blank if source is file/url and read or load-extension fails.

    if import [delay: _] ; /import overrides /delay

    ; Process the source, based on its type

    switch type of source [
        word! [ ; loading the preloaded
            if as_LOAD_MODULE [
                cause-error 'script 'bad-refine /as ; no renaming
            ]

            ; Return blank if no module of that name found

            tmp: find/skip system/modules source 2 else [
                return blank
            ]

            set [mod:] next tmp

            ensure [module! block!] mod

            ; If no further processing is needed, shortcut return

            if not version and [delay or [module? :mod]] [
                return reduce [source (reify match module! :mod)]
            ]
        ]

        ; !!! Transcoding is currently based on UTF-8.  "UTF-8 Everywhere"
        ; will use that as the internal representation of STRING!, but until
        ; then any strings passed in to loading have to be UTF-8 converted,
        ; which means making them into BINARY!.
        ;
        binary! [data: source]
        text! [data: to binary! source]

        file!
        url! [
            tmp: file-type? source
            case [
                tmp = 'rebol [
                    data: read source or [
                        return blank
                    ]
                ]

                tmp = 'extension [
                    fail "Use LOAD or LOAD-EXTENSION to load an extension"
                ]
            ] else [
                cause-error 'access 'no-script source ; needs better error
            ]
        ]

        module! [
            ; see if the same module is already in the list
            if tmp: find/skip next system/modules mod: source 2 [
                if as_LOAD_MODULE [
                    ; already imported
                    cause-error 'script 'bad-refine /as
                ]

                all [
                    ; not /version, same as top module of that name
                    not version
                    same? mod select system/modules pick tmp 0
                ] then [
                    return copy/part back tmp 2
                ]

                set [mod:] tmp
            ]
        ]

        block! [
            if any [version as] [
                cause-error 'script 'bad-refines blank
            ]

            data: make block! length of source

            parse/match source [
                any [
                    tmp:
                    set name opt set-word!
                    set mod [
                        word! | module! | file! | url! | text! | binary!
                    ]
                    set ver opt tuple! (
                        append data reduce [mod ver if name [to word! name]]
                    )
                ]
            ] else [
                cause-error 'script 'invalid-arg tmp
            ]

            return map-each [mod ver name] source [
                applique 'load-module [
                    source: mod
                    version: version
                    set the ver: :ver
                    as: true
                    set the name: opt name
                    no-share: no-share
                    no-lib: no-lib
                    import: import
                    delay: delay
                ]
            ]
        ]
    ]

    mod: default [_]

    ; Get info from preloaded or delayed modules
    if module? mod [
        delay: no-share: _ hdr: meta-of mod
        ensure [block! blank!] hdr/options
    ]
    if block? mod [
        set [hdr: code:] mod
    ]

    ; module/block mod used later for override testing

    ; Get and process the header
    if not hdr [
        ; Only happens for string, binary or non-extension file/url source
        set [hdr: code: line:] load-header/required data
        case [
            word? hdr [cause-error 'syntax hdr source]
            import [
                ; /import overrides 'delay option
            ]
            not delay [delay: did find maybe hdr/options 'delay]
        ]
    ] else [
        ; !!! Some circumstances, e.g. `do <json>`, will wind up not passing
        ; a URL! to this routine, but a MODULE!.  If so, it has already been
        ; transcoded...so line numbers in the text are already accounted for.
        ; These mechanics need to be better understood, but until it's known
        ; exactly why it's working that way fake a line number so that the
        ; rest of the code does not complain.
        ;
        line: 1
    ]
    if no-share [
        hdr/options: append any [hdr/options make block! 1] 'isolate
    ]

    ; Unify hdr/name and /as name
    if name [
        hdr/name: name  ; rename /as name
    ] else [
        name: :hdr/name
    ]

    if not no-lib and [not word? :name] [ ; requires name for full import
        ; Unnamed module can't be imported to lib, so /no-lib here
        no-lib: true  ; Still not /no-lib in IMPORT

        if not find maybe hdr/options 'private [
            hdr/options: append any [hdr/options make block! 1] 'private
        ]
    ]
    if not tuple? set 'modver :hdr/version [
        modver: 0.0.0 ; get version
    ]

    ; See if it's there already, or there is something more recent
    all [
        ; set to false later if existing module is used
        override?: not no-lib
        pos: find/skip system/modules name 2
        set [name0: mod0:] pos
    ] then [
        ; Get existing module's info

        if module? :mod0 [hdr0: meta-of mod0] ; final header
        if block? :mod0 [hdr0: first mod0] ; cached preparsed header

        ensure word! name0
        ensure object! hdr0

        if not tuple? ver0: :hdr0/version [
            ver0: 0.0.0
        ]

        ; Compare it to the module we want to load
        case [
            same? mod mod0 [
                override?: not any [delay module? mod] ; here already
            ]

            module? mod0 [
                ; premade module
                pos: _  ; just override, don't replace
                if ver0 >= modver [
                    ; it's at least as new, use it instead
                    mod: mod0 | hdr: hdr0 | code: _
                    modver: ver0
                    override?: false
                ]
            ]

            ; else is delayed module
            ver0 > modver [ ; and it's newer, use it instead
                mod: _ set [hdr code] mod0
                modver: ver0
                ext: all [(object? code) code] ; delayed extension
                override?: not delay  ; stays delayed if /delay
            ]
        ]
    ]

    if not module? mod [
        mod: _ ; don't need/want the block reference now
    ]

    if version and [ver > modver] [
        cause-error 'syntax 'needs reduce [name ver]
    ]

    ; If no further processing is needed, shortcut return
    if (not override?) and [any [mod delay]] [return reduce [name mod]]

    ; If /delay, save the intermediate form
    if delay [
        mod: reduce [hdr either object? ext [ext] [code]]
    ]

    ; Else not /delay, make the module if needed
    if not mod [
        ; not prebuilt or delayed, make a module

        if find maybe hdr/options 'isolate [no-share: true] ; in case of delay

        if binary? code [code: make block! code]

        ensure object! hdr
        ensure block! code

        if exports [
            if null? hdr/exports [
                append hdr compose [exports: ((export-list))]
            ] else [
                append exports hdr/exports
                hdr/exports: export-list
            ]
        ]

        catch/quit [
            mod: module/into hdr code :existing
        ]
    ]

    if not no-lib and [override?] [
        if pos [
            pos/2: mod ; replace delayed module
        ] else [
            append system/modules reduce [name mod]
        ]

        all [
            module? mod
            block? select hdr 'exports
        ] then [
            resolve/extend/only lib mod hdr/exports ; no-op if empty
        ]
    ]

    reduce [
        name
        match module! mod
        ensure integer! line
    ]
]


; See also: sys/make-module*, sys/load-module
;
import: function [
    {Imports a module; locate, load, make, and setup its bindings.}

    module [word! file! url! text! binary! module! block! tag!]
    /version "Module must be this version or greater"
    ver [tuple!]
    /no-share "Force module to use its own non-shared global namespace"
    /no-lib "Don't export to the runtime library (lib)"
    /no-user "Don't export to the user context"
][
    ; `import <name>` looks up relative path
    ;
    if tag? module [
        if not system/script/path [
            fail ["Can't relatively load" module "- system/script/path not set"]
        ]
        module: join system/script/path to text! module
    ]

    set [name: mod:] applique 'load-module [
        source: module
        version: version
        set the ver: :ver
        no-share: no-share
        no-lib: no-lib
        import: true ;-- !!! original code always passed /IMPORT, should it?
    ]

    case [
        mod [
            ; success!
        ]

        word? module [
            ;
            ; Module (as word!) is not loaded already, so try to find it.
            ;
            file: append to file! module system/options/default-suffix

            for-each path system/options/module-paths [
                if set [name: mod:] (
                    applique 'load-module [
                        source: path/:file
                        version: version
                        set the ver: :ver
                        no-share: :no-share
                        no-lib: :no-lib
                        import: true
                    ]
                ) [
                    break
                ]
            ]
        ]

        match [file! url!] module [
            cause-error 'access 'cannot-open reduce [
                module "not found or not valid"
            ]
        ]
    ]

    if not mod [
        cause-error 'access 'cannot-open reduce [module "module not found"]
    ]

    ; Do any imports to the user context that are necessary.
    ; The lib imports were handled earlier by LOAD-MODULE.
    case [
        any [
            no-user
            not block? exports: select hdr: meta-of mod 'exports
            empty? exports
        ][
            ; Do nothing if /no-user or no exports.
        ]

        any [
            no-lib
            find select hdr 'options 'private ; /no-lib causes private
        ][
            ; It's a private module
            ; we must add *all* of its exports to user

            resolve/extend/only system/contexts/user mod exports
        ]

        ; Unless /no-lib its exports are in lib already
        ; ...so just import what we need.
        ;
        not no-lib [
            resolve/only system/contexts/user lib exports
        ]
    ]

    return ensure module! mod
]


export [load import]
