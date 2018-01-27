REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Load, Import, Modules"
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
        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!

        These functions are kept in a single file because they
        are inter-related.

        The fledgling module system in R3-Alpha was never widely used or
        tested, but there's a description page here:

        http://www.rebol.com/r3/docs/concepts/modules-defining.html
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
    "Exports words of a context into both the system lib and user contexts."
    ctx [module! object!]
        "Module context"
    words [block! blank!]
        "The exports words block of the module"
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
    mod [module! object!] "Module or spec header"
][
    ; Note: Unnamed modules DO NOT default to being mixins.
    if module? mod [mod: meta-of mod]  ; Get the header object
    did all [
        find select mod 'options 'private
        ; If there are no exports, there's no difference
        block? select mod 'exports
        not empty? select mod 'exports
    ]
]


load-header: function [
    "Loads script header object and body binary (not loaded)."
    source [binary! string!]
        "Source code (string! will be UTF-8 encoded)"
    /only
        "Only process header, don't decompress or checksum body"
    /required
        "Script header is required"

    <static>
    non-ws (make bitset! [not 1 - 32])
][
    ; This function decodes the script header from the script body.
    ; It checks the header 'checksum and 'compress and 'content options,
    ; and supports length-specified or script-in-a-block embedding.
    ;
    ; It will set the 'content field to the binary source if 'content is true.
    ; The 'content will be set to the source at the position of the beginning
    ; of the script header, skipping anything before it. For multi-scripts it
    ; doesn't copy the portion of the content that relates to the current
    ; script, or at all, so be careful with the source data you get.
    ;
    ; If the 'compress option is set then the body will be decompressed.
    ; Binary vs. script encoded compression will be autodetected. The
    ; header 'checksum is compared to the checksum of the decompressed binary.
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
    ;    bad-checksum
    ;    bad-compress
    ;
    ; Note: set and :var used - prevent malicious code errors.
    ; Commented assert statements are for documentation and testing.
    ;
    end: _ ;-- locals are now unset by default, added after that change

    case/all [
        binary? source [tmp: assert-utf8 source]

        string? source [tmp: to binary! source]

        not data: script? tmp [ ; no script header found
            return either required ['no-header] [reduce [_ tmp tail of tmp]]
        ]

        true [
            ; get 'rebol keyword
            ;
            set* [key: rest:] transcode/only data

            ; get header block
            ;
            set* [hdr: rest:] transcode/next/relax rest
        ]

        not block? :hdr [
            ; header block is incomplete
            return 'no-header
        ]

        not attempt [hdr: construct/only system/standard/header :hdr] [
            return 'bad-header
        ]

        not any [block? :hdr/options blank? :hdr/options] [
            return 'bad-header
        ]

        not any [binary? :hdr/checksum blank? :hdr/checksum] [
            return 'bad-checksum
        ]

        find hdr/options 'content [
            join hdr ['content data] ; as of start of header
        ]

        13 = rest/1 [rest: next rest] ; skip CR
        10 = rest/1 [rest: next rest] ; skip LF

        integer? tmp: select hdr 'length [
            end: skip rest tmp
        ]

        not end [end: tail of data]

        only [
            ; decompress and checksum not done
            return reduce [hdr rest end]
        ]

        sum: hdr/checksum [
            ; blank saved to simplify later code
            blank ;[print sum]
        ]

        :key = 'rebol [
            ; regular script, binary or script encoded compression supported
            case [
                find hdr/options 'compress [
                    ; skip whitespace after header
                    rest: any [find rest non-ws | rest]

                    ; automatic detection of compression type
                    unless rest: any [
                        attempt [
                            ; binary compression
                            decompress/part rest end
                        ]
                        attempt [
                            ; script encoded
                            decompress first transcode/next rest
                        ]
                    ][
                        return 'bad-compress
                    ]

                    if sum and (sum != checksum/secure rest) [
                        return 'bad-checksum
                    ]
                ] ; else assumed not compressed

                sum and (sum <> checksum/secure/part rest end) [
                    return 'bad-checksum
                ]
            ]
        ]

        :key <> 'rebol [
            ; block-embedded script, only script compression, ignore hdr/length

            tmp: ensure binary! rest ; saved for possible checksum calc later

            ; decode embedded script
            rest: skip first set [data: end:] transcode/next data 2

            case [
                find hdr/options 'compress [ ; script encoded only
                    unless rest: attempt [decompress first rest] [
                        return 'bad-compress
                    ]

                    if sum and (sum <> checksum/secure rest) [
                        return 'bad-checksum
                    ]
                ]

                sum and (sum <> checksum/secure/part tmp back end) [
                    return 'bad-checksum
                ]
            ]
        ]

    ]

    ensure [binary! blank!] hdr/checksum
    ensure [block! blank!] hdr/options

    ; Return a BLOCK! with 3 elements in it
    ;
    return reduce [
        ensure object! hdr
        ensure [binary! block!] rest
        ensure binary! end
    ]
]


read-decode: function [
    "Reads code/data from source or DLL, decodes it, returns result."
    source [file! url!]
        "Source (binary, block, image,...) or block of sources?"
    type [word! blank!]
        "File type, or NONE for binary raw data"
][
    either type = 'extension [
        ; DLL-based extension, try to load it (will fail if source is a url)
        ; `load-extension` returns an object or throws an error
        data: load-extension source
    ][
        data: read source ; can be string, binary, block
        if find system/options/file-types type [
            ; e.g. not 'unbound
            data: decode type :data
        ]
    ]
    data
]


load: function [
    {Loads code or data from a file, URL, string, or binary.}
    source [file! url! string! binary! block!]
        {Source or block of sources}
    /header
        {Result includes REBOL header object (preempts /all)}
    /all ;-- renamed to all_LOAD to avoid conflict with ALL native
        {Load all values (does not evaluate REBOL header)}
    /type
        {Override default file-type; use NONE to always load as code}
        ftype [word! blank!]
            "E.g. text, markup, jpeg, unbound, etc."
] [
    ; Rename the /all refinement out of the way and put back lib/all (safer!)
    all_LOAD: all
    all: :lib/all

    file: line: void

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

    case/all [
        header [all_LOAD: _]

        ;-- Load multiple sources?
        block? source [
            return map-each item source [
                load/type/(all [header 'header])/(all [all_LOAD 'all])
                    item :ftype
            ]
        ]

        ;-- What type of file? Decode it too:
        match [file! url!] source [
            file: source
            line: 1

            sftype: file-type? source
            ftype: case [
                all [:ftype = 'unbound | :sftype = 'extension] [sftype]
                type [ftype]
            ] else [
                sftype
            ]
            data: read-decode source ftype
            if sftype = 'extension [return data]
        ]

        void? :data [data: source]

        ;-- Is it not source code? Then return it now:
        any [block? data | not find [0 extension unbound] any [:ftype 0]] [
            ; !!! "due to make-boot issue with #[none]" <-- What?
            return data ; directory, image, txt, markup, etc.
        ]

        ;-- Try to load the header, handle error:
        not all_LOAD [
            set [hdr: data:] either object? data [
                load-ext-module data
            ][
                load-header data
            ]
            if word? hdr [cause-error 'syntax hdr source]
        ]
        not set? 'hdr [hdr: _]
        ; data is binary or block now, hdr is object or blank

        ;-- Convert code to block, insert header if requested:
        not block? data [
            if string? data [
                data: to binary! data ;-- !!! inefficient, might be UTF8
            ]
            assert [binary? data]
            data: transcode/file/line data :file :line
            take/last data ;-- !!! always the residual, a #{}... why?
        ]

        header [insert data hdr]

        ;-- Bind code to user context:
        not any [
            | 'unbound = :ftype ;-- may be void
            | 'module = select hdr 'type
            | find select hdr 'options 'unbound
        ][
            data: intern data
        ]

        ;-- If appropriate and possible, return singular data value:
        not any [
            all_LOAD
            header
            empty? data
            1 < length of data
        ][
            data: first data
        ]
    ]

    :data
]


do-needs: function [
    "Process the NEEDS block of a program header. Returns unapplied mixins."
    needs [block! object! tuple! blank!]
        "Needs block, header or version"
    /no-share
        "Force module to use its own non-shared global namespace"
    /no-lib
        "Don't export to the runtime library"
    /no-user
        "Don't export to the user context (mixins returned)"
    /block
        "Return all the imported modules in a block, instead"
][
    ; NOTES:
    ; This is a low-level function and its use and return values reflect that.
    ; In user mode, the mixins are applied by IMPORT, so they don't need to
    ; be returned. In /no-user mode the mixins are collected into an object
    ; and returned, if the object isn't empty. This object can then be passed
    ; to MAKE module! to be applied there. The /block option returns a block
    ; of all the modules imported, not any mixins - this is for when IMPORT
    ; is called with a Needs block.

    case/all [
        ; If it's a header object:
        object? needs [needs: select needs 'needs] ; (protected)

        blank? needs [return blank]

        ; If simple version number check:
        tuple? needs [
            case [
                needs > system/version [
                    cause-error 'syntax 'needs reduce ['core needs]
                ]

                3 >= length of needs [ ; no platform id
                    blank
                ]

                (needs and+ 0.0.0.255.255)
                <> (system/version and+ 0.0.0.255.255) [
                    cause-error 'syntax 'needs reduce ['core needs]
                ]
            ]
            return blank
        ]

        ; If it's an inline value, put it in a block:
        not block? needs [needs: reduce [needs]]

        empty? needs [return blank]
    ]

    ; Parse the needs dialect [source <version> <checksum-hash>]
    mods: make block! length of needs
    name: vers: hash: _
    unless parse needs [
        here:
        opt [opt 'core set vers tuple! (do-needs vers)]
        any [
            here:
            set name [word! | file! | url! | tag!]
            set vers opt tuple!
            set hash opt binary!
            (join mods [name vers hash])
        ]
    ][
        cause-error 'script 'invalid-arg here
    ]

    ; Temporary object to collect exports of "mixins" (private modules).
    ; Don't bother if returning all the modules in a block, or if in user mode.
    if all [no-user not block] [
        ; Minimal length since it may persist later
        mixins: make object! 0
    ]

    ; Import the modules:
    mods: map-each [name vers hash] mods [
        ; Import the module
        mod: apply 'import [
            module: name

            version: true
            ver: opt vers

            check: true
            sum: opt hash

            no-share: no-share
            no-lib: no-lib
            no-user: no-user
        ]

        ; Collect any mixins into the object (if we are doing that)
        if all [any-value? :mixins | mixin? mod] [
            resolve/extend/only mixins mod select meta-of mod 'exports
        ]
        mod
    ]

    case [
        block [mods] ; /block refinement asks for block of modules
        not empty? to-value :mixins [mixins] ; else if any mixins, return them
        true [blank] ; return blank otherwise
    ]
]


load-ext-module: function [
    spec    [binary!]  "Spec for the module"
    impl    [handle!] "Native function implementation array"
    error-base [integer! blank!] "error base for the module"
    /unloadable
    /no-lib
    /no-user
][
    code: load/header decompress spec
    hdr: take code
    tmp-ctx: make object! [
        native: function [
            return: [function!]
            spec
            /export
                "this refinement is ignored here"
            /body
            code [block!]
                "Equivalent rebol code"
            <static>
            index (-1)
        ] compose [
            index: index + 1
            f: load-native/(all [body 'body])/(all [unloadable 'unloadable]) spec (impl) index :code
            :f
        ]
    ]
    mod: make module! (length of code) / 2
    set-meta mod hdr
    if errors: find code to set-word! 'errors [
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

    if hdr/name [
        reduce/into [
            hdr/name mod either hdr/checksum [copy hdr/checksum][blank]
        ] system/modules
    ]

    case [
        not module? mod [blank]

        not block? select hdr 'exports [blank]

        empty? hdr/exports [blank]

        find hdr/options 'private [
            ; full export to user
            unless no-user [
                resolve/extend/only system/contexts/user mod hdr/exports
            ]
        ]
    ] else [
        unless no-lib [
            resolve/extend/only system/contexts/lib mod hdr/exports
        ]
        unless no-user [
            resolve/extend/only system/contexts/user mod hdr/exports
        ]
    ]

    mod
]


load-module: function [
    {Loads a module and inserts it into the system module list.}
    source [word! file! url! string! binary! module! block!]
        {Source (file, URL, binary, etc.) or block of sources}
    /version ver [tuple!]
        "Module must be this version or greater"
    /check sum [binary!]
        "Match checksum (must be set in header)"
    /no-share
        "Force module to use its own non-shared global namespace"
    /no-lib
        "Don't export to the runtime library (lib)"
    /import
        "Do module import now, overriding /delay and 'delay option"
    /as
    name [word!]
        "New name for the module (not valid for reloads)"
    /delay
        "Delay module init until later (ignored if source is module!)"
][
    as_LOAD_MODULE: :as
    as: :lib/as

    ; NOTES:
    ;
    ; This is a variation of LOAD that is used by IMPORT. Unlike LOAD, the
    ; module init may be delayed. The module may be stored as binary or as an
    ; unbound block, then init'd later, as needed.
    ;
    ; The checksum applies to the uncompressed binary source of the body, and
    ; is calculated in LOAD-HEADER if the 'checksum header field is set.
    ; A copy of the checksum is saved in the system modules list for security.
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
    case [
        word? source [ ; loading the preloaded
            case/all [
                as_LOAD_MODULE [
                    cause-error 'script 'bad-refine /as ; no renaming
                ]

                ; Return blank if no module of that name found
                not tmp: find/skip system/modules source 3 [return blank]

                true [
                    ; get the module
                    ;
                    set [mod: modsum:] next tmp

                    ensure [module! block!] mod
                    ensure [binary! blank!] modsum
                ]

                ; If no further processing is needed, shortcut return
                all [not version | not check | any [delay module? :mod]] [
                    return reduce [source if module? :mod [mod]]
                ]
            ]
        ]
        binary? source [data: source]
        string? source [data: to binary! source]

        match [file! url!] source [
            tmp: file-type? source
            case [ ; Return blank if read or load-extension fails
                not tmp [
                    unless attempt [data: read source] [return blank]
                ]

                tmp = 'extension [
                    fail "Use LOAD or LOAD-EXTENSION to load an extension"
                ]
            ] else [
                cause-error 'access 'no-script source ; needs better error
            ]
        ]

        module? source [
            ; see if the same module is already in the list
            if tmp: find/skip next system/modules mod: source 3 [
                if as_LOAD_MODULE [
                    ; already imported
                    cause-error 'script 'bad-refine /as
                ]

                if all [
                    ; not /version, not /check, same as top module of that name
                    not version
                    not check
                    same? mod select system/modules pick tmp 0
                ][
                    return copy/part back tmp 2
                ]

                set [mod: modsum:] tmp
            ]
        ]

        block? source [
            if any [version check as] [
                cause-error 'script 'bad-refines blank
            ]

            data: make block! length of source

            unless parse source [
                any [
                    tmp:
                    set name opt set-word!
                    set mod [word! | module! | file! | url! | string! | binary!]
                    set ver opt tuple!
                    set sum opt binary! ; ambiguous
                    (join data [mod ver sum if name [to word! name]])
                ]
            ][
                cause-error 'script 'invalid-arg tmp
            ]

            return map-each [mod ver sum name] source [
                apply 'load-module [
                    source: mod
                    version: version
                    ver: :ver
                    check: :check
                    sum: :sum
                    as: true
                    name: opt name
                    no-share: no-share
                    no-lib: no-lib
                    import: import
                    delay: delay
                ]
            ]
        ]
    ]

    case/all [
        ; Get info from preloaded or delayed modules
        void? :mod [mod: _]
        module? mod [
            delay: no-share: _ hdr: meta-of mod
            ensure [block! blank!] hdr/options
        ]
        block? mod [set* [hdr: code:] mod]

        ; module/block mod used later for override testing

        ; Get and process the header
        void? :hdr [
            ; Only happens for string, binary or non-extension file/url source
            set [hdr: code:] load-header/required data
            case [
                word? hdr [cause-error 'syntax hdr source]
                import [
                    ; /import overrides 'delay option
                ]
                not delay [delay: did find hdr/options 'delay]
            ]
            if hdr/checksum [modsum: copy hdr/checksum]
        ]
        no-share [
            hdr/options: append any [hdr/options make block! 1] 'isolate
        ]

        ; Unify hdr/name and /as name
        any-value? :name [hdr/name: name] ; rename /as name
        void? :name [name: :hdr/name]
        all [not no-lib not word? :name] [ ; requires name for full import
            ; Unnamed module can't be imported to lib, so /no-lib here
            no-lib: true  ; Still not /no-lib in IMPORT

            ; But make it a mixin and it will be imported directly later
            unless find hdr/options 'private [
                hdr/options: append any [hdr/options make block! 1] 'private
            ]
        ]
        not tuple? set* 'modver :hdr/version [
            modver: 0.0.0 ; get version
        ]

        ; See if it's there already, or there is something more recent
        all [
            ; set to false later if existing module is used
            | override?: not no-lib
            | set [name0: mod0: sum0:] pos: find/skip system/modules name 3
        ] [
            ; Get existing module's info
            case/all [
                module? :mod0 [hdr0: meta-of mod0] ; final header
                block? :mod0 [hdr0: first mod0] ; cached preparsed header

                true [
                    ensure word! name0
                    ensure object! hdr0
                    ensure [binary! blank!] sum0
                ]

                not tuple? ver0: :hdr0/version [ver0: 0.0.0]
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
                        modver: ver0 | modsum: sum0
                        override?: false
                    ]
                ]

                ; else is delayed module
                ver0 > modver [ ; and it's newer, use it instead
                    mod: _ set [hdr code] mod0
                    modver: ver0 | modsum: sum0
                    ext: all [(object? code) code] ; delayed extension
                    override?: not delay  ; stays delayed if /delay
                ]
            ]
        ]

        not module? mod [
            mod: _ ; don't need/want the block reference now
        ]

        ; Verify /check and /version
        all [check sum !== modsum] [
            cause-error 'access 'invalid-check module
        ]
        all [version ver > modver] [
            cause-error 'syntax 'needs reduce [name ver]
        ]

        ; If no further processing is needed, shortcut return
        all [not override? any [mod delay]] [return reduce [name mod]]

        ; If /delay, save the intermediate form
        delay [mod: reduce [hdr either object? ext [ext] [code]]]

        ; Else not /delay, make the module if needed
        not mod [
            ; not prebuilt or delayed, make a module
            case/all [
                find hdr/options 'isolate [no-share: true] ; in case of delay

                object? code [ ; delayed extension
                    set [hdr: code:] load-ext-module code
                    hdr/name: name ; in case of delayed rename
                    if all [no-share not find hdr/options 'isolate] [
                        hdr/options: append any [hdr/options make block! 1] 'isolate
                    ]
                ]

                binary? code [code: to block! code]
            ]

            ensure object! hdr
            ensure block! code

            mod: catch/quit [
                module/mixin hdr code (opt do-needs/no-user hdr)
            ]
        ]

        all [not no-lib override?] [
            unless any-value? :modsum [modsum: _]
            case/all [
                pos [pos/2: mod pos/3: modsum] ; replace delayed module

                not pos [reduce/into [name mod modsum] system/modules]

                all [module? mod not mixin? hdr block? select hdr 'exports] [
                    resolve/extend/only lib mod hdr/exports ; no-op if empty
                ]
            ]
        ]
    ]

    reduce [name if module? mod [mod]]
]


import: function [
    ; See also: sys/make-module*, sys/load-module, sys/do-needs

    "Imports a module; locate, load, make, and setup its bindings."
     module [word! file! url! string! binary! module! block! tag!]
    /version ver [tuple!]
        "Module must be this version or greater"
    /check sum [binary!]
        "Match checksum (must be set in header)"
    /no-share
        "Force module to use its own non-shared global namespace"
    /no-lib
        "Don't export to the runtime library (lib)"
    /no-user
        "Don't export to the user context"
][
    if tag? module [
        if trap? [
            module: first tmp: select load rebol/locale/library/modules module
        ][
            cause-error 'access 'cannot-open reduce
            either blank? tmp [
                [module "module not found in system/locale/library/modules"]
            ][
                [module "error occurred in loading module from system/locale/library/modules"]
            ]
        ]
    ]
    ; If it's a needs dialect block, call DO-NEEDS/block:
    if block? module [
        assert [not version not check] ; these can only apply to one module
        return apply 'do-needs [
            needs: module
            no-share: :no-share
            no-lib: :no-lib
            no-user: :no-user
            block: true
        ]
    ]

    ; Note: IMPORT block! returns a block of all the modules imported.

    ; Try to load and check the module.
    ; !!! the original code said /import, not conditional on refinement
    set [name: mod:] apply 'load-module [
        source: module
        version: version
        ver: :ver
        check: check
        sum: :sum
        no-share: no-share
        no-lib: no-lib
        import: true
    ]

    case [
        mod [
            ; success!
        ]

        word? module [
            ; Module (as word!) is not loaded already, so let's try to find it.
            file: append to file! module system/options/default-suffix

            for-each path system/options/module-paths [
                if set [name: mod:] (
                    apply 'load-module [
                        source: path/:file
                        version: version
                        ver: :ver
                        check: check
                        sum: :sum
                        no-share: :no-share
                        no-lib: :no-lib
                        import: true
                    ]
                ) [
                    break
                ]
            ]
        ]

        any [file? module | url? module] [
            cause-error 'access 'cannot-open reduce [module "not found or not valid"]
        ]
    ]

    unless mod [
        cause-error 'access 'cannot-open reduce [module "module not found"]
    ]

    ; Do any imports to the user context that are necessary.
    ; The lib imports were handled earlier by LOAD-MODULE.
    case [
        any [
            | no-user
            | not block? exports: select hdr: meta-of mod 'exports
            | empty? exports
        ][
            ; Do nothing if /no-user or no exports.
        ]

        any [
            no-lib
            find select hdr 'options 'private ; /no-lib causes private
        ][
            ; It's a private module (mixin)
            ; we must add *all* of its exports to user

            resolve/extend/only system/contexts/user mod exports
        ]

        ; Unless /no-lib its exports are in lib already
        ; ...so just import what we need.
        not no-lib [resolve/only system/contexts/user lib exports]
    ]

    mod ; module! returned
]


load-extension: function [
    file [file! handle!]
        "library file or handle to init function in the builtin extension"
    /no-user
        "Do not export to the user context"
    /no-lib
        "Do not export to the lib context"
][
    ext: load-extension-helper file

    if locked? ext [; already loaded
        return ext
    ]
    case [
        ; !!! This used to treat BINARY! scripts as compressed and STRING!
        ; as uncompressed, but it used byte-oriented data to back the STRING!
        ; which temporarily requires UTF-8 to wide string expansion.  Hence
        ; decompression is done in the C, and all are assumed to be UTF8
        ; binary for the moment.  This can do a usermode decompress after
        ; UTF-8 everywhere is implemented, because byte-oriented UTF-8 will
        ; be a legal string series.
        ;
        string? ext/script [
            fail "STRING! ext/script shouldn't happen right now (temporary)"
            script: load/header ext/script
        ]
        binary? ext/script [
            comment [
                script: load/header decompress ext/script
            ]
            script: load/header ext/script
        ]
    ] else [
        ; ext/script should ALWAYS be set by the extension but if it's not,
        ; do not fail, because failing to load a builtin extension could
        ; cause the interpreter to fail to boot
        ;
        script: reduce [construct system/standard/header []]
    ]

    ext/script: _ ;clear the startup script to save memory
    ext/header: take script
    modules: make block! 1
    for-each [spec impl error-base] ext/modules [
        append modules apply 'load-ext-module [
            spec: spec
            impl: impl
            error-base: error-base
            unloadable: true
            no-user: no-user
            no-lib: no-lib
        ]
    ]

    ext/modules: modules
    if blank? ext/header/type [
        ext/header/type: 'extension
    ]

    append system/extensions ext

    ;run the startup script
    do script

    lock ext/header
    lock ext

    ext
]


unload-extension: procedure [
    ext [object!] "extension object"
][

    ;sanity checking
    unless locked? ext [
        fail "Extension is not locked"
    ]
    unless all [
        library? ext/lib-base
        file? ext/lib-file
    ][
        fail "Can't unload a builtin extension"
    ]

    remove find system/extensions ext
    for-each m ext/modules [
        remove/part back find system/modules m 3
        ;print ["words of m:" words of m]
        for-each w words of m [
            v: get w
            if all [function? :v 1 = func-class-of :v] [
                unload-native :v
            ]
        ]
    ]
    unload-extension-helper ext
]


export [load import load-extension unload-extension]
