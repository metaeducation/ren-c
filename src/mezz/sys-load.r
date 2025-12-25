Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Sys: Load, Import, Modules"
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    context: sys
    notes: --[
        The boot binding of this module is SYS then LIB deep.
        Any non-local words not found in those contexts WILL BE
        UNBOUND and will error out at runtime!

        These functions are kept in a single file because they
        are inter-related.

        The fledgling module system in R3-Alpha was never widely used or
        tested, but here is some information:

        http://www.rebol.com/r3/docs/concepts/modules-defining.html
        https://github.com/revault/rebol-wiki/wiki/Module-Design-Details
    ]--
]


; !!! R3-Alpha Module loading had the ability to be delayed.  This was so
; that special modules like CGI protocols or HTML formatters could be available
; in the SYSTEM.MODULES list...held as just headers and the BLOB! or BLOCK!
; that would be used to initialize them.  The feature obfuscated more
; foundational design points, so was temporarily removed...but should be
; brought back once the code solidifies.


transcode-header: func [
    "Try to match a data blob! as being a script, fail if not"

    return: [
        ~[[<null> block!] [<null> blob!] [integer!]]~
        "[header rest line]"

        error! "Missing `Rebol [...]` at start of data"
    ]
    data [blob!]
    :file [file! url!]

    {line (1) key hdr rest}
][
    [rest key]: trap transcode:next // [  ; "Rebol"
        data
        file: file
        line: $line
    ]
    if not rest [
        return pack [null null line]  ; !!! rethink interface (impure null)
    ]
    [rest :hdr]: trap transcode:next // [  ; BLOCK!
        rest
        file: file
        line: $line
    ]

    hdr: all [key = 'Rebol, match block! hdr]
    return pack [hdr rest line]  ; !!! hdr can be null but not ELSE-reactive
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
; If not :ONLY and the script is embedded in a block and not compressed then
; the body text will be a decoded block instead of binary, to avoid the
; overhead of decoding the body twice.
;
load-header: func [
    "Loads script header object and body binary (not loaded)"

    return: [
        ~[[<null> object!] [blob! text!] [<null> integer!] blob!]~
        "header OBJECT! if present [hdr body line final], and next position"
    ]
    source "Source code (text! will be UTF-8 encoded)"
        [blob! text!]
    :file "Where source is being loaded from"
        [file! url!]
    :only "Only process header, don't decompress body"
    :required "Script header is required"

    {data hdr rest line body final end binary}
]
bind {
    non-ws: make bitset! [not 1 - 32]
} [
    data: as blob! source  ; if it's not UTF-8, decoding provides error

    ; The TRANSCODE function convention is that the LINE OF is the line number
    ; of the *end* of the transcoding so far, (to sync line numbering across
    ; multiple transcodes)

    === TRY TO MATCH PATTERN OF "Rebol [...]" ===

    [hdr rest line]: transcode-header:file data file except e -> [
        return fail e  ; TRANSCODE choked, wasn't valid at all
    ]

    if not hdr [
        ;
        ; TRANSCODE didn't detect Rebol [...], but it didn't see anything it
        ; thought was invalid Rebol tokens either.
        ;
        if required [
            return fail "no-header"
        ]
        body: data
        final: tail of data
        return pack [null body line final]  ; !!! impure null
    ]

    hdr: construct:with (pin hdr) system.standard.header except [
        return fail "bad-header"
    ]

    if not typecheck [<null> block!] hdr.options [
        return fail "bad-header"
    ]

    if find opt hdr.options 'content [
        append hdr spread compose [content (data)]  ; as of start of header
    ]

    if 10 = try rest.1 [rest: next rest, line: me + 1]  ; skip LF

    end: all [
        let tmp: select hdr 'length
        integer? tmp
    ] then [
        skip rest tmp
    ] else [
        tail of data
    ]

    if only [  ; when it's :ONLY, decompression is not performed
        body: rest
        final: end
        return [hdr body line final]
    ]

    let binary
    if <always> [  ; was `if key = 'Rebol`, how was that ever not true?
        ;
        ; !!! R3-Alpha apparently used a very bad heuristic of attempting to
        ; decompress garbage (likely asking for a very big memory allocation
        ; and panicking), and rescued it to see if it panicked.
        ;
        if find opt hdr.options 'compress [
            any [
                not warning? sys.util/recover [
                    ; Raw bits.  whitespace *could* be tolerated; if
                    ; you know the kind of compression and are looking
                    ; for its signature (gzip is 0x1f8b)
                    ;
                    rest: gunzip:part rest end
                ]
                not warning? sys.util/recover [  ; e.g. not error
                    ; BLOB! literal ("'SCRIPT encoded").  Since it
                    ; uses transcode, leading whitespace and comments
                    ; are tolerated before the literal.
                    ;
                    [rest binary]: transcode:next:file:line rest file $line
                    rest: gunzip binary
                ]
            ]
            else [
                return fail "bad-compress"
            ]
        ]
    ] else [
        ; block-embedded script, only script compression

        data: transcode data  ; decode embedded script
        rest: skip data 2  ; !!! what is this skipping ("hdr.length" ??)

        if find opt hdr.options 'compress [  ; script encoded only
            rest: trap gunzip first rest
        ]
    ]

    ; !!! pack typecheck should handle this
    body: ensure [blob! text!] rest
    ensure integer! line
    final: ensure [blob! text!] end

    ensure object! hdr
    ensure [<null> block! _] hdr.options

    return pack [hdr body line final]
]


; 1. !!! R3-Alpha's READ is nebulous, comment said "can be string, binary,
;    block".  Current leaning is that READ always be a binary protocol, and
;    that LOAD would be higher level--and be based on decoding BLOB! or some
;    higher level method that never goes through a binary.
;
; 2. Pursuant to [1]: `read %./` would return a BLOCK! of directory contents,
;    and LOAD was expected to return that block.  Do that for compatibility
;    with the tests until more work is done.
;
; 3. !!! Once this would bind code to user context, now we're thinking of
;    code being unbound by default.  Should LOAD be binding?  Here we give it
;    its own module which inherits from lib.  Ultimately you should be able
;    to load modules using completely different baselines.
;
load: func [
    "Loads code or data from a file, URL, text string, or binary"

    return: [
        ~[element? [<null> object!]]~
        "BLOCK! if Rebol code (or codec value) plus optional header"

        <null>
    ]
    source "Source of the information being loaded"
        [<opt-out> file! url! tag! @word! text! blob!]
    :type "E.g. rebol, text, markup, jpeg... (by default, auto-detected)"
        [word!]

    {header file line data}
][
    if match [file! url! tag! @word!] source [
        source: clean-path source

        file: ensure [file! url!] source
        type: default [file-type? source else ['rebol]]  ; !!! rebol default?

        data: read source  ; not always binary [1]

        if block? data [  ; e.g. (read %some-dir/) gives block [2]
            header: null
            return pack [data header]
        ]
    ]
    else [
        file: null
        data: source
        type: default ['rebol]
    ]

    if type = 'extension [
        panic "Use LOAD-EXTENSION to load extensions (at least for now)"
    ]

    if not find [unbound rebol] type [
        if find system.options.file-types type [
            header: null
            return pack [(decode type data) header]
        ]

        panic ["No" type "LOADer found for" type of source]
    ]

    ensure [text! blob!] data

    [header data line]: trap load-header:file data file

    if word? opt header [cause-error 'syntax header source]

    ensure [<null> object!] header
    ensure [blob! block! text!] data

    if not block? data [  ; Convert code to block
        assert [match [blob! text!] data]  ; UTF-8

        data: trap transcode:file:line data file $line

        if not data [  ; completely empty file e.g. (load "Rebol []")
            data: copy []  ; allow it in this case
        ]
    ]

    let mod: make module! inside system.contexts.lib '[]  ; inherit lib [3]

    return pack [(inside mod data) header]
]


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
    return: [<null> url!]
    url [<opt-out> url!]
][
    let text: to text! url  ; URL! is immutable, must copy to mutate in parse

    try parse text [
        "http" opt "s" "://gitlab.com/"
        thru "/"  ; user name
        thru "/"  ; repository name
        opt "-/"  ; mystery thing (see remarks on CORSify-gitlab-port)
        change "blob/" ("raw/")

        (return as url! text)  ; The port will CORSIFY at a lower level
    ]

    ; Adjust a decorated GitHub UI to https://raw.githubusercontent.com
    ;
    try parse text [
        "http" opt "s" "://github.com/"
        let start: <here>
        thru "/"  ; user name
        thru "/"  ; repository name
        change "blob/" ("")  ; GitHub puts the "raw" in the subdomain name

        (return compose https://raw.githubusercontent.com/(start))
    ]

    ; Adjust a Github Gist URL to https://gist.github.com/.../raw/
    ;
    try parse text [
        "http" opt "s" "://gist.github.com/"
        let start: <here>
        thru "/"  ; user name
        [
            to "#file="
            remove to <end>  ; ignore file for now, id does not match filename
            |
            to <end>
        ]
        insert ("/raw/")

        (return compose https://gist.githubusercontent.com/(start))
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
    "Imports a module; locate, load, make, and setup its bindings"

    return: [
        ~[module! ~(executed)~ any-value?]~
        "Loaded module and evaluative product (if not previously loaded)"

        ~[module! ~(cached registered nameless)~]~
        "Previously loaded module"
    ]
    where "Where to put exported definitions from SOURCE"
        [<opt> module!]
    source [
        file! url!  ; get from location, run with location as working dir
        tag!  ; load relative to system.script.path
        @word!  ; look up as a shorthand in registry
        blob!  ; UTF-8 source, needs to be checked for invalid byte patterns
        text!  ; source internally stored as validated UTF-8, *may* scan faster
        word!  ; not entirely clear on what WORD! does.  :-/
        module!  ; register the module and import its exports--do not create
    ]
    :args "Args passed as system.script.args to a script (normally a string)"
        [element?]
    :into "e.g. reuse VarList* already made for NATIVEs loading from extension"
        [module!]
]
bind construct [
    importing-remotely: 'no
][
    return: adapt return/ [  ; make sure all return paths actually import vars
        ;
        ; Note: `atom` below is the argument to RETURN.  It is a ^META
        ; parameter so should be a quoted pack containing a module.  We don't
        ; disrupt that, else falling through to RETURN would get tripped up.
        ;
        ; !!! The idea of `import *` is frowned upon as a practice, as it adds
        ; an unknown number of things to the namespace of the caller.  Most
        ; languages urge you not to do it, but JavaScript bans it entirely.  We
        ; should maybe ban it as well (or at least make it inconvenient).  But
        ; do it for the moment since that is how it has worked in the past.
        ;
        assert [pack? ^value]
        if where [
            let mod: ensure module! decay ^value
            let exports: select (opt adjunct-of mod) 'exports
            proxy-exports where mod (opt exports)
        ]
    ]

    === IF MODULE ALREADY CREATED, REGISTER AND RESOLVE IMPORTED VARS ===

    if module? source [
        assert [not into]  ; INTO isn't applicable unless scanning new source

        let name: (adjunct-of source).name else [
            return pack [source 'nameless]  ; just RESOLVE to get variables
        ]
        let mod: select system.modules name else [
            system.modules.(name): source  ; not in mod list, add
            return pack [source 'registered]
        ]
        if mod != source [
            panic ["Conflict: more than one module instance named" name]
        ]
        return pack [source 'cached]
    ]


    === ADJUST URL FROM HTML PRESENTATION TO RAW IF SUPPORTED ===

    ; If URL is decorated source (syntax highlighting, etc.) get raw form.
    ;
    (adjust-url-for-raw opt match url! source) then adjusted -> [
        source: adjusted  ; !!! https://forum.rebol.info/t/1582/6
    ]


    === TREAT (IMPORT 'FILENAME) AS REQUEST TO LOOK LOCALLY FOR FILENAME.R ===

    ; We don't want remote execution of a module via `import <some-library>`
    ; to be able to turn around and run code locally.  So during a remote
    ; import, any WORD!-style imports like `import 'mod2` are turned into
    ; `import @mod2`.
    ;
    let old-importing-remotely: importing-remotely
    if all [yes? importing-remotely, word? source] [
        source: pin source
    ]

    if word? source [
        let file
        for-each 'path system.options.module-paths [
            file: join path [source system.options.default-suffix]
            if not exists? file [
                continue
            ]
            source: file
            break
        ]

        (file? source) else [
            panic ["Could not find any" file "in SYSTEM.OPTIONS.MODULE-PATHS"]
        ]
    ]

    === GET DIRECTORY PORTION OF SOURCE PATH FOR NEW SYSTEM.SCRIPT.PATH  ===

    ; A relative path may have contributed some of its own directory portions,
    ; so extract the net path where we're executing to save in system.script.

    let dir: null
    match [file! url! @word! tag!] source then [
        source: clean-path source
        dir: as text! source
        let [before file]: find-last dir slash
        assert [before]
        dir: as (type of source) copy:part dir file
    ]

    if url? source [
        importing-remotely: 'yes
    ]

    === LOAD JUST THE HEADER FOR EXAMINATION, TO FIND THE NAME ===

    ; There may be a `Name:` field in the header.  Historically this was used
    ; to tell if an already loaded version of the module was loaded...but this
    ; doesn't account for potential variations (maybe loading different hashes
    ; from different locations, or if the file has changed?)
    ;
    ; !!! Noticing changes would be extremely helpful by not forcing you to
    ; quit and restart to see module changes.  This suggests storing a hash.

    let file: match [file! url!] source  ; for file and line info during scan

    let data: match [blob! text!] source else [read source]

    let [hdr code line]: load-header:file data file
    if not hdr [
        panic ["IMPORT and DO require a header on:" (any [file, "<source>"])]
    ]

    let name: select opt hdr 'name
    (select system.modules opt name) then cached -> [
        return pack [cached 'cached]
    ]

    ensure [<null> object!] hdr

    let is-module: to-yesno all [hdr, 'module = select hdr 'type]

    === MAKE SCRIPT CHARACTERIZATION OBJECT AND CALL PRE-SCRIPT HOOK ===

    ; The header object doesn't track instance information like the args to
    ; a script, or what file path it was executed from.  And we probably don't
    ; want to inject that in the header.  So SYSTEM.STANDARD.SCRIPT is the
    ; base object for gathering these other instantiation-related properties
    ; so the running script can know about them.

    let original-script: system.script

    system.script: make system.standard.script compose [
        title: select opt hdr 'title
        header: hdr
        parent: original-script
        path: dir
        args: '(args)  ; variable same name as field, trips up binding
    ]

    if (not trash? get $script-pre-load-hook) and (match [file! url!] source) [
        ;
        ; !!! It seems we could/should pass system.script here, and the
        ; filtering of source as something not to notify about would be the
        ; decision of the hook.
        ;
        ; !!! Should there be a post-script-hook?
        ;
        script-pre-load-hook // [is-module hdr]
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

    if not block? code [  ; review assumption of lib here (header guided?)
        code: inside lib transcode:file:line code file line
    ]
    ignore let [mod ^product]: module:into hdr code into

    ensure module! mod

    if (yes? is-module) and name [
        system.modules.(name): mod
    ]

    === RESTORE SYSTEM.SCRIPT AND THE DIRECTORY IF THEY WERE CHANGED ===

    if original-script [system.script: original-script]
    if original-path [change-dir original-path]

    importing-remotely: old-importing-remotely

    return pack [mod 'executed ^product]  ; ERROR! antiform is legal product
]


export*: func [
    "Add words to module's (exports: []) list, return value if assigned"

    return: [any-stable?]
    where "Specialized for each module via EXPORT"
        [module!]
    @what [word!: /word!: group!: group! block!]
    ^args "(export x: ...) for single or (export [...]) for words list"
        [any-value? <variadic>]

    {hdr exports val word types items}
][
    hdr: adjunct-of where
    exports: ensure block! select hdr 'exports

    if group? what [
        ensure block! what: eval what
    ]

    if not block? what [
        if set-group? what [
            ^what: eval unchain what
            word: case [
                void? ^what [null]
                word? ^what [^what]
                match [word!:] ^what [unchain ^what]
                match [/word!:] ^what [unchain unpath ^what]

                panic "EXPORT of SET-GROUP must be VOID, WORD or SET-WORD"
            ]
        ] else [
            word: resolve what
        ]
        ^args: try take args  ; eval before EXTEND clears variable...
        return (  ; can't append until after, if prev. definition used in expr
            (
                if word [  ; no "attached" state, must append word to get IN
                    extend where word  ; maybe bound e.g. WHAT-DIR
                ]
            ): ^args
            elide if word [append exports word]
        )
    ]

    items: what

    until [tail? items] [
        ^val: get meta inside items word: match word! items.1 else [
            panic ["EXPORT only accepts WORD! or WORD! [typeset], not" ^items.1]
        ]
        ; !!! notation for exporting antiforms?
        items: next items

        (types: match block! ?? items.1) then [
            (match types ^val) else [
                panic [
                    "EXPORT expected" word "to be in" @types
                    "but it was" (to word! type of val) else ["null"]
                ]
            ]
            items: next items
        ]
        append exports word
    ]

    return ~  ; !!! Return the exported words list?
]
