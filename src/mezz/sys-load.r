REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Load, Import, Modules"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
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


bind-lib: func [
    {Bind only the top words of the block to the lib context (mezzanine load)}
    block [block!]
][
    bind/only/set block lib  ; Note: not BIND/NEW !
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


mixin?: func [
    "Returns TRUE if module is a mixin with exports."
    return: [logic!]
    mod [module! object!] "Module or spec header"
][
    ; Note: Unnamed modules DO NOT default to being mixins

    if module? mod [mod: meta-of mod]  ; Get the header object

    return did all [
        find select mod 'options 'private

        ; If there are no exports, there's no difference
        block? select mod 'exports
        not empty? select mod 'exports
    ]
]


load-header: function [
    {Loads script header object and body binary (not loaded).}

    return: "header OBJECT! if present, or error WORD!"
        [<opt> object! word!]
    source "Source code (text! will be UTF-8 encoded)"
        [binary! text!]
    /file "Where source is being loaded from"
        [file! url!]
    /only "Only process header, don't decompress body"
    /required "Script header is required"
    /body [<output> binary! block!]
    /line [<output> integer!]
    /final [<output> binary!]

    <static>
    non-ws (make bitset! [not 1 - 32])
][
    let line-out: line  ; we use LINE for the line number inside the body
    line: 1

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
    ; If not /ONLY and the script is embedded in a block and not compressed
    ; then the body text will be a decoded block instead of binary, to avoid
    ; the overhead of decoding the body twice.
    ;
    ; Syntax errors are returned as words:
    ;    no-header
    ;    bad-header
    ;    bad-compress

    if binary? source [
        tmp: source  ; if it's not UTF-8, the decoding will provide the error
    ]

    if text? source [tmp: as binary! source]

    if not (data: script? tmp) [  ; !!! Review: SCRIPT? doesn't return LOGIC!
        ; no script header found
        return either required ['no-header] [
            if body [set body tmp]
            if line-out [set line-out line]  ; e.g. line 1
            if final [set final tail of tmp]
            return null  ; no header object
        ]
    ]

    ; The TRANSCODE function returns a BLOCK! containing the transcoded
    ; elements as well as a BINARY! indicating any remainder.  Convention
    ; is also that block has a LINE OF with the line number of the *end*
    ; of the transcoding so far, to sync line numbering across transcodes.

    ; get 'rebol keyword

    let key
    let rest
    [key rest]: transcode/file/line data file 'line

    ; get header block

    let hdr
    let error
    [hdr rest error]: transcode/file/line rest file 'line

    if error [fail error]

    if not block? :hdr [
        return 'no-header  ; header block is incomplete
    ]

    trap [
        hdr: construct/with/only :hdr system/standard/header
    ] then [
        return 'bad-header
    ]

    (match [block! blank!] try :hdr/options) else [
        return 'bad-header
    ]

    if find hdr/options 'content [
        append hdr compose [content (data)]  ; as of start of header
    ]

    if 10 = rest/1 [rest: next rest, line: me + 1]  ; skip LF

    if integer? tmp: select hdr 'length [
        end: skip rest tmp
    ] else [
        end: tail of data
    ]

    if only [  ; when it's /ONLY, decompression is not performed
        if body [set body rest]
        if line-out [set line-out line]
        if final [set final end]
        return hdr
    ]

    let binary
    if :key = 'rebol [
        ; regular script, binary or script encoded compression supported
        case [
            find hdr/options 'compress [
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
                        [binary rest]: transcode/file/line rest file 'line
                        gunzip binary
                    ]
                ] else [
                    return 'bad-compress
                ]
            ]  ; else assumed not compressed
        ]
    ] else [
        ; block-embedded script, only script compression

        let data
        data: transcode data  ; decode embedded script
        rest: skip data 2  ; !!! what is this skipping ("hdr/length" ??)

        if find hdr/options 'compress [  ; script encoded only
            rest: attempt [gunzip first rest] else [
                return 'bad-compress
            ]
        ]
    ]

    if body [set body ensure [binary! block!] rest]
    if line-out [set line-out ensure integer! line]
    if final [set final ensure binary! end]

    ensure object! hdr
    ensure [block! blank!] hdr/options
    return hdr
]


load: function [
    {Loads code or data from a file, URL, text string, or binary.}

    return: "Loaded code (may be single-value if /header or /all not used)"
        [any-value!]
    source "Source or block of sources"
        [file! url! text! binary! block!]
    /header "Request the Rebol header object be returned as well"
        [<output> object!]
    /all "Load all values (cannot be used with /HEADER)"  ; use all_LOAD
    /type "E.g. rebol, text, markup, jpeg... (by default, auto-detected)"
        [word!]
][
    all_LOAD: :all
    all: :lib/all

    ; Note that code or data can be embedded in other datatypes, including
    ; not just text, but any binary data, including images, etc. The type
    ; argument can be used to control how the raw source is converted.
    ; Pass a /TYPE of blank or 'UNBOUND if you want embedded code or data.
    ;
    ; Scripts are normally bound to the user context, but no binding will
    ; happen for a module or if the /TYPE is 'UNBOUND.  This allows the result
    ; to be handled properly by DO (keeping it out of user context.)
    ; Extensions will still be loaded properly if type is unbound.
    ;
    ; Note that IMPORT has its own loader, and does not use LOAD directly.
    ; /TYPE with anything other than 'EXTENSION disables extension loading.

    all [header, all_LOAD] then [
        fail "Cannot use /ALL and /HEADER refinements together"
    ]

    if block? source [
        ; A BLOCK! means multiple sources, calls LOAD recursively for each

        return map-each s source [
            applique 'load [
                source: s
                header: header
                all: all_LOAD
                type: :type
            ]
        ]
    ]

    ; Detect file type, and decode the data

    if match [file! url!] source [
        file: source
        line: 1
        type: default [file-type? source else ['rebol]]  ; !!! rebol default?

        if type = 'extension [
            if not file? source [
                fail ["Can only load extensions from FILE!, not" source]
            ]
            return ensure module! load-extension source  ; DO embedded script
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
        type: default ['rebol]

        if type = 'extension [
            fail "Extensions can only be loaded from a FILE! (.DLL, .so)"
        ]
    ]

    if not find [unbound rebol] type [
        if find system/options/file-types type [
            return decode type :data
        ]

        fail ["No" type "LOADer found for" type of source]
    ]

    ensure [text! binary!] data

    if block? data [
        return data  ; !!! Things break if you don't pass through; review
    ]

    ; Try to load the header, handle error

    if not all_LOAD [
        hdr: line: _  ; for SET-WORD! gathering, evolving...
        either object? data [
            fail "Code has not been updated for LOAD-EXT-MODULE"
            load-ext-module data
        ][
            [hdr data line]: load-header/file data file
        ]

        if word? hdr [cause-error 'syntax hdr source]
    ]

    ensure [blank! object!] hdr: default [_]
    ensure [binary! block! text!] data

    ; Convert code to block, insert header if requested

    if not block? data [
        assert [match [binary! text!] data]  ; UTF-8
        data: transcode/file/line data file 'line
    ]

    ; Bind code to user context

    none [
        'unbound = type
        'module = select hdr 'type
        find try get 'hdr/options 'unbound
    ] then [
        data: intern data
    ]

    ; If appropriate and possible, return singular data value.
    ;
    ; !!! How good an idea is this, really?  People are used to saying
    ; load "10" and getting `10`, not `[10]`.  But it seems like this makes
    ; the process have some variability to it that makes it a poor default.
    ;
    none [
        all_LOAD
        header  ; technically doesn't prevent returning single value (?)
        (length of data) <> 1
    ] then [
        data: first data
    ]

    if header [
        set header opt hdr
    ]
    return :data
]


do-needs: function [
    {Process the NEEDS block of a program header. Returns unapplied mixins.}

    needs "Needs block, header or version"
        [block! object! tuple! blank!]  ; has to handle blank! if in object!
    /no-share "Force module to use its own non-shared global namespace"
    /no-lib "Don't export to the runtime library"
    /no-user "Don't export to the user context (mixins returned)"
    /block "Return all the imported modules in a block, instead"
][
    ; This is a low-level function and its use and return values reflect that.
    ;
    ; In user mode, the mixins are applied by IMPORT, so they don't need to
    ; be returned. In /NO-USER mode the mixins are collected into an object
    ; and returned, if the object isn't empty. This object can then be passed
    ; to MAKE module! to be applied there.
    ;
    ; The /BLOCK option returns a block of all the modules imported, not any
    ; mixins.  This is for when IMPORT is called with a `Needs: [...]` block.

    if object? needs [  ; header object
        needs: select needs 'needs  ; (protected)
    ]

    switch type of needs [
        blank! [return blank]  ; may have come from SELECT, not just arg

        tuple! [  ; simple version number check for interpreter itself
            case [
                needs > system/version [
                    cause-error 'syntax 'needs reduce ['core needs]
                ]

                3 >= length of needs [  ; no platform id
                    blank
                ]

                (needs and+ 0.0.0.255.255)
                <> (system/version and+ 0.0.0.255.255) [
                    cause-error 'syntax 'needs reduce ['core needs]
                ]
            ]
            return blank
        ]

        block! [
            if empty? needs [return blank]
        ]
    ] else [
        needs: reduce [needs]  ; If it's an inline value, put it in a block
    ]

    ; Parse the needs dialect [source <version>]

    mods: make block! length of needs
    name: vers: hash: _
    parse ensure block! needs [
        here:
        opt [opt 'core set vers tuple! (do-needs vers)]
        any [
            here:
            set name [word! | file! | url! | tag!]
            set vers opt tuple!
            set hash opt binary!
            (append mods reduce [name vers hash])
        ]
        end
    ] else [
        cause-error 'script 'invalid-arg here
    ]

    ; Temporary object to collect exports of "mixins" (private modules).
    ; Don't bother if returning all the modules in a block, or if in user mode.
    ;
    if no-user and [not block] [
        mixins: make object! 0  ; Minimal length since it may persist later
    ]

    ; Import the modules:
    ;
    mods: map-each [name vers hash] mods [
        mod: applique 'import [
            module: name

            version: true  ; !!! automatic from VERS?
            ver: opt vers

            no-share: no-share
            no-lib: no-lib
            no-user: no-user
        ]

        ; Collect any mixins into the object (if we are doing that)
        if all [set? 'mixins, mixin? mod] [
            resolve/extend/only mixins mod select meta-of mod 'exports
        ]
        mod
    ]

    return try case [
        block [mods]  ; /BLOCK refinement asks for block of modules
        not empty? to-value :mixins [mixins]  ; if any mixins, return them
    ]
]


load-module: func [
    {Loads a module and inserts it into the system module list.}

    source {Source (file, URL, binary, etc.) or block of sources}
        [word! file! url! text! binary! module! block!]
    /version "Module must be this version or greater"
        [tuple!]
    /no-share "Force module to use its own non-shared global namespace"
    /no-lib "Don't export to the runtime library (lib)"
    /import "Do module import now, overriding /delay and 'delay option"
    /as "New name for the module (not valid for reloads)"
        [word!]
    /delay "Delay module init until later (ignored if source is module!)"
    /into "Load into an existing module (e.g. populated with some natives)"
        [module!]
    /exports "Add exports on top of those in the EXPORTS: section"
        [block!]
][
    let name: as
    as: :lib/as

    let mod: null
    let hdr: null
    let tmp: null

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

    if import [delay: _]  ; /import overrides /delay

    ; Process the source, based on its type

    let data
    switch type of source [
        word! [ ; loading the preloaded
            if name [
                cause-error 'script 'bad-refine /as  ; no renaming
            ]

            ; Return blank if no module of that name found

            tmp: find/skip system/modules source 2 else [
                return blank
            ]

            set [mod:] next tmp

            ensure [module! block!] mod

            ; If no further processing is needed, shortcut return

            all [not version, any [delay, module? :mod]] then [
                return reduce [source (try match module! :mod)]
            ]
        ]

        ; !!! Transcoding is currently based on UTF-8.  "UTF-8 Everywhere"
        ; will use that as the internal representation of STRING!, but until
        ; then any strings passed in to loading have to be UTF-8 converted,
        ; which means making them into BINARY!.
        ;
        binary!  ; bytes are not known to be valid UTF-8
        text! [  ; bytes are known to be valid UTF-8
            data: source
        ]

        file!
        url! [
            let tmp: file-type? source
            case [
                tmp = 'rebol [
                    data: read source else [
                        return blank
                    ]
                ]

                tmp = 'extension [
                    fail "Use LOAD or LOAD-EXTENSION to load an extension"
                ]
            ] else [
                cause-error 'access 'no-script source  ; !!! need better error
            ]
        ]

        module! [
            ; see if the same module is already in the list
            if let tmp: find/skip next system/modules mod: source 2 [
                if name [
                    ; already imported
                    cause-error 'script 'bad-refine /as
                ]

                all [
                    ; not /VERSION, same as top module of that name
                    not version
                    same? mod select system/modules pick tmp 0
                ] then [
                    return copy/part back tmp 2
                ]

                set [mod:] tmp
            ]
        ]

        block! [
            if any [version name] [
                cause-error 'script 'bad-refines blank
            ]

            data: make block! length of source

            let tmp
            parse source [
                any [
                    tmp:
                    set name opt set-word!
                    set mod [
                        word! | module! | file! | url! | text! | binary!
                    ]
                    set version opt tuple! (
                        append data reduce [
                            mod version if name [to word! name]
                        ]
                    )
                ]
                end
            ] else [
                cause-error 'script 'invalid-arg tmp
            ]

            return map-each [mod ver name] source [
                applique 'load-module [
                    source: mod
                    version: version
                    as: name
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

    let code
    let line
    if null? hdr [
        ; Only happens for string, binary or non-extension file/url source

        [hdr code line]: load-header/file/required data (
            match [file! url!] source
        )
        case [
            word? hdr [cause-error 'syntax hdr source]
            import [
                ; /IMPORT overrides 'delay option
            ]
            not delay [delay: did find hdr/options 'delay]
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

    ; Unify hdr/name and /AS name
    if name [
        hdr/name: name  ; rename /AS name
    ] else [
        name: :hdr/name
    ]

    if (not no-lib) and @[not word? :name] [
        ;
        ; Requires name for full import
        ; Unnamed module can't be imported to lib, so /NO-LIB here

        no-lib: true  ; Still not /NO-LIB in IMPORT

        ; But make it a mixin and it will be imported directly later

        if not find hdr/options 'private [
            hdr/options: append any [hdr/options make block! 1] 'private
        ]
    ]
    if not tuple? let modver: :hdr/version [
        modver: 0.0.0 ; get version
    ]

    ; See if it's there already, or there is something more recent

    let name0
    let mod0
    let ver0
    let hdr0
    let override?
    let pos
    all [
        override?: not no-lib  ; set to false later if existing module is used
        set [name0: mod0:] pos: try find/skip system/modules name 2
    ]
    then [  ; Get existing module's info

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
                override?: not any [delay module? mod]  ; here already
            ]

            module? mod0 [
                ; premade module
                pos: _   ; just override, don't replace
                if ver0 >= modver [
                    ; it's at least as new, use it instead
                    mod: mod0, hdr: hdr0, code: _
                    modver: ver0
                    override?: false
                ]
            ]

            ; else is delayed module

            ver0 > modver [  ; and it's newer, use it instead
                mod: _ set [hdr code] mod0
                modver: ver0
                ext: all [(object? code) code]  ; delayed extension
                override?: not delay  ; stays delayed if /delay
            ]
        ]
    ]

    if not module? mod [
        mod: _   ; don't need/want the block reference now
    ]

    all [version, version > modver] then [
        cause-error 'syntax 'needs reduce [name version]
    ]

    ; If no further processing is needed, shortcut return
    if (not override?) and (mod or delay) [return reduce [name mod]]

    ; If /DELAY, save the intermediate form
    if delay [
        mod: reduce [hdr either object? ext [ext] [code]]
    ]

    ; Else not /DELAY, make the module if needed
    if not mod [
        ; not prebuilt or delayed, make a module

        if find hdr/options 'isolate [no-share: true]  ; in case of delay

        if object? code [ ; delayed extension
            fail "Code has not been updated for LOAD-EXT-MODULE"

            set [hdr: code:] load-ext-module code
            hdr/name: name ; in case of delayed rename
            if all [no-share not find hdr/options 'isolate] [
                hdr/options: append any [hdr/options make block! 1] 'isolate
            ]
        ]

        if binary? code [code: make block! code]

        ensure object! hdr
        ensure block! code

        if exports [
            if null? select hdr 'exports [
                append hdr compose [exports: (exports)]
            ] else [
                append exports hdr/exports
                hdr/exports: exports
            ]
        ]

        catch/quit [
            mod: module/mixin/into hdr code (
                opt do-needs/no-user hdr
            ) :into
        ]
    ]

    all [
        not no-lib
        override?
    ] then [
        if pos [
            pos/2: mod  ; replace delayed module
        ] else [
            append system/modules reduce [name mod]
        ]

        all [
            module? mod
            not mixin? hdr
            block? select hdr 'exports
        ] then [
            resolve/extend/only lib mod hdr/exports  ; no-op if empty
        ]
    ]

    reduce [
        name
        match module! mod
        ensure integer! line
    ]
]

; If TRUE, IMPORT 'MOD acts as IMPORT <MOD>
;
force-remote-import: false

; See also: SYS/MAKE-MODULE*, SYS/LOAD-MODULE, SYS/DO-NEEDS
;
import: function [
    {Imports a module; locate, load, make, and setup its bindings.}

    return: "Loaded module (or block of modules if argument was block)"
        [<opt> module! block!]
    module [word! file! url! text! binary! module! block! tag!]
    /version "Module must be this version or greater"
        [tuple!]
    /no-share "Force module to use its own non-shared global namespace"
    /no-lib "Don't export to the runtime library (lib)"
    /no-user "Don't export to the user context"
][
    old-force-remote-import: force-remote-import
    ; `import <name>` will look in the module library for the "actual"
    ; module to load up, and drop through.
    ;
    ; if a module is loaded with `import <mod1>`
    ; then every nested `import 'mod2`
    ; is forced to `import <mod2>`
    ;
    if tag? module [set 'force-remote-import true]
    if all [force-remote-import, word? module] [
        module: to tag! module
    ]
    if tag? module [
        tmp: (select load system/locale/library/modules module) else [
            cause-error 'access 'cannot-open reduce [
                module "module not found in system/locale/library/modules"
            ]
        ]

        module: (first tmp) else [
            cause-error 'access 'cannot-open reduce [
                module "error occurred in loading module"
                    "from system/locale/library/modules"
            ]
        ]
    ]

    ; If it's a needs dialect block, call DO-NEEDS with /BLOCK:
    ;
    ; Note: IMPORT block! returns a block of all the modules imported.
    ;
    if block? module [
        assert [not version] ; can only apply to one module
        return applique 'do-needs [
            needs: module
            no-share: :no-share
            no-lib: :no-lib
            no-user: :no-user
            block: true
        ]
    ]

    set [name: mod:] applique 'load-module [
        source: module
        version: version
        no-share: no-share
        no-lib: no-lib
        import: true  ; !!! original code always passed /IMPORT, should it?
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
            ; Do nothing if /NO-USER or no exports.
        ]

        any [
            no-lib
            find select hdr 'options 'private  ; /NO-LIB causes private
        ][
            ; It's a private module (mixin)
            ; we must add *all* of its exports to user

            resolve/extend/only system/contexts/user mod exports
        ]

        ; Unless /NO-LIB its exports are in lib already
        ; ...so just import what we need.
        ;
        not no-lib [
            resolve/only system/contexts/user lib exports
        ]
    ]
    set 'force-remote-import old-force-remote-import
    return ensure module! mod
]


export [load import]
