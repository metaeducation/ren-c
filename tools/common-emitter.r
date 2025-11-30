Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Common Code for Emitting Text Files"
    type: module
    name: Common-Emitter
    rights: --[
        Copyright 2016-2024 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    purpose: --[
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
    ]--
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
    "Escape Rebol expressions in templated C source, returns new string"

    return: "${} TO-C-NAME, $<> UNSPACED, $[]/$() DELIMIT closed/open"
        [text! file!]
    template "${Expr} case as-is, ${expr} lowercased, ${EXPR} is uppercased"
        [text! file! block!]
    <local> col start finish prefix suffix expr mode pattern void-marker
][
    if match [text! file!] template [
        template: reduce [template]
    ]

    let return-type: type of last template

    if (text! <> return-type) and (file! <> return-type) [
        panic ["CSCAPE requires TEXT! or FILE! as template:" mold last template]
    ]

    let string: trim:auto to text! last template

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
                        take:last expr
                        suffix: " "
                    ]
                    if #"," = last expr [  ; delimit with comma [2]
                        take:last expr
                        prefix: if suffix [
                            unspaced ["," suffix]  ; do both [3]
                        ] else [
                            ","
                        ]
                    ]
                )
                    |
                (prefix: copy:part start finish)
                "$[" change [expr: across to "]"] (num-text) one (
                    mode: #delimit
                    pattern: unspaced ["$[" num "]"]
                )
                suffix: across to newline
                    |
                (prefix: copy:part start finish)
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
        return as return-type string
    ]

    list: unique:case list

    void-marker: "!?*VOID*?!"  ; should be taken out, good to disrupt if not

    let substitutions: collect [
        for-each 'item list [
            ;
            ; SET no longer takes BLOCK!, and bootstrap executable doesn't have
            ; SET-BLOCK! so no UNPACK.
            ;
            pattern: degrade item.1
            col: item.2
            mode: item.3
            expr: item.4
            prefix: degrade item.5
            suffix: degrade item.6

            let any-upper: did find:case expr charset [#"A" - #"Z"]
            let any-lower: did find:case expr charset [#"a" - #"z"]
            keep opt pattern

            ; With binding being case-sensitive, we lowercase the expression.
            ; Since we do the lowercasing before the load, embedded string
            ; literals will also wind up being lowercase.  It would be more
            ; inconvenient to deep traverse the splice after loading to only
            ; lowercase ANY-WORD!s, so this is considered fine
            ;
            let code: transcode lowercase expr

            code: cscape-inside template code

            let sub: lift eval code

            ; We want to recognize lines that had substitutions that all
            ; vanished, and remove them (distinctly from lines left empty
            ; on purpose in the template).  We need to put some kind of signal
            ; to get that behavior.
            ;
            if void? unlift sub [
                keep void-marker  ; replaced in post phase
                continue
            ]

            sub: unlift sub

            if null? sub  [
                print mold template
                print mold code
                panic "Substitution can't be NULL"
            ]

            sub: switch mode [
                #cname [
                    ; !!! The #prefixed scope is unchecked for valid global or
                    ; local identifiers.  This is okay for cases that actually
                    ; are prefixed, like `cscape {SYM_${...}}`.  But if there
                    ; is no prefix, then the check might be helpful.  Review.
                    ;
                    to-c-name:scope sub #prefixed
                ]
                #unspaced [
                    if block? sub [
                        any [
                            either prefix [
                                delimit:tail prefix sub
                            ][
                                unspaced sub
                            ]
                            either prefix [void-marker] [null]
                            panic [
                                "No vaporizing blocks in CSCAPE $<>" newline
                                mold:limit template 200
                            ]
                        ]
                    ] else [
                        form sub  ; UNSPACED doesn't take INTEGER!, should it?
                    ]
                ]
                #delimit [
                    delimit (unspaced [opt suffix newline]) sub else [
                        panic [
                            "No vaporizing blocks in CSCAPE $() or $[]" newline
                            mold:limit template 200
                        ]
                    ]
                ]
                panic ["Invalid CSCAPE mode:" mode]
            ]

            assert [not null? sub]

            case [
                any-upper and (not any-lower) [
                    uppercase sub
                ]
                any-lower and (not any-upper) [
                    lowercase sub
                ]
            ]

            ; If the substitution started at a certain column, make any line
            ; breaks continue at the same column.
            ;
            let indent: unspaced [newline opt prefix]
            replace sub newline indent

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
        (allwhite: 'yes, seen-void: 'no) start-line: <here>
        opt some [
            space
            |
            newline
            [
                end-line: <here>
                (all [
                    yes? allwhite
                    yes? seen-void
                    end-line != next start-line
                ][
                    insert kill-lines start-line  ; back to front for delete
                    insert kill-lines end-line
                ])
            ]
            (allwhite: 'yes, seen-void: 'no) start-line: <here>
            |
            [
                void-marker  ; e.g. "!?*VOID*?!"
                (seen-void: 'yes)
                |
                (allwhite: 'no)  ; has something not a newline or space in it
                one
            ]
        ]
    ]

    for-each [start end] kill-lines [
        remove:part start end
    ]

    replace string void-marker ""

    return as return-type string
]


export boot-version: transcode:one read %../src/specs/version.r

export make-emitter: func [
    "Create a buffered output text file emitter"

    return: [object!]
    title "Title for the comment header (header matches file type)"
        [text!]
    file "Filename to be emitted... .r/.reb/.c/.h/.inc files supported"
        [file!]
    :temporary "DO-NOT-EDIT warning (automatic if file begins with 'tmp-')"
][
    if not let by: system.script.header.file [
        panic [
            "File: should be set in the generating scripts header section"
            "so that generated files have a comment on what made them"
        ]
    ]

    print cscape [title file
        --[Generating "$<Title>" ($<Mold File>)]--
    ]

    let stem
    split-path3:file file $stem

    temporary: boolean any [
        temporary
        parse3:match stem ["tmp-" to <end>]
    ]

    let is-c: did parse3:match stem [thru [".c" | ".h" | ".inc"] <end>]

    let is-js: did parse3:match stem [thru ".js" <end>]

    let e: construct compose [
        ;
        ; NOTE: %make-headers.r directly manipulates the buffer, because it
        ; wishes to merge #ifdef and #endif cases
        ;
        ; !!! Should the allocation size be configurable?
        ;
        buf-emit: make text! 32000

        file: (file)  ; need COMPOSE1 for bootstrap
        title: (title)

        emit: proc [
            "Write data to the emitter using CSCAPE templating (see HELP)"

            template "Adds newline if BLOCK! (use EMIT CSCAPE [...] to avoid)"
                [text! block! char?]
            <.>
        ][
            case [  ; no switch:type in bootstrap
                text? template [
                    if not find template #"$" [
                        append .buf-emit trim:auto copy template
                    ]
                    else [
                        append .buf-emit cscape reduce [template]
                    ]
                ]
                char? template [
                    append .buf-emit template
                ]
                block? template [
                    append .buf-emit cscape template
                    append .buf-emit newline
                ]
                panic
            ]
        ]

        write-emitted: proc [
            :tabbed
            <.>
        ][
            if newline != last .buf-emit [
                probe skip (tail of .buf-emit) -100
                panic "WRITE-EMITTED needs NEWLINE as last character in buffer"
            ]

            if let tab-pos: find .buf-emit tab [
                probe skip tab-pos -100
                panic "tab character passed to emit"
            ]

            if tabbed [
                replace .buf-emit spaced-tab tab
            ]

            write-if-changed .file .buf-emit

            ; For clarity/simplicity, emitters are not reused.
            ;
            .file: ~
            .buf-emit: ~
        ]
    ]

    any [is-c is-js] then [
        e/emit [title boot-version stem by --[
            /**********************************************************************
            **
            **  Rebol [R3] Language Interpreter and Run-time Environment
            **  Copyright 2012 REBOL Technologies
            **  Copyright 2012-2024 Ren-C Open Source Contributors
            **  REBOL is a trademark of REBOL Technologies
            **  Licensed under the Apache License, Version 2.0
            **
            ************************************************************************
            **
            **  title: $<Mold Title>
            **  build: A$<Boot-Version.3>
            **  file: $<Mold Stem>
            **  author: $<Mold By>
            **  license: --[
            **      Licensed under the Apache License, Version 2.0.
            **      See: http://www.apache.org/licenses/LICENSE-2.0
            **  ]--
            **  notes: "!!! AUTO-GENERATED FILE !!!"
            **
            ***********************************************************************/
        ]--]
    ]
    else [
        e/emit [title stem --[
            Rebol [
                system: "Rebol [R3] Language Interpreter and Run-time Environment"
                title: $<mold title>
                file: $<mold stem>
                rights: --[
                    Copyright 2012 REBOL Technologies
                    Copyright 2012-2025 Ren-C Open Source Contributors
                    REBOL is a trademark of REBOL Technologies
                ]--
                license: --[
                    Licensed under the Apache License, Version 2.0.
                    See: http://www.apache.org/licenses/LICENSE-2.0
                ]--
                notes: "!!! AUTO-GENERATED FILE !!!"
            ]
        ]--]
    ]
    return e
]
