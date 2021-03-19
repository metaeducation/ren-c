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


; !!! R3-Alpha Module loading had the ability to be delayed.  This was so
; that special modules like CGI protocols or HTML formatters could be available
; in the system/modules list...held as just headers and the BINARY! or BLOCK!
; that would be used to initialize them.  The feature obfuscated more
; foundational design points, so was temporarily removed...but should be
; brought back once the code solidifies.



load-header: function [
    {Loads script header object and body binary (not loaded).}

    return: "header OBJECT! if present, or error WORD!"
        [<opt> object! word!]
    body: "<output>"
        [binary! block!]
    line: "<output>"
        [integer!]
    final: "<output>"
        [<output> binary!]

    source "Source code (text! will be UTF-8 encoded)"
        [binary! text!]
    /file "Where source is being loaded from"
        [file! url!]
    /only "Only process header, don't decompress body"
    /required "Script header is required"

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

    let [key rest]: transcode/file/line data file 'line

    ; get header block

    let [hdr 'rest error]: transcode/file/line rest file 'line

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

    if find hdr/options just content [
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
            find hdr/options just compress [
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

        data: transcode data  ; decode embedded script
        rest: skip data 2  ; !!! what is this skipping ("hdr/length" ??)

        if find hdr/options just compress [  ; script encoded only
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

    return: "BLOCK! if Rebol code, otherwise value(s) appropriate for codec"
        [any-value!]
    header: "<output> Request the Rebol header object be returned as well"
        [object!]

    source "Source of the information being loaded"
        [file! url! text! binary!]
    /type "E.g. rebol, text, markup, jpeg... (by default, auto-detected)"
        [word!]
][
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

    if not find [unbound rebol] ^type [
        if find system/options/file-types ^type [
            return decode type :data
        ]

        fail ["No" type "LOADer found for" type of source]
    ]

    ensure [text! binary!] data

    if block? data [
        return data  ; !!! Things break if you don't pass through; review
    ]

    ; Try to load the header, handle error

    let [hdr 'data line]: load-header/file data file

    if word? hdr [cause-error 'syntax hdr source]

    ensure [blank! object!] hdr: default [_]
    ensure [binary! block! text!] data

    ; Convert code to block, insert header if requested

    if not block? data [
        assert [match [binary! text!] data]  ; UTF-8
        data: transcode/file/line data file 'line
    ]

    ; Bind code to user context

    all .not [
        'unbound = type
        'module = select hdr 'type
        find (try get 'hdr/options) [unbound]
    ] then [
        data: intern data system/contexts/user
    ]

    if header [
        set header opt hdr
    ]
    return :data
]

load-value: redescribe [
    {Do a LOAD of a single value}
](
    chain [
        :load
            |
        func [x] [
            assert [block? x]
            if 1 <> length of x [
                fail ["LOAD-VALUE got length" length of x "block, not 1"]
            ]
            first x
        ]
    ]
)


; This is code that is reused by both IMPORT and LOAD-EXTENSION.
;
; We don't want to load the same module more than once.  This is standard
; in languages like Python and JavaScript:
;
; https://dmitripavlutin.com/javascript-module-import-twice
; https://stackoverflow.com/q/19077381/
;
; This means there needs to be some way of knowing a "key" for the module
; without running it.  Unfortunately we can't tell the key from just the
; information given as the "source"...so it might require an HTTP fetch of
; a URL! or reading from a file.  But we could notice when the same filename
; or URL was used.
;
; !!! Previously this had the ability to /DELAY running the module; which
; could hold it even as a BINARY! and wait until it was needed.  This was not
; fully realized, and has been culled to try and get more foundational
; features (e.g. binding) working.
;
load-module: func [
    {Inserts a module into the system module list (creating if necessary)}

    return: [<opt> module!]
    source {Source (file, URL, binary, etc.) or name of already loaded module}
        [module! file! url! text! binary! word!]
    /into "Load into an existing module (e.g. populated with some natives)"
        [module!]
    /exports "Add exports on top of those in the EXPORTS: section"
        [block!]
][
    if module? source [  ; Register only, don't create
        let name: noquote (meta-of source)/name

        ; The module they're passing in may or may not have a name.  If it
        ; has a name, we want to add it to the module list if it's not
        ; already there.
        ;
        if not name [
            return source
        ]
        let mod: select/skip system/modules name 2 else [
            append system/modules reduce [name source]
            return source
        ]
        if mod != source [
            print mold meta-of mod
            print mold meta-of source
            fail ["Conflict: more than one module instance named" name]
        ]
        return source
    ]

    let data: null
    let [hdr code line]

    ; Figure out the name of the module desired, by examining the header

    let file: match [file! url!] source  ; used for file/line info during scan

    let name: match word! source else [
        data: match [binary! text!] source else [read source]

        [hdr code line]: load-header/file/required data file
        name: noquote hdr/name
    ]

    ; See if a module by that name is already loaded, and return it if so

    let mod: select/skip system/modules name 2 then [
        return mod
    ]

    if not data [return null]  ; If source was a WORD!, can't fallback on load

    code: transcode/line/file code line file

    ensure object! hdr
    ensure block! code

    ; !!! The /EXPORTS parameter allows the adding of more exports, it's used
    ; by the extension mechanism when it sees "export" in the spec on a native.
    ; That doesn't correspond to an actual execution of an export command.
    ; This code added it to the header, but it's not clear if the header
    ; needs to be the canon list of exports.

    if exports [
        if null? select hdr 'exports [
            append hdr compose [exports: (exports)]
        ] else [
            append exports hdr/exports
            hdr/exports: exports
        ]
    ]

    catch/quit [
        mod: module/into hdr code into
    ]

    append system/modules reduce [name, ensure module! mod]

    return mod
]

; If TRUE, IMPORT 'MOD acts as IMPORT <MOD>
;
force-remote-import: false

; See also: SYS/MAKE-MODULE*, SYS/LOAD-MODULE
;
import: function [
    {Imports a module; locate, load, make, and setup its bindings}

    return: "Loaded module"
        [<opt> module!]
    source [word! file! url! text! binary! module! tag!]
][
    old-force-remote-import: force-remote-import
    ; `import <name>` will look in the module library for the "actual"
    ; module to load up, and drop through.
    ;
    ; if a module is loaded with `import <mod1>`
    ; then every nested `import 'mod2`
    ; is forced to `import <mod2>`
    ;
    if tag? source [set 'force-remote-import true]
    if all [force-remote-import, word? source] [
        source: to tag! source
    ]
    if tag? source [
        tmp: (select load system/locale/library/modules source) else [
            cause-error 'access 'cannot-open reduce [
                module "module not found in system/locale/library/modules"
            ]
        ]

        source: (first tmp) else [
            cause-error 'access 'cannot-open reduce [
                module "error occurred in loading module"
                    "from system/locale/library/modules"
            ]
        ]
    ]

    mod: load-module source

    case [
        mod [
            ; success!
        ]

        word? source [
            ;
            ; Module (as word!) is not loaded already, so try to find it.
            ;
            file: append to file! module system/options/default-suffix

            for-each path system/options/module-paths [
                if mod: load-module join path file [  ; Note: %% not defined yet
                    break
                ]
            ]
        ]

        match [file! url!] source [
            cause-error 'access 'cannot-open reduce [
                source "not found or not valid"
            ]
        ]
    ]

    if not mod [
        cause-error 'access 'cannot-open reduce [source "module not found"]
    ]

    ; !!! Previously the idea was that HDR/EXPORTS would go into lib.  Many
    ; modules are manually using EXPORT at the moment to do this, because
    ; modules were so exasperating.  The new idea is that exports would only
    ; be given to those who imported the features.
    ;
    ; !!! LIB imports handled should be implicitly picked up by virtue of
    ; inheritance of the lib context.
    ;
    ; !!! The idea of `import *` is frowned upon as a practice, as it adds
    ; an unknown number of things to the namespace of the caller.  Most
    ; languages urge you not to do it, but JavaScript bans it entirely.  We
    ; should probably ban it as well.  But do it for the moment since that
    ; is how it has worked in the past.
    ;
    if let exports: select (meta-of mod) 'exports [
        resolve/extend/only lib mod try exports  ; no-op if empty
    ]

    set 'force-remote-import old-force-remote-import
    return ensure module! mod
]


export [load load-value import]
