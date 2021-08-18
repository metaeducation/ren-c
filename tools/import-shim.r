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

        This shim redefines import for the bootstrap executable to be a synonym
        for DO, when the `Type: 'Module` annotation is removed.  That just
        puts everything into the user context, which is ugly but made to work
        in order to use the old executable to build.  It also avoids importing
        things more than once, so it achieves a similar semantic to modules.

        So long as it is doing that, it also strips out the commas.  This
        doesn't let them act as expression barriers but can help readability.
        Additionally it turns lone quotes into the word `null`.
    }
]

already-imported: make map! []  ; avoid importing things twice

trap [
    func [x [<blank> integer!]] []  ; use <blank> as litmus test for newish EXE
] else [
    quit  ; assume modern IMPORT/EXPORT, don't need hacks
]

if in lib 'import-shim-loaded [quit]  ; already ran this shim

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
        return :args
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

    parse source rule else [fail "STRIP-COMMAS did not work"]
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

import: enfix func [
    "%import-shim.r variant of IMPORT which acts like DO and loads only once"

    :set-word [<skip> set-word!]
    f [file!]
    <local> code old ret path+file
][
    f: clean-path f
    ret: :already-imported/(f)
    if not null? :ret [
        return :ret
    ]

    path+file: split-path f

    old: what-dir
    change-dir first path+file

    code: read/string second path+file
    if find code "Type: 'Module" [
        replace code "Type: 'Module" ""
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

    change-dir old

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

append lib compose [import-shim-loaded: true]
