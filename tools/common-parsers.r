Rebol [
    system: "Ren-C Core Extraction of the Rebol System"
    title: "Common Parsers for Tools"
    type: module
    name: Common-Parsers
    rights: --[
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren-C is Copyright 2015-2018 MetaEducation
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    author: "@codebybrett"
    version: 2.100.0
    needs: 2.100.100
    purpose: --[
        These are some common routines used by the utilities
        that build and test the system.
    ]--
]

import <bootstrap-shim.r>

c-lexical: import <c-lexicals.r>
import <text-lines.reb>
import <parsing-tools.reb>

load-until-double-newline: func [
    "Load rebol values from text until double newline."
    text [text!]
    <local> position  ; no LET in parse3 :-/
][
    let wsp: compose [some (charset -[ ^-]-)]

    let dummy  ; :NEXT3 requires arg
    let rebol-value: parsing-at 'x [
        try transcode:next3 x $dummy  ; transcode gives pos, null, or error
    ]

    let terminator: [opt wsp newline opt wsp newline]

    parse3:match text [
        some [not ahead terminator rebol-value]
        opt wsp opt [newline opt newline] position: <here>
        to <end>
    ] then [
        let values: load3 copy:part text position
        return reduce [values position]
    ]

    return null
]


collapse-whitespace: bind:copy3 c-lexical.grammar [
    some [change some white-space (space) | one] <end>
]

export proto-parser: context [

    unsorted-buffer: ~
    file: ~

    emit-fileheader: ~
    emit-proto: ~
    emit-directive: ~

    parse-position: ~
    notes: ~
    lines: ~
    proto-id: ~
    proto-arg-1: ~
    data: ~
    eoh: ~ ; End of file header.

    count: ~

    process: func [return: [] text] [
        parse3 text grammar.rule

        emit-fileheader: ~
        emit-proto: ~
        emit-directive: ~
    ]

    grammar: context bind:copy3 c-lexical.grammar [

        rule: [
            parse-position: <here>
            opt fileheader
            opt some [
                parse-position: <here>
                segment
            ]
        ]

        fileheader: [
            (data: null)
            doubleslashed-lines
            ahead is-fileheader
            eoh: <here>
            (
                if set? $emit-fileheader [
                    emit-fileheader data
                ]
            )
        ]

        segment: [
            (proto-id: proto-arg-1: null)
            format-func-section
            | span-comment
            | line-comment opt some [newline line-comment] newline
            | opt wsp directive
            | other-segment
        ]

        directive: [
            data: across [
                ["#ifndef" | "#ifdef" | "#if" | "#else" | "#elif" | "#endif"]
                opt some [not ahead newline c-pp-token]
            ] eol
            (
                if set? $emit-directive [
                    emit-directive data
                ]
            )
        ]

        ; We COPY:DEEP here because this part gets invasively modified by
        ; the source analysis tools.
        ;
        other-segment: copy:deep [thru newline]

        ; we COPY:DEEP here because this part gets invasively modified by
        ; the source analysis tools.
        ;
        format-func-section: copy:deep [
            doubleslashed-lines
            ahead is-intro
            function-proto opt some white-space
            function-body
            (
                ; EMIT-PROTO doesn't want to see extra whitespace (such as
                ; when individual parameters are on their own lines).
                ;
                parse3 proto collapse-whitespace
                proto: trim proto
                assert [find proto "("]

                if find proto "()" [
                    print [
                        proto
                        newline
                        "C-Style no args should be foo(void) and not foo()"
                        newline
                        http://stackoverflow.com/q/693788/c-void-arguments
                    ]
                    panic "C++ no-arg prototype used instead of C style"
                ]

                ; Call the EMIT-PROTO hook that the client provided.  They
                ; receive the stripped prototype as a formal parameter, but
                ; can also examine state variables of the parser to extract
                ; other properties--such as the processed intro block.
                ;
                emit-proto proto
            )
        ]

        function-body: #"{"

        doubleslashed-lines: [lines: across some ["//" thru newline]]

        is-fileheader: parsing-at 'position [
            all [  ; note: not LOGIC!, a series
                lines: try decode-lines lines -[//]- -[ ]-
                parse3:match lines [data: across to -[=///]- to <end>]
                data: load-until-double-newline trim:auto data
                all [
                    data.1
                    block? data.1
                    set-word? first data.1
                    data: data.1
                ]
                position ; Success.
            ]
        ]

        ; Recognize a comment header on a function, e.g. `Some_Function: C`
        ; or `rebSomething: API`.  It does this by trying to LOAD the lines,
        ; and if it succeeds looks for SET-WORD! or EXPORT of SET-WORD!
        ;
        ; !!! Could this be simpler, e.g. go by noticing DECLARE_NATIVE() etc?
        ;
        is-intro: parsing-at 'position [
            all [
                lines: try decode-lines lines -[//]- -[ ]-
                data: load-until-double-newline lines

                any [
                    set-word? first data.1
                    'export = first data.1
                ] then [
                    notes: data.2
                    data: data.1
                ] else [
                    data: notes: ~
                    null
                ]
                position  ; return the start position (e.g. stay at head)
            ]
        ]

        ; http://blog.hostilefork.com/kinda-smart-pointers-in-c/
        ;
        ;     TYPEMACRO(*) Some_Function(TYPEMACRO(const*) value, ...)
        ;     { ...
        ;
        typemacro-parentheses: [
            "Option(" opt [identifier "(" thru ")"] thru ")"
            | "Sink(" opt [identifier "(" thru ")"] thru ")"
            | "Option(Sink(" opt [identifier "(" thru ")"] thru "))"
            | "Need(" opt [identifier "(" thru ")"] thru ")"
            | "Option(Need(" opt [identifier "(" thru ")"] thru "))"
            | "Init(" opt [identifier "(" thru ")"] thru ")"
            | "Option(Init(" opt [identifier "(" thru ")"] thru "))"
            | "(*)" | "(const*)"
            | "(const *)" (panic "use (const*) not (const *)")
            | "(const Cell*)"
            | "(const Cell* )" (panic "use (const Cell*) not (const Cell* )")
        ]

        proto: ~

        function-proto: [
            proto: across [
                not ahead white-space
                some [
                    typemacro-parentheses
                    | [
                        not ahead "(" not ahead "="
                        [white-space | proto-id: across identifier | one]
                    ]
                ]
                "("
                opt some white-space
                opt [
                    not ahead typemacro-parentheses
                    not ahead ")"
                    proto-arg-1: across identifier
                ]
                opt some [
                    typemacro-parentheses | not ahead ")" [white-space | one]
                ]
                ")"
            ]
        ]

    ]
]

export rewrite-if-directives: func [
    "Bottom up rewrite conditional directives to remove unnecessary sections"
    return: []
    position
][
    insist [
        let rewritten
        parse3:match position [
            (rewritten: 'no)
            some [
                [
                    change ["#if" thru newline "#endif" thru newline] ("")
                    | change ["#elif" thru newline "#endif"] ("#endif")
                    | change ["#else" thru newline "#endif"] ("#endif")
                ] (rewritten: 'yes)
                seek position

              | thru newline
            ]
            <end>
        ]
        no? rewritten
    ]
]
