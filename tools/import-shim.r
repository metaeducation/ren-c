Rebol [
    Title: {Fake IMPORT/EXPORT for Bootstrap, enable use of COMMA! syntax}
    File: %import-shim.r
    Description: {
        If a module imports %common.r, that should also imply the import of
        definitions from %bootstrap-shim.r.  This is not something obvious how
        to do in the R3-Alpha module system.

        Ren-C wants something like `Baseline: %bootstrap-shim.r` in the module
        header to indicate inheriting from places other than lib.  The feature
        is not ready at time of writing, so it is faked by having %common.r
        export the export list from %bootstrap-shim.r.  But it is on the radar.

        This shim redefines IMPORT for the bootstrap executable to be a synonym
        for DO, when the `Type: module` annotation is removed.  That just
        puts everything into the user context, which is ugly but made to work
        in order to use the old executable to build.  It also avoids importing
        things more than once, so it achieves a similar semantic to modules.

        So long as it is doing that, it also strips out the commas.  This
        doesn't let them act as expression barriers but can help readability.
        Additionally it turns lone quotes into the word `null`.
    }
    Usage: {
        DO no longer changes the working directory to system.script.path,
        so to include this it should say this magic incantation:

            if not find words of import 'product [
                do load append copy system/script/path %import-shim.r
            ]

        This helps standardize the following rules between the new and the
        old bootstrap executable:

            https://github.com/metaeducation/rebol-issues/issues/2374
    }
    Notes: {
      * DO LOAD is used instead of DO to avoid the legacy behavior of changing
        to the directory of %import-shim.r when running a FILE! (vs. a BLOCK!)
        This also keeps it from resetting the directory when the DO is over,
        so we can CHANGE-DIR from this shim to compensate for the changing
        into the calling script's directory...making it appear that the
        command line processing never switched the dir in the first place.

      * APPEND is used instead of JOIN because the bootstrap executable
        semantically considers JOIN to be mutating.  The decision on this was
        that JOIN would be non-mutating but also non-reducing.  But the
        bootstrap-shim.r can't be imported until after the import-shim.r, so
        using APPEND is the clearest alternative.)

      * There seems to be flakiness on recursive DO in the bootstrap EXE.
        So having multi-inclusion handled inside this script isn't an option,
        because READs and other things start panic'ing randomly.  It seems
        avoiding recursion solves the issue: run %import-shim.r only once.
    }
]

already-imported: make map! []  ; avoid importing things twice

trap [
    func [x [<maybe> integer!]] []  ; use <maybe> as litmus test for newish EXE
] else [
    quit  ; assume modern IMPORT/EXPORT, don't need hacks
]

if set? 'import-shim-loaded [  ; already ran this shim
    fail "Recursive loading %import-shim.r is flaky, check 'import-shim-loaded"
]

lib3: lib

; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
; Only supports strings in bootstrap, because sea of words is not in bootstrap
; executable, so plain words here creates a bunch of variables...could confuse
; the global state more than it already is.
;
===: lib3/func [
    ; note: <...> is now a TUPLE!, and : used to be "hard quote" (vs ')
    label [text!]
    'terminal [word!]
][
    assert [equal? terminal '===]
]


=== {WORKAROUND FOR BUGGY PRINT IN BOOTSTRAP EXECUTABLE} ===

; Commit #8994d23 circa Dec 2018 has sporadic problems printing large chunks
; (in certain mediums, e.g. to the VSCode integrated terminal).  Replace PRINT
; as early as possible in the boot process with one that uses smaller chunks.
; This seems to avoid the issue.
;
prin3-buggy: :lib3/prin
print: lib3/print: lib3/func [value <local> pos] [
    if value = newline [  ; new: allow newline, to mean print newline only
        prin3-buggy newline
        return
    ]
    value: spaced value  ; uses bootstrap shim spaced (once available)
    while [true] [
        prin3-buggy copy/part value 256
        if tail? value: skip value 256 [break]
    ]
    prin3-buggy newline
]


=== {GIVE SHORT NAMES THAT CALL OUT BOOTSTRAP EXE'S VERSIONS OF FUNCTIONS} ===

; The shims use the functions in the bootstrap EXE's lib to make forwards
; compatible variations.  But it's not obvious to a reader that something like
; `lib/func` isn't the same as `func`.  These names help point it out what's
; happening more clearly (the 3 in the name means "sorta like R3-Alpha")

libuser: system/contexts/user

find3: :lib3/find  ; used in incantation magic for import shim...can't undefine

for-each [alias shim] [  ; SET-WORD! including 3 for easy search on `xxx3: ...`
    func3: *
    function3: *
    unset3: *
    append3: * change3: * insert3: *
    compose3: *
    select3: *
    split-path3: *
    local-to-file3: * file-to-local3: *

    collect3: (adapt :lib3/collect [
        body: lib3/compose [
            keep3: :keep  ; help point out keep3 will splice blocks, has /ONLY
            keep []  ; bootstrap workaround: force block result even w/no keeps
            lib3/unset 'keep
            (body)  ; compose3 will splice the body in here
        ]
    ])
][
    lib3/parse to text! alias [copy name to "3" (name: to word! name)]

    if shim = '* [
        set alias (get in lib3 name)
    ] else [
        set alias do shim
    ]

    ; Manually expanding contexts this way seems a bit buggy in bootstrap EXE
    ; Appending the word first, and setting via a PATH! seems okay.
    ;
    error: spaced [(mold name) "not shimmed yet, see" as word! (mold alias)]
    lib3/append system/contexts/user 'name
    recycle
    system/contexts/user/(name): lib3/func [] lib3/compose [
        fail/where (error) 'return
    ]
]


=== {STANDARDIZE DIRECTORY TO WHERE THE COMMAND LINE WAS INVOKED FROM} ===

; Typically if any filenames are passed to a script, those paths should be
; interpreted relative to what directory the user was in when they invoked
; the program.  Historical Rebol changed the directory to the directory of the
; running script--which throws this off.
;
; Here we change the directory back to where it was when the script was
; started, which is compatible with the current EXE's behavior.

change-dir system/options/path


=== {EXPORT} ===

export: func3 [
    "%import-shim.r variant of EXPORT which just puts the definition into LIB"

    :set-word [<skip> set-word!]  ; old style unescapable literal
    args "`export x: ...` for single or `export [...]` for words list"
        [<opt> any-value! <...>]  ; <...> is old-style variadic indicator
    <local>
        items
][
    if :set-word [
        args: take args
        append3 system/contexts/user reduce [  ; splices blocks by default
            set-word (set set-word :args)
        ]
        return get 'args
    ]

    items: take args
    if group? :items [items: do args]
    if not block? :items [
        fail "EXPORT must be of form `export x: ...` or `export [...]`"
    ]

    for-each word :items [
        if not word? :word [  ; no type checking in shim via BLOCK!s
            fail "EXPORT only exports block of words in bootstrap shim"
        ]
        append3 system/contexts/user reduce [  ; splices blocks by default
            word get word
        ]
    ]
]

strip-commas-and-null-apostrophes: func3 [
    {Remove the comma-space sequence from the non-string portions of the code}
    source [text!]
    <local> pushed rule
][
    pushed: copy []  ; <Q>uoted or <B>raced string delimiter stack

    rule: [
        ; Bootstrap WHILE: https://github.com/rebol/rebol-issues/issues/1401
        while [
            "^^{"  ; (actually `^{`) escaped brace, never count
            |
            "^^}"  ; (actually `^}`) escaped brace, never count
            |
            {^^"}  ; (actually `^"`) escaped quote, never count
            |
            "{" (if <Q> != last pushed [append3 pushed <B>])
            |
            "}" (if <B> = last pushed [take/last pushed])
            |
            {"} (
                case [
                    <Q> = last pushed [take/last pushed]
                    empty? pushed [append3 pushed <Q>]
                ]
            )
            |
            ; Try to get a little performance by matching ahead ", " first
            ;
            ahead ", " if (empty? pushed) remove ","
            |
            [space | newline] ahead {'} ahead [{' } | {')} | {']} | {'^/}]
            change {'} ({null}) skip
            |
            skip
        ]
        end
    ]

    lib3/parse source rule else [  ; PARSE2 shim may not be loaded yet
        fail "STRIP-COMMAS did not work"
    ]
    return source
]

old-do: :lib3/do
do: lib3/do: enclose :lib3/do func3 [f <local> old-dir] [
    old-dir: _
    if file? :f.source [
        ;
        ; We want to strip the commas, but we also want the file-like behavior
        ; of preserving the directory.  :-(  Implement via wrapper.
        ;
        f.source: strip-commas-and-null-apostrophes read/string f.source
        old-dir: what-dir
        change-dir first split-path f.source
    ]
    old-do f
    elide if old-dir [change-dir old-dir]
]


import: enfix func3 [
    "%import-shim.r variant of IMPORT which acts like DO and loads only once"

    :set-word [<skip> set-word!]
    f [tag!]  ; help catch mistakes, all bootstrap uses TAG!

    ; !!! The new import has a PRODUCT: output, but WORDS OF only returns
    ; words in modern Ren-C...whereas they were decorated in historical Rebol.
    ; Make the signal the argument to an old-style refinement so the WORDS OF
    ; will make it look like a plain word for the check.
    ;
    /trick product "Signal that the shim has been applied"

    ; NOTE: LET is unavailable (we have not run the bootstrap shim yet)
    ;
    <local> ret path+file old-dir old-system-script code new-script-path
][
    if trick [
        fail "/PRODUCT not actually available, just makes IMPORT look modern"
    ]

    f: as file! f

    if ret: select3 already-imported f [
        return ret
    ]

    path+file: split-path3 f

    assert [#"/" <> first path+file/1]  ; should be relative
    assert [#"%" <> first path+file/1]  ; accidental `import <%foo.r>`

    new-script-path: append3 copy any [
        system/script/path system/options/path
    ] path+file/1

    new-script-path: clean-path new-script-path

    old-dir: what-dir
    old-system-script: system/script
    change-dir new-script-path
    system/script: make system/standard/script [
        title: "Script imported by import shim"
        header: _
        parent: system/standard/script
        path: new-script-path
        args: _
    ]

    code: read/string second path+file

    if find3 code "Type: 'Module" [
        fail "Old tick-style module definition, use `Type: module` instead"
    ]
    if find3 code "Type: module" [
        replace code "Type: module" ""
    ]

    strip-commas-and-null-apostrophes code

    ; The MAKE OBJECT! sense of things will overwrite definitions, e.g.
    ; if you try and say `func: func [...] [...]` then that won't work because
    ; `make object! [func: ...]` unsets func in advance.  Only use this trick
    ; if they're trying to set a result back.
    ;
    ret: #quit
    catch/quit [
        ret: if :set-word [
            set set-word make object! load code
        ] else [
            do load code
            #imported
        ]
    ]

    change-dir old-dir
    system/script: old-system-script
    if system/script/path = %/home/hostilefork/Projects/ren-c/ [
        protect 'system/script/path
    ]
    already-imported/(f): ret
    return ret
]

lib3/import: sys/import: func3 [
    module [word! file! url! text! binary! module! block! tag!]
    /version ver [tuple!]
    /no-share
    /no-lib
    /no-user
][
    fail ["Bootstrap must use %import-shim.r's IMPORT, not call LIB3/IMPORT"]
]

import-shim-loaded: true
