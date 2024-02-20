Rebol [
    Title: "Compatibility IMPORT/EXPORT for Bootstrap"
    File: %import-shim.r
    Type: module

    ; SEE NOTES BELOW: r3-alpha module system fully broken in bootstrap  EXE
    ;Name: Import-Shim  ; without Name: will act like `Options: [private]`
    ;Exports: [lib3 import export load]

    Description: {
        This shim redefines IMPORT and EXPORT for the bootstrap executable:

        * It uses plain DO of the code so that the `Type: module` in the
          header is ignored.  That just puts everything into the user context,
          which is ugly but made to work in order to use the old executable
          to build.  It also avoids importing things more than once, so it
          achieves a similar semantic to modules.

        * It preprocesses the source, so that <{...}> strings are turned into
          legacy {...} strings.  ({...} are FENCE! arrays in modern Ren-C)

        * It removes commas from non-strings in source, so that commas can be
          used in boostrap code.  This doesn't act as expression barriers, but
          helps with readability (and new executables will see them as
          expression barriers and enforce them).
    }
    Usage: {
        To affect how code is loaded, the import shim has to be hooked in
        before your script is running (so you can't `do %import-shim.r`).

        Fortunately, there is an --import option to preload modules that
        existed in the bootstrap executable.

            r3 --import import-shim.r make.r [OPTIONS]

        Newer executables don't need this shim, just run the script plain:

            r3 make.r [OPTIONS]
    }
    Notes: {
      * !!! R3-Alpha Module System Was Completely Broken !!!, so we manually
        overwrite the definitions in lib instead of using Exports:  Not even
        that worked in pre-R3C builds, so it forced an update of the bootstrap
        executable to R3C when we started using the `--import` option.

      * DO LOAD is used instead of DO to avoid the legacy behavior of changing
        to the directory of %import-shim.r when running a FILE! (vs. a BLOCK!)
        This also keeps it from resetting the directory when the DO is over,
        so we can CHANGE-DIR from this shim to compensate for the changing
        into the calling script's directory...making it appear that the
        command line processing never switched the dir in the first place.
    }
]


if not trap [
    :import/into  ; no error here means already shimmed, or EXE is new
][
    print ""
    print "!!! %import-shim.r is only for use with old Ren-C EXEs"
    print ""
    quit/with 1
]


write-stdout "LOADING %import-shim.r --- "  ; when finished, adds "COMPLETE!"


; Because the semantics vary greatly, we'd like it to be clear to readers
; when something like APPEND or FUNC are using old rules.  LIB3/APPEND and
; LIB3/FUNC are good ways of seeing that.
;
append lib [lib3: _]  ; see header notes: `Exports` broken
lib/lib3: lib3: lib  ; use LIB3 to make it clearer when using old semantics


=== "EXPORT" ===

append lib [export: _]  ; see header notes: `Exports` broken
lib3/export: export: lib3/func [
    "%import-shim.r variant of EXPORT which just puts the definition into LIB"

    :set-word [<skip> set-word!]  ; old style unescapable literal
    args "`export x: ...` for single or `export [...]` for words list"
        [<opt> any-value! <...>]  ; <...> is old-style variadic indicator
    <local>
        items
][
    if :set-word [
        args: take args
        lib3/append system/contexts/user reduce [  ; splices blocks by default
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
        lib3/append system/contexts/user reduce [  ; splices blocks by default
            word get word
        ]
    ]
]


=== "SOURCE CONVERSION" ===

strip-commas-and-downgrade-strings: lib3/func [
    "Remove commas from non-string portions of the code, turn <{...}> to {...}"
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
            skip
        ]
        end
    ]

    lib3/parse source rule else [
        fail "STRIP-COMMAS-AND-DOWNGRADE-STRINGS did not work"
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
; can't use any words you redefine.  e.g. if you say `func: func [...] [...]`
; then that won't work because `make object! [func: ...]` unsets FUNC in
; advance.  We only use the trick if they're trying to set a result back,
; such as with:
;
;    rebmake: import <tools/rebmake.r>
;
wrap-module: false

old-do: :lib3/do
lib3/do: enclose :lib3/do lib3/func [
    f [frame!]
    <local> old-system-script file
    <with> wrap-module
][
    old-system-script: system/script

    if file? f/source [
        file: f/source

        system/script: make system/standard/script [
            title: "Script imported by import shim"
            header: make system/standard/header compose [
                title: "Script imported by import shim"
                file: (file)
            ]
            parent: old-system-script
            path: first split-path file
            args: either old-system-script/path [_] [system/options/args]
        ]

        ; We want to strip the commas, but we also want the file-like behavior
        ; of preserving the directory.  :-(  Implement via wrapper.
        ;
        f/source: strip-commas-and-downgrade-strings read/string file

        ; We do not want top-level set-words to be automatically cleared out,
        ; in case you plan to overwrite something like IF but are using the
        ; old definition.
        ;
        replace f/source "Type: module" ""

        ; Wrap the whole thing in an object if needed
        ;
        replace f/source unspaced [newline "]"] unspaced compose [
            newline
            "]" newline
            (if wrap-module ["make object! ["]) newline
        ]
        if wrap-module [
            append f/source newline
            append f/source "]  ; end wrapping MAKE OBJECT!"
        ]
        wrap-module: false  ; only wrap one level of DO
    ]
    old-do f
    elide system/script: old-system-script
]


already-imported: make map! []  ; avoid importing things twice


; see header notes: `Exports` broken
lib3/import: enfix lib3/func [
    "%import-shim.r variant of IMPORT which acts like DO and loads only once"

    :set-word "optional left argument, used by `rebmake: import <rebmake.r>`"
        [<skip> set-word!]

    f [tag!]  ; help catch mistakes, all bootstrap uses TAG!

    ; !!! The new import has an /INTO option; add it even though we don't
    ; support it here to serve as a signal the shim was applied.
    ;
    /into "Signal that the shim has been applied"

    ; NOTE: LET is unavailable (we have not run the bootstrap shim yet)
    ;
    <local> ret path+file new-script-path old-dir code
    <with> wrap-module already-imported
][
    if into [
        fail "/INTO not actually available, just makes IMPORT look modern"
    ]

    f: as file! f

    if ret: select already-imported f [
        return ret
    ]

    path+file: lib3/split-path f

    assert [#"/" <> first path+file/1]  ; should be relative
    assert [#"%" <> first path+file/1]  ; accidental `import <%foo.r>`

    new-script-path: clean-path lib3/append copy any [
        system/script/path system/options/path
    ] path+file/1

    old-dir: what-dir
    change-dir new-script-path  ; modules expect to run in their directory

    ret: #quit
    catch/quit [
        ret: if :set-word [
            wrap-module: true
            set set-word do path+file/2
        ] else [
            assert [not wrap-module]
            do path+file/2
            #imported
        ]
    ]

    change-dir old-dir

    already-imported/(f): ret
    return ret
]


=== "LOAD WRAPPING" ===

; see header notes: `Exports` broken
lib3/load: adapt :lib3/load [  ; source [file! url! text! binary! block!]
    if all [  ; ALL THEN does not seem to work in bootstrap EXE
        file? source
        not dir? source
    ][use [item] [
        source: strip-commas-and-downgrade-strings read/string source
        source: next find source unspaced ["]" newline]  ; skip header
    ]]
]

print "COMPLETE!"
