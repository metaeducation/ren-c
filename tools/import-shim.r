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
    Notes: {
        * DO no longer changes the working directory to system.script.path,
          so to include this it should say this magic incantation:

            if not find words of import [product] [
                do load append copy system/script/path %import-shim.r
            ]

          This helps standardize the following rules between the new and the
          old bootstrap executable:

            https://github.com/metaeducation/rebol-issues/issues/2374

          A couple of points...  :-(

          LOAD is used so that the DO does not have the legacy behavior of
          changing to the directory of %import-shim.r, because it's just doing
          a BLOCK!.  This also keeps it from resetting the directory when the
          DO is over, so we can CHANGE-DIR in this script to compensate for
          the changing into the script directory...making it appear that the
          command line processing never switched the dir in the first place.

          APPEND is used instead of JOIN because the bootstrap executable
          semantically considers JOIN to be mutating.  The decision on this
          was that JOIN would be non-mutating but also non-reducing.  But the
          bootstrap-shim.r can't be imported until after the import-shim.r, so
          using APPEND is the clearest alternative.)

          There seems to be flakiness on recursive DO in the bootstrap EXE.
          So having multi-inclusion handled inside this script isn't an option,
          because READs and other things start panic'ing randomly.  It seems
          avoiding recursion solves the issue.
    }
]

already-imported: make map! []  ; avoid importing things twice

trap [
    func [x [<blank> integer!]] []  ; use <blank> as litmus test for newish EXE
] else [
    quit  ; assume modern IMPORT/EXPORT, don't need hacks
]

if set? 'import-shim-loaded [  ; already ran this shim
    fail "Recursive loading %import-shim.r is flaky, check 'import-shim-loaded"
]

; Standardize the directory to be wherever the command line was invoked from,
; and NOT where the script invoked (e.g. %make.r) is located.
;
change-dir system/options/path

export: func [
    "%import-shim.r variant of EXPORT which just puts the definition into LIB"

    :set-word [<skip> set-word!]  ; old style unescapable literal
    args "`export x: ...` for single or `export [...]` for words list"
        [<opt> any-value! <...>]  ; <...> is old-style variadic indicator
    <local>
        items
][
    if :set-word [
        args: take args
        append system/contexts/user reduce [set-word (set set-word :args)]
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
        append system/contexts/user reduce [word get word]
    ]
]

strip-commas-and-null-apostrophes: func [
    {Remove the comma-space sequence from the non-string portions of the code}
    source [text!]
    <local> pushed rule
][
    pushed: copy []  ; <Q>uoted or <B>raced string delimiter stack

    rule: [
        while [  ; https://github.com/rebol/rebol-issues/issues/1401
            "^^{"  ; (actually `^{`) escaped brace, never count
            |
            "^^}"  ; (actually `^}`) escaped brace, never count
            |
            {^^"}  ; (actually `^"`) escaped quote, never count
            |
            "{" (if <Q> != last pushed [append pushed <B>])
            |
            "}" (if <B> = last pushed [take/last pushed])
            |
            {"} (
                case [
                    <Q> = last pushed [take/last pushed]
                    empty? pushed [append pushed <Q>]
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

    ; NOTE: This is PARSE2 but bootstrap-shim may not be loaded when this is
    ; called.  use LIB/PARSE to be safe (it's bootstrap exe's version)
    ;
    lib/parse source rule else [fail "STRIP-COMMAS did not work"]
    return source
]

old-do: :lib/do
do: lib/do: enclose :lib/do func [f <local> old-dir] [
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

first-import: true

import: enfix func [
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

    ret: :already-imported/(f)
    if not null? :ret [
        return :ret
    ]

    path+file: split-path f

    assert [#"/" <> first path+file/1]  ; should be relative
    assert [#"%" <> first path+file/1]  ; accidental `import <%foo.r>`

    new-script-path: append copy any [
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

    if find code "Type: 'Module" [
        fail "Old tick-style module definition, use `Type: module` instead"
    ]
    if find code "Type: module" [
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

lib/import: sys/import: func [
    module [word! file! url! text! binary! module! block! tag!]
    /version ver [tuple!]
    /no-share
    /no-lib
    /no-user
][
    fail ["Bootstrap must use %import-shim.r's IMPORT, not call LIB/IMPORT"]
]

import-shim-loaded: true
