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
; in the SYSTEM.MODULES list...held as just headers and the BINARY! or BLOCK!
; that would be used to initialize them.  The feature obfuscated more
; foundational design points, so was temporarily removed...but should be
; brought back once the code solidifies.


transcode-header: func [
    {Try to match a data binary! as being a script, definitional fail if not}

    return: [<opt> block!]
    @rest [<opt> binary!]
    @line [integer!]

    data [binary!]
    /file [file! url!]

    <local> key hdr
][
    line: 1
    [key /rest]: transcode/one/file/line data file line except e -> [  ; "REBOL"
        return raise e
    ]
    if not rest [
        return null
    ]
    [hdr /rest]: transcode/one/file/line rest file line except e -> [  ; BLOCK!
        return raise e
    ]

    return all [key = 'REBOL, block? hdr] then [hdr]
]


; This function decodes the script header from the script body.  It checks the
; header 'compress and 'content options, and supports length-specified  or
; script-in-a-block embedding.
;
; It will set the 'content field to the binary source if 'content is true.
; The 'content will be set to the source at the position of the beginning of
; the script header, skipping anything before it. For multi-scripts it doesn't
; copy the portion of the content that relates to the current script, or at
; all, so be careful with the source data you get.
;
; If the 'compress option is set then the body will be decompressed.  Binary
; vs. script encoded compression will be autodetected.
;
; Normally, returns the header object, the body text (as binary), and the
; end of the script or script-in-a-block. The end position can be used to
; determine where to stop decoding the body text. After the end is the rest of
; the binary data, which can contain anything you like. This can support
; multiple scripts in the same binary data ("multi-scripts").
;
; If not /ONLY and the script is embedded in a block and not compressed then
; the body text will be a decoded block instead of binary, to avoid the
; overhead of decoding the body twice.
;
; Syntax errors are returned as words:
;    no-header
;    bad-header
;    bad-compress
;
load-header: function [
    {Loads script header object and body binary (not loaded)}

    return: "header OBJECT! if present"
        [<opt> object!]
    @body [binary! text!]
    @line [integer!]
    @final [binary!]
    source "Source code (text! will be UTF-8 encoded)"
        [binary! text!]
    /file "Where source is being loaded from"
        [file! url!]
    /only "Only process header, don't decompress body"
    /required "Script header is required"

    <static>
    non-ws (make bitset! [not 1 - 32])
][
    line: 1

    let data: as binary! source  ; if it's not UTF-8, decoding provides error

    ; The TRANSCODE function convention is that the LINE OF is the line number
    ; of the *end* of the transcoding so far, (to sync line numbering across
    ; multiple transcodes)

    === TRY TO MATCH PATTERN OF "REBOL [...]" ===

    let [hdr rest 'line]: transcode-header/file data file except e -> [
        return raise e  ; TRANSCODE choked, wasn't valid at all
    ]

    if not hdr [
        ;
        ; TRANSCODE didn't detect REBOL [...], but it didn't see anything it
        ; thought was invalid Rebol tokens either.
        ;
        if required [
            return raise "no-header"
        ]
        body: data
        final: tail of data
        return null
    ]

    hdr: construct/with/only hdr system.standard.header except [
        return raise "bad-header"
    ]

    (match [<opt> block!] hdr.options) else [
        return raise "bad-header"
    ]

    if find maybe hdr.options 'content [
        append hdr spread compose [content (data)]  ; as of start of header
    ]

    if 10 = rest.1 [rest: next rest, line: me + 1]  ; skip LF

    if integer? tmp: select hdr 'length [
        end: skip rest tmp
    ] else [
        end: tail of data
    ]

    if only [  ; when it's /ONLY, decompression is not performed
        body: rest
        final: end
        return hdr
    ]

    let binary
    if true [  ; was `if key = 'REBOL`, how was that ever not true?
        ;
        ; !!! R3-Alpha apparently used a very bad heuristic of attempting to
        ; decompress garbage (likely asking for a very big memory allocation
        ; and failing), and trapped it to see if it failed.  It did those
        ; traps with ATTEMPT.  ATTEMPT no longer glosses over such things.
        ;
        ; This feature needs redesign if it's to be kept, but switching it
        ; to use RESCUE with the bad idea for now.
        ;
        if find maybe hdr.options 'compress [
            any [
                not error? sys.util.rescue [
                    ; Raw bits.  whitespace *could* be tolerated; if
                    ; you know the kind of compression and are looking
                    ; for its signature (gzip is 0x1f8b)
                    ;
                    rest: gunzip/part rest end
                ]
                not error? sys.util.rescue [  ; e.g. not error
                    ; BINARY! literal ("'SCRIPT encoded").  Since it
                    ; uses transcode, leading whitespace and comments
                    ; are tolerated before the literal.
                    ;
                    [binary rest]: transcode/one/file/line rest file 'line
                    rest: gunzip binary
                ]
            ]
            else [
                return raise "bad-compress"
            ]
        ]
    ] else [
        ; block-embedded script, only script compression

        data: transcode data  ; decode embedded script
        rest: skip data 2  ; !!! what is this skipping ("hdr.length" ??)

        if find maybe hdr.options 'compress [  ; script encoded only
            rest: attempt [gunzip first rest] else [
                return raise "bad-compress"
            ]
        ]
    ]

    body: ensure [binary! text!] rest
    ensure integer! line
    final: ensure [binary! text!] end

    ensure object! hdr
    ensure [<opt> block! blank!] hdr.options
    return hdr
]


load: func [
    {Loads code or data from a file, URL, text string, or binary.}

    return: "BLOCK! if Rebol code, otherwise value(s) appropriate for codec"
        [<opt> any-value!]
    @header "Request the Rebol header object be returned as well"
        [<opt> object!]
    source "Source of the information being loaded"
        [<maybe> file! url! tag! the-word! text! binary!]
    /type "E.g. rebol, text, markup, jpeg... (by default, auto-detected)"
        [word!]

    <local> file line data
][
    if match [file! url! tag! the-word!] source [
        source: clean-path source

        file: ensure [file! url!] source
        type: default [file-type? source else ['rebol]]  ; !!! rebol default?

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
            header: null
            return data
        ]
    ]
    else [
        file: null
        data: source
        type: default ['rebol]
    ]

    if type = 'extension [
        fail "Use LOAD-EXTENSION to load extensions (at least for now)"
    ]

    if not find [unbound rebol] type [
        if find system.options.file-types type [
            header: null
            return decode type :data
        ]

        fail ["No" type "LOADer found for" kind of source]
    ]

    ensure [text! binary!] data

    [header data line]: load-header/file data file except e -> [return raise e]

    if word? header [cause-error 'syntax header source]

    ensure [<opt> object!] header
    ensure [binary! block! text!] data

    ; Convert code to block

    if not block? data [
        assert [match [binary! text!] data]  ; UTF-8
        data: transcode/file/line data file 'line except e -> [return raise e]
    ]

    ; Bind code to user context

    all [
        'unbound != type
        'module != select maybe header 'type
        not find maybe (select maybe header 'options) [unbound]
    ] then [
        data: intern* system.contexts.user data
    ]

    return data
]

load-value: redescribe [
    {Do a LOAD of a single value}
](
    chain [
        :load,
        lambda [^x [<fail> block!]] [
            either raised? unmeta x [
                unmeta x  ; pipe error
            ][
                x: unmeta x
                either 1 = length of ensure block! x [
                    first x
                ][
                    raise ["LOAD-VALUE got length" length of x "block, not 1"]
                ]
            ]
        ]
    ]
)


; Some URLs that represent executable code have a HTML presentation layer on
; them.  This is why a GitHub link has a "raw" offering without all that extra
; stuff on it (line numbers, buttons, etc.)
;
; We don't want to hook at the READ level to redirect those UI pages to give
; back the raw data...because you might want to READ and process the UI
; decorations!  But if you ask to IMPORT or DO such a page, it's reasonable to
; assume what you actually wanted was to DO the raw content implied by it.
;
; This performs that forwarding for GitLab and GitHub UI links.  The JS
; interop routines (like JS-DO and CSS-DO) want the same thing, and use
; this routine as well.

adjust-url-for-raw: func [
    return: [<opt> url!]
    url [<maybe> url!]
][
    let text: to text! url  ; URL! may become immutable, try thinking ahead

    parse text [
        "http" try "s" "://gitlab.com/"
        thru "/"  ; user name
        thru "/"  ; repository name
        try "-/"  ; mystery thing (see remarks on CORSify-gitlab-port)
        change "blob/" ("raw/")
        to <end>
    ] then [
        return as url! text  ; The port will CORSIFY at a lower level
    ]

    ; Adjust a decorated GitHub UI to https://raw.githubusercontent.com
    let start
    parse text [
        "http" try "s" "://github.com/"
        start: <here>
        thru "/"  ; user name
        thru "/"  ; repository name
        change "blob/" ("")  ; GitHub puts the "raw" in the subdomain name
        to <end>
    ] then [
        return as url! unspaced [
            https://raw.githubusercontent.com/ start
        ]
    ]

    ; Adjust a Github Gist URL to https://gist.github.com/.../raw/
    parse text [
        "http" try "s" "://gist.github.com/"
        start: <here>
        thru "/"  ; user name
        [
            to "#file="
            remove to <end>  ; ignore file for now, id does not match filename
            |
            to <end>
        ]
        insert ("/raw/")
    ] then [
        return as url! unspaced [
            https://gist.githubusercontent.com/ start
        ]
    ]

    return null
]


; While DO can run a script any number of times with fresh variables on each
; run, we don't want to IMPORT the same module more than once.  This is
; standard in languages like Python and JavaScript:
;
; https://dmitripavlutin.com/javascript-module-import-twice
; https://stackoverflow.com/q/19077381/
;
; This means there needs to be some way of knowing a "key" for the module
; without running it.  Unfortunately we can't tell the key from just the
; information given as the "source"...so it might require an HTTP fetch of
; a URL! or reading from a file.  But we could notice when the same filename
; or URL was used.  We should also use hashes to tell when things change.
;
import*: func [
    {Imports a module; locate, load, make, and setup its bindings}

    return: "Loaded module"
        [<opt> module!]
    @product' "Evaluative product of module body (only if WHERE is BLANK!)"
        [<opt> any-value!]
    where "Where to put exported definitions from SOURCE"
        [<opt> module!]
    source [
        file! url!  ; get from location, run with location as working dir
        tag!  ; load relative to system.script.path
        the-word!  ; look up as a shorthand in registry
        binary!  ; UTF-8 source, needs to be checked for invalid byte patterns
        text!  ; source internally stored as validated UTF-8, *may* scan faster
        word!  ; not entirely clear on what WORD! does.  :-/
        module!  ; register the module and import its exports--do not create
    ]
    /args "Args passed as system.script.args to a script (normally a string)"
        [any-value!]
    /only "Do not catch quits...propagate them"
        [logic!]
    /into "e.g. reuse Context(*) already made for NATIVEs loading from extension"
        [module!]
    <static>
        importing-remotely (false)
][
    return: adapt :return [  ; make sure all return paths actually import vars
        ;
        ; Note: `value` below is the argument to RETURN.  It is a ^META
        ; parameter so should be a quoted module.  We don't disrupt that, else
        ; falling through to RETURN would get tripped up.
        ;
        ; !!! The idea of `import *` is frowned upon as a practice, as it adds
        ; an unknown number of things to the namespace of the caller.  Most
        ; languages urge you not to do it, but JavaScript bans it entirely.  We
        ; should maybe ban it as well (or at least make it inconvenient).  But
        ; do it for the moment since that is how it has worked in the past.
        ;
        ensure module! unmeta value
        if where [
            let exports: select (maybe meta-of unmeta value) 'exports
            proxy-exports where (unmeta value) (maybe exports)
        ]
    ]

    === IF MODULE ALREADY CREATED, REGISTER AND RESOLVE IMPORTED VARS ===

    if module? source [
        assert [not into]  ; ONLY isn't applicable unless scanning new source

        let name: (meta-of source).name else [
            product': ~nameless~
            return source  ; no name, so just do the RESOLVE to get variables
        ]
        let mod: (select/skip system.modules name 2) else [
            append system.modules spread :[name source]  ; not in mod list, add
            product': ~registered~
            return source
        ]
        if mod != source [
            fail ["Conflict: more than one module instance named" name]
        ]
        product': ~cached~
        return source
    ]


    === ADJUST URL FROM HTML PRESENTATION TO RAW IF SUPPORTED ===

    ; If URL is decorated source (syntax highlighting, etc.) get raw form.
    ;
    (adjust-url-for-raw maybe match url! :source) then adjusted -> [
        source: adjusted  ; !!! https://forum.rebol.info/t/1582/6
    ]


    === TREAT (IMPORT 'FILENAME) AS REQUEST TO LOOK LOCALLY FOR FILENAME.R ===

    ; We don't want remote execution of a module via `import <some-library>`
    ; to be able to turn around and run code locally.  So during a remote
    ; import, any WORD!-style imports like `import 'mod2` are turned into
    ; `import @mod2`.
    ;
    let old-importing-remotely: importing-remotely
    if all [importing-remotely, word? source] [
        source: to the-word! source
    ]

    if word? source [
        let file
        for-each path system.options.module-paths [
            file: join path spread reduce [source system.options.default-suffix]
            if not exists? file [
                continue
            ]
            source: file
            break
        ]

        (file? source)
        or (fail ["Could not find any" file "in SYSTEM.OPTIONS.MODULE-PATHS"])
    ]

    === GET DIRECTORY PORTION OF SOURCE PATH FOR NEW SYSTEM.SCRIPT.PATH  ===

    ; A relative path may have contributed some of its own directory portions,
    ; so extract the net path where we're executing to save in system.script.

    let dir: null
    match [file! url! the-word! tag!] source then [
        source: clean-path source
        dir: as text! source
        let [before file]: find-last dir slash
        assert [before]
        dir: as (kind of source) copy/part dir file
    ]

    if url? source [
        importing-remotely: true
    ]

    === LOAD JUST THE HEADER FOR EXAMINATION, TO FIND THE NAME ===

    ; There may be a `Name:` field in the header.  Historically this was used
    ; to tell if an already loaded version of the module was loaded...but this
    ; doesn't account for potential variations (maybe loading different hashes
    ; from different locations, or if the file has changed?)
    ;
    ; !!! Noticing changes would be extremely helpful by not forcing you to
    ; quit and restart to see module changes.  This suggests storing a hash.

    let file: match [file! url!] source  ; used for file/line info during scan

    let data: match [binary! text!] source else [read source]

    let [hdr code line]: load-header/file data file
    if not hdr [
        if where [  ; not just a DO
            fail ["IMPORT requires a header on:" (any [file, "<source>"])]
        ]
    ]

    let name: select maybe hdr 'name
    (select/skip system.modules maybe name 2) then cached -> [
        product': ~cached~
        return cached
    ]

    ensure [<opt> object!] hdr

    let is-module: hdr and ('module = select hdr 'type)

    === MAKE SCRIPT CHARACTERIZATION OBJECT AND CALL PRE-SCRIPT HOOK ===

    ; The header object doesn't track instance information like the args to
    ; a script, or what file path it was executed from.  And we probably don't
    ; want to inject that in the header.  So SYSTEM.STANDARD.SCRIPT is the
    ; base object for gathering these other instantiation-related properties
    ; so the running script can know about them.

    let original-script: system.script

    system.script: make system.standard.script compose [
        title: select maybe hdr 'title
        header: hdr
        parent: original-script
        path: dir
        args: (^ :args)  ; variable same name as field, trips up binding
    ]

    if (set? 'script-pre-load-hook) and (match [file! url!] source) [
        ;
        ; !!! It seems we could/should pass system.script here, and the
        ; filtering of source as something not to notify about would be the
        ; decision of the hook.
        ;
        ; !!! Should there be a post-script-hook?
        ;
        script-pre-load-hook/ [is-module hdr]
    ]

    === CHANGE WORKING DIRECTORY TO MODULE'S DIRECTORY (IF IT'S A MODULE) ===

    ; When code is run with DO or on the command line, it is considered to be
    ; a "script".  It can be instantiated multiple times and passed arguments.
    ; These arguments may include relative file paths like %foo.txt, where the
    ; notion of where they are relative to is in the worldview of the person
    ; who invoked DO.  The best way of letting the script stay in that view
    ; is to leave the current working directory where it is.
    ;
    ; But when you IMPORT a module, this module is imported once even if it is
    ; invoked from multiple locations.  Hence one should not rely on side
    ; effects of an IMPORT, nor be passing it arguments (as no code might run
    ; at all on the IMPORT).  So the only real sensible notion for a "current
    ; directory" would be the directory of the module.
    ;
    ; (The notation of `import @some-module.r` is provided to request a path
    ; be looked up relative to `system.script.path`.  But %xxx.r is always
    ; looked up relative to the current directory.)

    let original-path: what-dir
    if where and dir [  ; IMPORT
        change-dir dir
    ]

    === EXECUTE SCRIPT BODY ===

    ; This routine is attempting to merge two distinct codebases: IMPORT
    ; and DO.  They have common needs for looking up <tag> shorthands to make
    ; URLs, or to cleanly preserve the directories, etc.  But the actual
    ; execution is different: modules are imported only once and the body
    ; result is discarded, while DO can run multiple times and the body result
    ; is returned as the overall return for the DO.
    ;
    ; !!! None of this is perfect...but it's much better than what had come
    ; from the unfinished R3-Alpha module system, and its decade of atrophy
    ; that happened after that...

    let [mod 'product' quitting]: module/into/file/line hdr code into file line

    ensure module! mod

    if is-module and name [
        append system.modules spread :[name mod]
    ]

    === RESTORE SYSTEM.SCRIPT AND THE DIRECTORY IF THEY WERE CHANGED ===

    if original-script [system.script: original-script]
    if original-path [change-dir original-path]

    importing-remotely: old-importing-remotely

    === PROPAGATE QUIT IF REQUESTED, OR RETURN MODULE ===

    if quitting and only [
        quit/with unmeta product'  ; "rethrow" the QUIT if DO/ONLY
    ]

    return mod
]


export*: func [
    {Add words to module's `Exports: []` list}

    return: "Evaluated expression if used with SET-WORD!"
        [<opt> any-value!]
    where "Specialized for each module via EXPORT"
        [module!]
    'left [<skip> set-word! set-group!]
    args "`export x: ...` for single or `export [...]` for words list"
        [<opt> any-value! <variadic>]
    <local>
        hdr exports val word types items
][
    hdr: meta-of where
    exports: ensure block! select hdr 'Exports

    if left [
        if set-group? left [
            left: ^ eval left
            case [
                void' = left [word: null]
                any-word? unmeta left [word: as word! unmeta left]
                fail "EXPORT of SET-GROUP! must be VOID or ANY-WORD!"
            ]
        ] else [
            word: as word! left
        ]
        return (
            (maybe word): take args
            elide if word [append exports word]
        )
    ]

    items: ^ take args
    if group? unmeta items [items: do unmeta items]
    if not block? unmeta items [
        fail "EXPORT must be of form `export x: ...` or `export [...]`"
    ]
    items: unmeta items

    while [not tail? items] [
        val: get/any word: match word! items.1 else [
            fail ["EXPORT only accepts WORD! or WORD! [typeset], not" ^items.1]
        ]
        ; !!! notation for exporting isotopes?
        items: next items

        (types: match block! items.1) then [
            (match types val) else [
                fail [
                    {EXPORT expected} word {to be in} ^types
                    {but it was} (mold kind of val) else ["null"]
                ]
            ]
            items: next items
        ]
        append exports word
    ]

    return none  ; !!! Return the exported words list?
]
