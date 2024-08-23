REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Common Code for Emitting Text Files"
    Type: module
    Name: Common-Emitter
    Rights: {
        Copyright 2016-2024 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        While emitting text files isn't exactly rocket science, it can help
        to have a few sanity checks on the process.

        The features added here vs. writing lines oneself are:

        * Some awareness of C constructs and the automatic conversion of
          Rebol symbols to valid C identifiers, as well as a not-too-invasive
          method for omitting commas from the ends of enum or initializer
          lists (they're not legal in standard C or C++).

        * Not allowing whitespace at the end of lines, tab characters in the
          output, some abilities to manage indentation.

        * Automatically generating C comment headers or Rebol headers, and
          including "DO NOT EDIT" warnings on temporary files.

        * Being able to use the information of the file and title being
          generated to provide notifications of what work is currently in
          progress to make errors easier to locate.
    }
]

import <bootstrap-shim.r>

import <common.r>  ; for REPO-DIR
import <platforms.r>  ; for BOOT-VERSION

; 1. If you say $<content > then if the contents don't vanish, a space will be
;    included at the end.  But if it vanishes, there will be no space either.
;
; 2. If you say $<block,> then it will delimit the block with comma.  And if
;    block is empty then it will vanish (potentially vanishing the line).
;
; 3. These can be combined, so that $<xxx, > will delimit with ", "
;
export cscape: func [
    {Escape Rebol expressions in templated C source, returns new string}

    return: "${} TO-C-NAME, $<> UNSPACED, $[]/$() DELIMIT closed/open"
        [text!]
    template "${Expr} case as-is, ${expr} lowercased, ${EXPR} is uppercased"
        [block!]
    <local> col prefix suffix mode pattern void-marker
][
    assert [text? last template]

    let string: trim/auto copy last template

    ; As we process the string, we CHANGE any substitution expressions into
    ; an INTEGER! for doing the replacements later with REWORD (and not
    ; being ambiguous).
    ;
    let num: 1
    let num-text: to text! num  ; CHANGE won't take GROUP! to evaluate, #1279

    let list: collect* [
        parse3 string [(col: 0), start: <here>
          opt some [
            [
                (prefix: null suffix: null)
                finish: <here>

                "${" change [expr: across to "}"] (num-text) one (
                    mode: #cname
                    pattern: unspaced ["${" num "}"]
                )
                    |
                "$<" change [expr: across to ">"] (num-text) one (
                    mode: #unspaced
                    pattern: unspaced ["$<" num ">"]
                    if space = last expr [  ; add space at end [1]
                        take/last expr
                        suffix: " "
                    ]
                    if #"," = last expr [  ; delimit with comma [2]
                        take/last expr
                        prefix: if suffix [
                            unspaced ["," suffix]  ; do both [3]
                        ] else [
                            ","
                        ]
                    ]
                )
                    |
                (prefix: copy/part start finish)
                "$[" change [expr: across to "]"] (num-text) one (
                    mode: #delimit
                    pattern: unspaced ["$[" num "]"]
                )
                suffix: across to newline
                    |
                (prefix: copy/part start finish)
                "$(" change [expr: across to ")"] (num-text) one (
                    mode: #delimit
                    pattern: unspaced ["$(" num ")"]
                )
                suffix: across remove to newline
            ] (
                keep compose [
                    (reify pattern) (col) (mode) (expr)
                    (reify prefix) (reify suffix)
                ]
                num: num + 1
                num-text: to text! num
            )
                |
            newline
            (col: 0 prefix: null suffix: null)
            start: <here>
                |
            one (col: col + 1)
        ]]
    ] else [  ; COLLECT* was NULL, so no substitutions
        return string
    ]

    list: unique/case list

    void-marker: "!?*VOID*?!"  ; should be taken out, good to disrupt if not

    let substitutions: collect [
        for-each item list [
            ;
            ; SET no longer takes BLOCK!, and bootstrap executable doesn't have
            ; SET-BLOCK! so no UNPACK.
            ;
            pattern: degrade item/1
            col: item/2
            mode: item/3
            expr: item/4
            prefix: degrade item/5
            suffix: degrade item/6

            let any-upper: did find/case expr charset [#"A" - #"Z"]
            let any-lower: did find/case expr charset [#"a" - #"z"]
            keep maybe pattern

            ; With binding being case-sensitive, we lowercase the expression.
            ; Since we do the lowercasing before the load, embedded string
            ; literals will also wind up being lowercase.  It would be more
            ; inconvenient to deep traverse the splice after loading to only
            ; lowercase ANY-WORD!s, so this is considered fine
            ;
            ; !!! Needs LOAD-ALL shim hack for bootstrap since /ALL deprecated
            ;
            let code: transcode lowercase expr

            code: cscape-inside template code

            if null? let sub: eval code [  ; shim null, e.g. blank!
                print mold template
                print mold code
                fail "Substitution can't be NULL (shim BLANK!)"
            ]

            ; We want to recognize lines that had substitutions that all
            ; vanished, and remove them (distinctly from lines left empty
            ; on purpose in the template).  We need to put some kind of signal
            ; to get that behavior.
            ;
            if void? :sub [
                keep void-marker  ; replaced in post phase
                continue
            ]

            sub: switch mode [
                #cname [
                    ; !!! The #prefixed scope is unchecked for valid global or
                    ; local identifiers.  This is okay for cases that actually
                    ; are prefixed, like `cscape {SYM_${...}}`.  But if there
                    ; is no prefix, then the check might be helpful.  Review.
                    ;
                    to-c-name/scope sub #prefixed
                ]
                #unspaced [
                    if block? sub [
                        any [
                            either prefix [
                                delimit/tail prefix sub
                            ][
                                unspaced sub
                            ]
                            either prefix [void-marker] [null]
                            fail ["No vaporizing blocks in CSCAPE $<>"]
                        ]
                    ] else [
                        form sub  ; UNSPACED doesn't take INTEGER!, should it?
                    ]
                ]
                #delimit [
                    delimit (unspaced [maybe :suffix newline]) sub else [
                        fail ["No vaporizing blocks in CSCAPE $() or $[]"]
                    ]
                ]
                fail ["Invalid CSCAPE mode:" mode]
            ]

            assert [not null? :sub]

            case [
                all [any-upper, not any-lower] [
                    uppercase sub
                ]
                all [any-lower, not any-upper] [
                    lowercase sub
                ]
            ]

            ; If the substitution started at a certain column, make any line
            ; breaks continue at the same column.
            ;
            let indent: unspaced [newline maybe :prefix]
            replace/all sub newline indent

            keep sub
        ]
    ]

    for-each [pattern replacement] substitutions [
        replace string pattern replacement
    ]

    ; void in CSCAPE tries to be "smart" about omitting the item from its
    ; surrounding context, including removing lines when void output and
    ; whitespace is all that ends up on them.  If the user doesn't want the
    ; intelligence, they should use "".
    ;
    ; !!! REMOVE was buggy and unpredictable in R3-Alpha PARSE.  This may be
    ; fixed in the modern bootstrap executable, but this code does not
    ; reflect that.  It collects a list of lines to kill and does it in a
    ; phase after the parse.
    ;
    let kill-lines: copy []
    let allwhite
    let seen-void
    let start-line
    let end-line
    parse3 string [
        (allwhite: true seen-void: false) start-line: <here>
        opt some [
            space
            |
            newline
            [
                ; PARSE arity-1 IF deprecated in Ren-C, but :(...) with logic
                ; not available in the bootstrap build.
                ;
                end-line: <here>
                (if allwhite and (seen-void) and (end-line != next start-line) [
                    insert kill-lines start-line  ; back to front for delete
                    insert kill-lines end-line
                ])
            ]
            (allwhite: true seen-void: false) start-line: <here>
            |
            [
                void-marker  ; e.g. "!?*VOID*?!"
                (seen-void: true)
                |
                (allwhite: false)  ; has something not a newline or space in it
                one
            ]
        ]
    ]

    for-each [start end] kill-lines [
        remove/part start end
    ]

    replace/all string void-marker ""

    return string
]


export boot-version: load-value %../src/boot/version.r

export make-emitter: func [
    {Create a buffered output text file emitter}

    return: [object!]
    title "Title for the comment header (header matches file type)"
        [text!]
    file "Filename to be emitted... .r/.reb/.c/.h/.inc files supported"
        [file!]
    /temporary "DO-NOT-EDIT warning (automatic if file begins with 'tmp-')"

    <with>
    system  ; The `System:` SET-WORD! below overrides the global for access
][
    if not let by: system/script/header/file [
        fail [
            "File: should be set in the generating scripts header section"
            "so that generated files have a comment on what made them"
        ]
    ]

    print unspaced [{Generating "} title {" (via } by {)}]

    let stem
    split-path3/file file inside [] 'stem

    temporary: to-logic any [
        temporary
        parse3/match stem ["tmp-" to <end>]
    ]

    let is-c: did parse3/match stem [thru [".c" | ".h" | ".inc"] <end>]

    let is-js: did parse3/match stem [thru ".js" <end>]

    let e: make object! compose [
        ;
        ; NOTE: %make-headers.r directly manipulates the buffer, because it
        ; wishes to merge #ifdef/#endif cases
        ;
        ; !!! Should the allocation size be configurable?
        ;
        buf-emit: make text! 32000

        file: (file)
        title: (title)

        emit: func [
            {Write data to the emitter using CSCAPE templating (see HELP)}

            return: [~]
            template [text! char?! block!]
            <with> buf-emit
        ][
            case [  ; no switch/type in bootstrap
                text? template [
                    append buf-emit trim/auto copy template
                ]
                char? template [
                    append buf-emit template
                ]
            ] else [
                append buf-emit cscape template
            ]
        ]

        write-emitted: func [
            return: [~]
            /tabbed
            <with> file buf-emit
        ][
            if newline != last buf-emit [
                probe skip (tail-of buf-emit) -100
                fail "WRITE-EMITTED needs NEWLINE as last character in buffer"
            ]

            if let tab-pos: find buf-emit tab [
                probe skip tab-pos -100
                fail "tab character passed to emit"
            ]

            if tabbed [
                replace/all buf-emit spaced-tab tab
            ]

            print ["WRITING =>" file]

            write-if-changed file buf-emit

            ; For clarity/simplicity, emitters are not reused.
            ;
            file: ~
            buf-emit: ~
        ]
    ]

    any [is-c is-js] then [
        e/emit [title boot-version stem by {
            /**********************************************************************
            **
            **  REBOL [R3] Language Interpreter and Run-time Environment
            **  Copyright 2012 REBOL Technologies
            **  Copyright 2012-2024 Ren-C Open Source Contributors
            **  REBOL is a trademark of REBOL Technologies
            **  Licensed under the Apache License, Version 2.0
            **
            ************************************************************************
            **
            **  Title: $<Mold Title>
            **  Build: A$<Boot-Version/3>
            **  File: $<Mold Stem>
            **  Author: $<Mold By>
            **  License: {
            **      Licensed under the Apache License, Version 2.0.
            **      See: http://www.apache.org/licenses/LICENSE-2.0
            **  }
        }]
        if temporary [
            e/emit {
                **  Note: {AUTO-GENERATED FILE - Do not modify.}
            }
        ]
        e/emit {
            **
            ***********************************************************************/
        }
        e/emit newline
    ]
    else [
        e/emit {REBOL }  ; no COMPOSE/DEEP in bootstrap shim, yet
        e/emit mold spread compose [
            System: "REBOL [R3] Language Interpreter and Run-time Environment"
            Title: (title)
            File: (stem)
            Rights: {
                Copyright 2012 REBOL Technologies
                Copyright 2012-2018 Ren-C Open Source Contributors
                REBOL is a trademark of REBOL Technologies
            }
            License: {
                Licensed under the Apache License, Version 2.0.
                See: http://www.apache.org/licenses/LICENSE-2.0
            }
            (if temporary [
                spread [Note: {AUTO-GENERATED FILE - Do not modify.}]
            ])
        ]
        e/emit newline
    ]
    return e
]
