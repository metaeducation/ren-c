REBOL [
    System: "Ren-C Core Extraction of the Rebol System"
    Title: "Common Parsers for Tools"
    Type: module
    Name: Common-Parsers
    Rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren-C is Copyright 2015-2018 MetaEducation
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "@codebybrett"
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        These are some common routines used by the utilities
        that build and test the system.
    }
]

import <bootstrap-shim.r>

c-lexical: import <c-lexicals.r>
import <text-lines.reb>
import <parsing-tools.reb>

load-until-blank: func [
    {Load rebol values from text until double newline.}
    text [text!]
    /next {Return values and next position.}
    <local> position  ; no LET in parse :-/
][
    let wsp: compose [some (charset { ^-})]

    let res: null  ; !!! collect as SET-WORD!s for locals, evolving...
    let rebol-value: parsing-at x [
        attempt [transcode/next x inside [] 'res]
        res
    ]

    let terminator: [opt wsp newline opt wsp newline]

    parse2 text [
        some [not terminator rebol-value]
        opt wsp opt [newline opt newline] position:  ; <here>
        to end
    ] then [
        values: load copy/part text position
        return reduce [values position]
    ]

    return null
]


collapse-whitespace: [some [change some white-space (space) | skip] end]
bind collapse-whitespace c-lexical/grammar


export proto-parser: context [

    unsorted-buffer: ~
    file: ~

    emit-fileheader: null
    emit-proto: null
    emit-directive: null

    parse-position: ~
    notes: ~
    lines: ~
    proto-id: ~
    proto-arg-1: ~
    data: ~
    eoh: ~ ; End of file header.

    count: ~

    process: func [return: [~] text] [
        parse2 text grammar/rule
    ]

    grammar: context bind [

        rule: [
            parse-position:  ; <here>
            opt fileheader
            opt some [
                parse-position:  ; <here>
                segment
            ]
        ]

        fileheader: [
            (data: null)
            doubleslashed-lines
            and is-fileheader
            eoh:  ; <here>
            (
                emit-fileheader data
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
            copy data [
                ["#ifndef" | "#ifdef" | "#if" | "#else" | "#elif" | "#endif"]
                opt some [not newline c-pp-token]
            ] eol
            (
                emit-directive data
            )
        ]

        ; We COPY/DEEP here because this part gets invasively modified by
        ; the source analysis tools.
        ;
        other-segment: copy/deep [thru newline]

        ; we COPY/DEEP here because this part gets invasively modified by
        ; the source analysis tools.
        ;
        format-func-section: copy/deep [
            doubleslashed-lines
            ahead is-intro
            function-proto opt some white-space
            function-body
            (
                ; EMIT-PROTO doesn't want to see extra whitespace (such as
                ; when individual parameters are on their own lines).
                ;
                parse2 proto collapse-whitespace
                proto: trim proto
                assert [find proto "("]

                if find proto "()" [
                    print [
                        proto
                        newline
                        {C-Style no args should be foo(void) and not foo()}
                        newline
                        http://stackoverflow.com/q/693788/c-void-arguments
                    ]
                    fail "C++ no-arg prototype used instead of C style"
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

        doubleslashed-lines: [copy lines some ["//" thru newline]]

        is-fileheader: parsing-at position [
            all [  ; note: not LOGIC!, a series
                lines: attempt [decode-lines lines {//} { }]
                parse2 lines [copy data to {=///} to end]
                data: attempt [load-until-blank trim/auto data]
                data: attempt [
                    if set-word? first data/1 [data/1] else [false]
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
        is-intro: parsing-at position [
            all [
                lines: attempt [decode-lines lines {//} { }]
                data: load-until-blank lines

                any [
                    set-word? first data/1
                    'export = first data/1
                ] then [
                    notes: data/2
                    data: data/1
                ] else [
                    data: notes: ~
                    false
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
            | "(*)" | "(const*)"
            | "(const *)" (fail "use (const*) not (const *)")
            | "(const Cell*)"
            | "(const Cell* )" (fail "use (const Cell*) not (const Cell* )")
        ]

        function-proto: [
            copy proto [
                not white-space
                some [
                    typemacro-parentheses
                    | [
                        not "(" not "="
                        [white-space | copy proto-id identifier | skip]
                    ]
                ]
                "("
                opt some white-space
                opt [
                    not typemacro-parentheses
                    not ")"
                    copy proto-arg-1 identifier
                ]
                opt some [typemacro-parentheses | not ")" [white-space | skip]]
                ")"
            ]
        ]

    ] c-lexical/grammar
]

export rewrite-if-directives: func [
    {Bottom up rewrite conditional directives to remove unnecessary sections.}
    return: [~]
    position
][
    until [
        let rewritten
        parse2 position [
            (rewritten: false)
            some [
                [
                    change ["#if" thru newline "#endif" thru newline] ("")
                    | change ["#elif" thru newline "#endif"] ("#endif")
                    | change ["#else" thru newline "#endif"] ("#endif")
                ] (rewritten: true)
                :position  ; seek

              | thru newline
            ]
            end
        ]
        not rewritten
    ]
]
