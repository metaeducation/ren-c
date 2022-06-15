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



load-header: function [
    {Loads script header object and body binary (not loaded).}

    return: "header OBJECT! if present, or error WORD!"
        [<opt> object! word!]
    body: "<output>"
        [binary! text!]
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
        hdr: construct/with/only :hdr system.standard.header
    ] then [
        return 'bad-header
    ]

    (match [block! blank!] try :hdr.options) else [
        return 'bad-header
    ]

    if find try hdr.options just content [
        append hdr compose [content (data)]  ; as of start of header
    ]

    if 10 = rest.1 [rest: next rest, line: me + 1]  ; skip LF

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
            find try hdr.options just compress [
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
        rest: skip data 2  ; !!! what is this skipping ("hdr.length" ??)

        if find try hdr.options just compress [  ; script encoded only
            rest: attempt [gunzip first rest] else [
                return 'bad-compress
            ]
        ]
    ]

    if body [set body ensure [binary! text!] rest]
    if line-out [set line-out ensure integer! line]
    if final [set final ensure [binary! text!] end]

    ensure object! hdr
    ensure [<opt> block! blank!] hdr.options
    return hdr
]


load: func [
    {Loads code or data from a file, URL, text string, or binary.}

    return: "BLOCK! if Rebol code, otherwise value(s) appropriate for codec"
        [<opt> any-value!]
    header: "<output> Request the Rebol header object be returned as well"
        [object!]

    source "Source of the information being loaded"
        [<blank> file! url! tag! the-word! text! binary!]
    /type "E.g. rebol, text, markup, jpeg... (by default, auto-detected)"
        [word!]

    <local> file line data hdr
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

    if not find [unbound rebol] ^type [
        if find system.options.file-types ^type [
            return decode type :data
        ]

        fail ["No" type "LOADer found for" type of source]
    ]

    ensure [text! binary!] data

    [hdr data line]: load-header/file data file

    if word? hdr [cause-error 'syntax hdr source]

    ensure [blank! object!] hdr: default [_]
    ensure [binary! block! text!] data

    ; Convert code to block, insert header if requested

    if not block? data [
        assert [match [binary! text!] data]  ; UTF-8
        data: transcode/file/line data file 'line
    ]

    ; Bind code to user context

    all [
        'unbound != type
        'module != select hdr 'type
        not find (try get 'hdr.options) [unbound]
    ] then [
        data: intern* system.contexts.user data
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
    url [<blank> url!]
][
    let text: to text! url  ; URL! may become immutable, try thinking ahead

    uparse text [
        "http" opt "s" "://gitlab.com/"
        thru "/"  ; user name
        thru "/"  ; repository name
        opt "-/"  ; mystery thing (see remarks on CORSify-gitlab-port)
        change "blob/" ("raw/")
        to <end>
    ] then [
        return as url! text  ; The port will CORSIFY at a lower level
    ]

    ; Adjust a decorated GitHub UI to https://raw.githubusercontent.com
    let start
    uparse text [
        "http" opt "s" "://github.com/"
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
    uparse text [
        "http" opt "s" "://gist.github.com/"
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
    product: "Evaluative product of module body (only if WHERE is BLANK!)"
        [<opt> any-value!]

    where "Where to put exported definitions from SOURCE"
        [blank! module!]
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
    /into "e.g. reuse REBCTX* already made for NATIVEs loading from extension"
        [module!]
    <static>
        importing-remotely (false)
][
    product: default [#]  ; we do nothing differently if not requested

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
        let exports: select (try meta-of ensure module! unmeta :value) 'exports
        if exports [
            resolve where (unmeta :value) exports
        ]
    ]

    === IF MODULE ALREADY CREATED, REGISTER AND RESOLVE IMPORTED VARS ===

    if module? source [
        assert [not into]  ; ONLY isn't applicable unless scanning new source

        let name: (meta-of source).name else [
            set product '~nameless~
            return source  ; no name, so just do the RESOLVE to get variables
        ]
        let mod: select/skip system.modules name 2 else [
            append system.modules :[name source]  ; not in module list, add it
            set product '~registered~
            return source
        ]
        if mod != source [
            fail ["Conflict: more than one module instance named" name]
        ]
        if product [
            set product '~cached~
        ]
        return source
    ]


    === ADJUST URL FROM HTML PRESENTATION TO RAW IF SUPPORTED ===

    ; If URL is decorated source (syntax highlighting, etc.) get raw form.
    ;
    (adjust-url-for-raw try match url! :source) then adjusted -> [
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
            file: join path :[source system.options.default-suffix]
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
        let file: find-last/tail dir slash
        assert [file]
        dir: as (type of source) copy/part dir file
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

    let name: (try hdr).name
    (select/skip system.modules try name 2) then cached -> [
        set product ~cached~
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
        title: try select try hdr 'title
        header: hdr
        parent: :original-script
        path: dir
        args: (try :args)  ; variable named same as field, trips up binding
    ]

    if (set? 'script-pre-load-hook) and (match [file! url!] source) [
        ;
        ; !!! It seems we could/should pass system.script here, and the
        ; filtering of source as something not to notify about would be the
        ; decision of the hook.
        ;
        ; !!! Should there be a post-script-hook?
        ;
        script-pre-load-hook is-module try hdr
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
    if where and (dir) [  ; IMPORT
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

    let [mod '(product) quitting]: module/into/file/line try hdr code into file line

    ensure module! mod

    if is-module and (name) [
        append system.modules :[name mod]
    ]

    === RESTORE SYSTEM.SCRIPT AND THE DIRECTORY IF THEY WERE CHANGED ===

    if original-script [system.script: original-script]
    if original-path [change-dir original-path]

    importing-remotely: old-importing-remotely

    === PROPAGATE QUIT IF REQUESTED, OR RETURN MODULE ===

    if quitting and only [
        quit get/any product  ; "rethrow" the QUIT if DO/ONLY
    ]

    return mod
]


export*: func [
    {Add words to module's `Exports: []` list}

    return: "Evaluated expression if used with SET-WORD!"
        [<opt> any-value!]
    where "Specialized for each module via EXPORT"
        [module!]
    'set-word [<skip> set-word!]
    args "`export x: ...` for single or `export [...]` for words list"
        [<opt> any-value! <variadic>]
    <local>
        hdr exports val word types items
][
    hdr: meta-of where
    exports: ensure block! select hdr 'Exports

    if set-word [
        set set-word args: take args
        append exports ^(as word! set-word)
        return get/any 'args
    ]

    items: take args
    if group? :items [items: do items]
    if not block? :items [
        fail "EXPORT must be of form `export x: ...` or `export [...]`"
    ]

    while [not tail? items] [
        val: get/any word: match word! items.1 else [
            fail ["EXPORT only accepts WORD! or WORD! [typeset], not" ^items.1]
        ]
        ; !!! notation for exporting isotopes?
        items: next items

        (types: match block! :items.1) then [
            if bad-word? ^val [  ; !!! assume type block means no isotopes
                fail [{EXPORT given} types {for} word {but it is} ^val]
            ]
            (find (make typeset! types) kind of :val) else [
                fail [
                    {EXPORT expected} word {to be in} ^types
                    {but it was} (mold kind of :val) else ["null"]
                ]
            ]
            items: next items
        ]
        append exports ^word
    ]

    return none  ; !!! Return the exported words list?
]
