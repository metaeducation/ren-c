REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Help"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]


spec-of: function [
    {Generate a block which could be used as a "spec block" from an action.}

    return: [block!]
    action [action!]
][
    meta: try match object! meta-of :action

    specializee: try match action! select meta 'specializee
    adaptee: try match action! select meta 'adaptee
    original-meta: try match object! any [
        meta-of :specializee
        meta-of :adaptee
    ]

    return collect-block [
        keep/line ensure* text! any [
            select meta 'description
            select original-meta 'description
        ]

        return-type: try ensure* block! any [
            select meta 'return-type
            select original-meta 'return-type
        ]
        return-note: try ensure* text! any [
            select meta 'return-note
            select original-meta 'return-note
        ]
        if any [return-type return-note] [
            keep compose [
                return: ((opt return-type)) (opt return-note)
            ]
        ]

        types: try ensure* frame! any [
            select meta 'parameter-types
            select original-meta 'parameter-types
        ]
        notes: try ensure* frame! any [
            select meta 'parameter-notes
            select original-meta 'parameter-notes
        ]

        for-each param words of :action [
            keep compose [
                (param) ((select types param)) (select notes param)
            ]
        ]
    ]
]


title-of: function [
    {Extracts a summary of a value's purpose from its "meta" information.}

    value [any-value!]
][
    opt switch type of :value [
        action! [
            try all [
                meta: try match object! meta-of :value
                copy try match text! select meta 'description
            ]
        ]

        datatype! [
            spec: spec-of value
            assert [text? spec] ;-- !!! Consider simplifying "type specs"
            spec/title
        ]
    ]
]

browse: function [
    "stub function for browse* in extensions/process/ext-process-init.reb"

    return: <void>
    location [<blank> url! file!]
][
    print "Browse needs redefining"
]

help: function [
    "Prints information about words and values (if no args, general help)."

    return: <void>
    :topic [<end> any-value!]
        "WORD! whose value to explain, or other HELP target (try HELP HELP)"
    /doc
        "Open web browser to related documentation."
][
    if unset? 'topic [
        ;
        ; Was just `>> help` or `do [help]` or similar.
        ; Print out generic help message.
        ;
        print trim/auto copy {
            Use HELP to see built-in info:

                help insert

            To search within the system, use quotes:

                help "insert"

            To browse online topics:

                help #compiling

            To browse online documentation:

                help/doc insert

            To view words and values of a context or object:

                help lib    - the runtime library
                help self   - your user context
                help system - the system object
                help system/options - special settings

            To see all words of a specific datatype:

                help object!
                help action!
                help datatype!

            Other debug helpers:

                docs - open browser to web documentation
                dump - display a variable and its value
                probe - print a value (molded)
                source func - show source code of func
                trace - trace evaluation steps
                what - show a list of known functions
                why - explain more about last error (via web)

            Other information:

                about - see general product info
                bugs - open GitHub issues website
                changes - show changelog
                chat - open GitHub developer forum
                install - install (when applicable)
                license - show user license
                topics - open help topics website
                upgrade - check for newer versions
                usage - program cmd line options
        }
        return
    ]

    ; HELP quotes, but someone might want to use an expression, e.g.
    ; `help (...)`.  However, enfix functions which hard quote the left would
    ; win over a soft-quoting non-enfix function that quotes to the right.
    ; (It is generally discouraged to make hard-quoting left enfix functions,
    ; but they exist...e.g. DEFAULT.)  To make sure HELP DEFAULT works, HELP
    ; must hard quote and simulate its own soft quote semantics.
    ;
    if match [group! get-word! get-path!] :topic [
        topic: reeval topic else [
            fail "HELP requested on NULL" ;-- should this PRINT vs. FAIL?
        ]
    ]

    ; !!! R3-Alpha permitted "multiple inheritance" in objects, in the sense
    ; that it would blindly overwrite fields of one object with another, which
    ; wreaked havoc on the semantics of functions in unrelated objects.  It
    ; doesn't work easily with derived binding, and doesn't make a lot of
    ; sense.  But it was used here to unify the lib and user contexts to
    ; remove potential duplicates (even if not actually identical).  This
    ; does that manually, review.
    ;
    make-libuser: does [
        libuser: copy system/contexts/lib
        for-each [key val] system/contexts/user [
            if set? 'val [
               append libuser reduce [key :val]
            ]
        ]
        libuser
    ]

    switch type of :topic [
        issue! [ ;; HELP #TOPIC will browse r3n for the topic
            say-browser
            browse join-all [https://r3n.github.io/topics/ as text! topic]
            leave
        ]

        text! [
            types: dump-obj/match make-libuser :topic
            sort types
            if not empty? types [
                print ["Found these related words:" newline types]
                return
            ]
            print ["No information on" topic]
            return
        ]

        path! word! [
            value: get/any topic else [
                print ["No information on" topic "(has no value)"]
                return
            ]
            enfixed: enfixed? topic
        ]
    ] else [
        if free? :topic [
            print ["is a freed" mold type of :topic]
        ] else [
            print [mold :topic "is" an mold type of :topic]
        ]
        return
    ]

    ; Open the web page for it?
    if doc and [match [action! datatype!] :value] [
        item: form :topic
        if action? get :topic [
            ;
            ; !!! The logic here repeats somewhat the same thing that is done
            ; by TO-C-NAME for generating C identifiers.  It might be worth it
            ; to standardize how symbols are textualized for C with what the
            ; documentation uses (though C must use underscores, not hyphen)
            ;
            for-each [a b] [
                "!" "-ex"
                "?" "-q"
                "*" "-mul"
                "+" "-plu"
                "/" "-div"
                "=" "-eq"
                "<" "-lt"
                ">" "-gt"
                "|" "-bar"
            ][
                replace/all item a b
            ]

            browse join-all [
                https://github.com/gchiu/reboldocs/blob/master/
                item
                %.MD
            ]
        ] else [
            remove back tail of item ;-- it's a DATATYPE!, so remove the !
            browse join-all [
                http://www.rebol.com/r3/docs/datatypes/
                item
                tmp: %.html
            ]
        ]
    ]

    if datatype? :value [
        types: dump-obj/match make-libuser :topic
        if not empty? types [
            print ["Found these" (uppercase form topic) "words:" newline types]
        ] else [
            print [topic {is a datatype}]
        ]
        return
    ]

    if not action? :value [
        print collect [
            keep [(uppercase mold topic) "is" an (mold type of :value)]
            if free? :value [
                keep "that has been FREEd"
            ] else [
                keep "of value:"
                if match [object! port!] value [
                    keep newline
                    keep unspaced dump-obj value
                ] else [
                    keep mold value
                ]
            ]
        ]
        return
    ]

    ; The HELP mechanics for ACTION! are more complex in Ren-C due to the
    ; existence of function composition tools like SPECIALIZE, CHAIN, ADAPT,
    ; HIJACK, etc.  Rather than keep multiple copies of the help strings,
    ; the relationships are maintained in META-OF information on the ACTION!
    ; and are "dug through" in order to dynamically inherit the information.
    ;
    ; Code to do this evolved rather organically, as automatically generating
    ; help for complex function derivations is a research project in its
    ; own right.  So it tends to break--test it as much as possible.

    space4: unspaced [space space space space] ;-- use instead of tab

    print "USAGE:"

    args: _ ;-- plain arguments
    refinements: _ ;-- refinements and refinement arguments

    parse words of :value [
        copy args any [word! | get-word! | lit-word! | issue!]
        copy refinements any [
            refinement! | word! | get-word! | lit-word! | issue!
        ]
        end
    ]

    ; Output exemplar calling string, e.g. LEFT + RIGHT or FOO A B C
    ; !!! Should refinement args be shown for enfixed case??
    ;
    if enfixed and [not empty? args] [
        print unspaced [
            space4 spaced [args/1 (uppercase mold topic) next args]
        ]
    ] else [
        print unspaced [
            space4 spaced [(uppercase mold topic) args refinements]
        ]
    ]

    ; Dig deeply, but try to inherit the most specific meta fields available
    ;
    fields: dig-action-meta-fields :value

    ; For reporting what *kind* of action this is, don't dig at all--just
    ; look at the meta information of the action being asked about.  Note that
    ; not all actions have META-OF (e.g. those from MAKE ACTION!, or FUNC
    ; when there was no type annotations or description information.)
    ;
    meta: try meta-of :value

    original-name: try <- ensure* word! any [
        select meta 'specializee-name
        select meta 'adaptee-name
    ] also lambda name [
        uppercase mold name
    ]

    specializee: try ensure* action! select meta 'specializee
    adaptee: try ensure* action! select meta 'adaptee
    chainees: try ensure* block! select meta 'chainees

    classification: case [
        :specializee [
            either original-name [
                spaced [{a specialization of} original-name]
            ][
                {a specialized ACTION!}
            ]
        ]

        :adaptee [
            either original-name [
                spaced [{an adaptation of} original-name]
            ][
                {an adapted ACTION!}
            ]
        ]

        :chainees [{a chained ACTION!}]
    ] else [
        {an ACTION!}
    ]

    print newline

    print "DESCRIPTION:"
    print unspaced [space4 fields/description or ["(undocumented)"]]
    print unspaced [
        space4 spaced [(uppercase mold topic) {is} classification]
    ]

    print-args: function [list /indent-words] [
        for-each param list [
            type: try ensure* block! (
                select fields/parameter-types to-word param
            )
            note: try ensure* text! (
                select fields/parameter-notes to-word param
            )

            ;-- parameter name and type line
            if type and [not refinement? param] [
                print unspaced [space4 param space "[" type "]"]
            ] else [
                print unspaced [space4 param]
            ]

            if note [
                print unspaced [space4 space4 note]
            ]
        ]
        null
    ]

    print newline
    if any [fields/return-type fields/return-note] [
        print ["RETURNS:" if fields/return-type [mold fields/return-type]]
        if fields/return-note [
            print unspaced [space4 fields/return-note]
        ]
    ] else [
        print ["RETURNS: (undocumented)"]
    ]

    if not empty? args [
        print newline
        print "ARGUMENTS:"
        print-args args
    ]

    if not empty? refinements [
        print newline
        print "REFINEMENTS:"
        print-args/indent-words refinements
    ]
]


source: function [
    "Prints the source code for an ACTION! (if available)"

    return: <void>
    'arg [word! path! action! tag!]
][
    switch type of :arg [
        tag! [
            f: copy "unknown tag"
            for-each location words of system/locale/library [
                if location: select load get location arg [
                    f: location/1
                    break
                ]
            ]
        ]

        word! path! [
            name: arg
            f: get arg else [
                print [name "is not set to a value"]
                return
            ]
        ]
    ] else [
        name: "anonymous"
        f: :arg
    ]

    case [
        match [text! url!] :f [
            print f
        ]
        not action? :f [
            print [name "is" an mold type of :f "and not an ACTION!"]
        ]
    ] then [
        return
    ]

    ;; ACTION!
    ;;
    ;; The system doesn't preserve the literal spec, so it must be rebuilt
    ;; from combining the the META-OF information.

    write-stdout unspaced [
        mold name ":" space "make action! [" space mold spec-of :f
    ]

    ; While all interfaces as far as invocation is concerned has been unified
    ; under the single ACTION! interface, the issue of getting things like
    ; some kind of displayable "source" would have to depend on the dispatcher
    ; used.  For the moment, BODY OF hands back limited information.  Review.
    ;
    switch type of (body: body of :f) [
        block! [ ;-- FUNC, FUNCTION, PROC, PROCEDURE or (DOES of a BLOCK!)
            print [mold body "]"]
        ]

        frame! [ ;-- SPECIALIZE (or DOES of an ACTION!)
            print [mold body "]"]
        ]
    ] else [
        print "...native code, no source available..."
    ]
]


what: function [
    {Prints a list of known actions}

    return: <void>
    'name [<end> word! lit-word!]
        "Optional module name"
    /args
        "Show arguments not titles"
][
    list: make block! 400
    size: 0

    ctx: all [set? 'name try select system/modules :name ] or [lib]

    for-each [word val] ctx [
        if action? :val [
            arg: either args [
                arg: words of :val
                clear find arg /local
                mold arg
            ][
                title-of :val
            ]
            append list reduce [word arg]
            size: max size length of to-text word
        ]
    ]

    vals: make text! size
    for-each [word arg] sort/skip list 2 [
        append/dup clear vals #" " size
        print [
            head of change vals word LF
            :arg
        ]
    ]
]


pending: does [
    comment "temp function"
    print "Pending implementation."
]


say-browser: does [
    comment "temp function"
    print "Opening web browser..."
]


bugs: func [return: <void>] [
    "View bug database."
][
    say-browser
    browse https://github.com/metaeducation/ren-c/issues
]


chat: func [
    "Open REBOL/ren-c developers chat forum"
    return: <void>
][
    say-browser
    browse http://chat.stackoverflow.com/rooms/291/rebol
]

; temporary solution to ensuring scripts run on a minimum build
;
require-commit: function [
    "checks current commit against required commit"

    return: <void>
    commit [text!]
][
    c: select system/script/header 'commit else [return]

    ; If we happen to have commit information that includes a date, then we
    ; can look at the date of the running Rebol and know that a build that is
    ; older than that won't work.
    ;
    if date: select c 'date and [rebol/build < date] [
        fail [
            "This script needs a build newer or equal to" date
            "so run `upgrade`"
        ]
    ]

    ; If there's a specific ID then assume that if the current build does not
    ; have that ID then there *could* be a problem.
    ;
    if id: select c 'id and [id <> commit] [
        print [
            "This script has only been tested again commit" id LF

            "If it doesn't run as expected"
            "you can try seeing if this commit is still available" LF

            "by using the `do <dl-renc>` tool and look for"
            unspaced [
                "r3-" copy/part id 7 "*"
                if find/last form rebol/version "0.3.4" [%.exe]
            ]
        ]
    ]
]
