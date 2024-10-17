Rebol [
    Title: "Compatibility IMPORT/EXPORT for Bootstrap"
    File: %import-shim.r
    Type: script  ; R3-Alpha module system broken, see notes

    Description: --{
        This shim redefines IMPORT and EXPORT for the bootstrap executable:

        * It handles a basic simulation of the IMPORT and EXPORT commands
          that does not use modules, but objects and puts things into
          the lib context.

        * It is also able to preprocess the source, if necessary.
    }--
    Usage: --{
        To affect how code is loaded, the import shim has to be hooked in
        before your script is running (so you can't `do %import-shim.r`).

        Fortunately, there is an --import option to preload modules that
        existed in the bootstrap executable.

            r3 --import import-shim.r make.r [OPTIONS]

        Newer executables don't need this shim, just run the script plain:

            r3 make.r [OPTIONS]
    }--
    Notes: --{
      * !!! R3-Alpha Module System Was Completely Broken !!!, so we manually
        overwrite the definitions in lib instead of using Exports:  Not even
        that worked in pre-R3C builds, so it forced an update of the bootstrap
        executable to R3C when we started using the `--import` option.
    }--
]


if find (words of import/) 'into [  ; non-bootstrap Ren-C
    fail "%import-shim.r is only for use with old Ren-C EXEs"
]


write-stdout "LOADING %import-shim.r --- "  ; when finished, adds "COMPLETE!"


; Because the semantics vary greatly, we'd like it to be clear to readers
; when something like APPEND or FUNC are using old rules.  LIB3/APPEND and
; LIB3/FUNC are good ways of seeing that.
;
append lib [lib3: _]  ; see header notes: `Exports` broken
lib.lib3: lib3: lib  ; use LIB3 to make it clearer when using old semantics

; Bootstrap shim does this to a greater extent, just help make this file
; clearer that it's using the old definitions.
;
set-word?!: set-word!
any-value?!: any-value!


=== "EXPORT" ===

/export: lib3/func [
    "%import-shim.r variant of EXPORT which just puts the definition into LIB"

    :set-word [<skip> set-word?!]  ; old style unescapable literal, old <skip>!
    args "`export x: ...` for single or `export [...]` for words list"
        [~null~ any-value?! <...>]  ; <...> is old-style variadic indicator
    <local>
        items
][
    if :set-word [
        args: take args
        lib3/append system.contexts.user reduce [  ; splices blocks by default
            set-word (set set-word :args)
        ]
        return get 'args
    ]

    items: take args
    if group? :items [items: eval args]
    if not block? :items [
        fail "EXPORT must be of form `export x: ...` or `export [...]`"
    ]

    for-each 'word :items [
        if not word? :word [  ; no type checking in shim via BLOCK!s
            fail "EXPORT only exports block of words in bootstrap shim"
        ]
        lib3/append system.contexts.user reduce [  ; splices blocks by default
            word get word
        ]
    ]
]


=== "SOURCE CONVERSION" ===

; At one point this was used to remove commas from non-string contexts, but
; that behavior was added to the bootstrap executable.
;
; It was also planned to have it preprocess the source to convert -{...}-
; strings into legacy {...} strings, but support for dashed strings was added
; to the bootstrap executable as well.
;
; So it's left around in case it comes in handy for some change that is
; easier to make here than the bootstrap executable.

/rewrite-source-for-bootstrap-exe: lib3/func [
    "Hookpoint that allows rewriting source"
    source [text!]
    <local> pushed rule
][
    pushed: copy []  ; <Q>uoted or <B>raced string delimiter stack

    rule: [
        opt some [
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
            -{"}- (
                case [
                    <Q> = last pushed [take/last pushed]
                    empty? pushed [append pushed <Q>]
                ]
            )
            |
            one
        ]
        <end>
    ]

    lib3/parse:match source rule else [
        fail "REWRITE-SOURCE-FOR-BOOTSTRAP-EXE did not work"
    ]
    return source
]


=== "IMPORT (and DO, which is used to implement IMPORT)" ===

; We must hook DO, in order for the import shim to be able to get its hooks
; into the initial DO of the top-level script:
;
;     r3 --import %import-shim.r make.r
;
; Command-line processing will DO %make.r in this situation.  So we have to
; take over the reading if we want make.r to follow new syntax rules.
;

; WRAP-MODULE is a signal set by IMPORT that the ensuing DO (of a file)
; should put the code inside of a MAKE OBJECT! (the classic way of getting
; modularization in Rebol2).
;
; However, this will erase all top level SET-WORD!s in the module.  So you
; can't use any words you redefine.  e.g. if you say `/func: func [...] [...]`
; then that won't work because `make object! [func: ...]` unsets FUNC in
; advance.  We only use the trick if they're trying to set a result back,
; such as with:
;
;    rebmake: import <tools/rebmake.r>
;
wrap-module: 'no

/old-do: lib3.do/
/do: lib3/enclose lib3.do/ lib3/func [
    f [frame!]
    <local> old-system-script file
    <with> wrap-module
][
    old-system-script: system.script

    if file? :f.source [
        file: f.source

        system.script: make system.standard.script [
            title: "Script imported by import shim"
            header: make system.standard.header compose [
                title: "Script imported by import shim"
                file: (file)
            ]
            parent: old-system-script
            path: lib3/split-path file
            args: either old-system-script.path [_] [system.options.args]
        ]

        ; Note: want the file-like behavior of preserving the directory.  :-(
        ; Implement via wrapper.
        ;
        f.source: rewrite-source-for-bootstrap-exe read:string file

        ; We do not want top-level set-words to be automatically cleared out,
        ; in case you plan to overwrite something like IF but are using the
        ; old definition.
        ;
        replace f.source "Type: module" ""

        ; Wrap the whole thing in an object if needed
        ;
        replace:one f.source unspaced [newline "]"] unspaced compose [
            newline
            "]" newline
            (if yes? wrap-module ["make object! ["]) newline
        ]
        if yes? wrap-module [
            lib3/append f.source newline
            lib3/append f.source ("]  ; end wrapping MAKE OBJECT!")
        ]
        wrap-module: 'no  ; only wrap one level of DO
    ]
    eval f
    elide system.script: old-system-script
]

already-imported: make map! []  ; avoid importing things twice


; see header notes: `Exports` broken
/import: infix lib3/func [
    "%import-shim.r variant of IMPORT which acts like DO and loads only once"

    :set-word "optional left argument, used by `rebmake: import <rebmake.r>`"
        [<skip> set-word?!]  ; old <skip>!

    f [tag!]  ; help catch mistakes, all bootstrap uses TAG!

    ; !!! The new import has an /INTO option; add it even though we don't
    ; support it here to serve as a signal the shim was applied.
    ;
    /into "Signal that the shim has been applied"

    ; NOTE: LET is unavailable (we have not run the bootstrap shim yet)
    ;
    <local> ret dir full-script-dir full-script-path old-dir code
            script-filename
    <with> wrap-module already-imported
][
    if into [
        fail ":INTO not actually available, just makes IMPORT look modern"
    ]

    f: as file! f

    dir: lib3/split-path:file f $script-filename

    assert [#"/" <> dir.1]  ; should be relative
    assert [#"%" <> dir.1]  ; accidental `import <%foo.r>`

    full-script-dir: clean-path lib3/append copy any [
        system.script.path system.options.path
    ] dir

    full-script-path: join full-script-dir script-filename

    if ret: select already-imported full-script-path [
        ; print ["ALREADY IMPORTED:" full-script-path]
        return ret
    ]
    ; print ["IMPORTING" full-script-path]

    old-dir: what-dir
    change-dir full-script-dir  ; modules expect to run in their directory

    ret: #quit
    catch:quit [
        ret: if :set-word [
            wrap-module: 'yes
            set set-word do script-filename
        ] else [
            assert [no? wrap-module]
            do script-filename
            #imported
        ]
    ]

    change-dir old-dir

    already-imported.(full-script-path): ret
    return ret
]


=== "LOAD WRAPPING" ===

; see header notes: `Exports` broken
/load: adapt lib3.load/ [  ; source [file! url! text! binary! block!]
    if all [  ; ALL THEN does not seem to work in bootstrap EXE
        file? source
        not dir? source
    ][use [item] [
        source: rewrite-source-for-bootstrap-exe read:string source
        source: next find source unspaced ["]" newline]  ; skip header
    ]]
]

; Poor-man's export in a non-working R3-Alpha module system.
;
append lib compose [
    lib3: (lib3)
    import: (import/)
    do: (do/)
    export: (export/)
    load: (load/)
]

print "COMPLETE!"
