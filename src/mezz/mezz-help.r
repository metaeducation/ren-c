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
    action [<unrun> action!]
][
    meta: (match object! meta-of action) else [return [~bad-spec~]]

    return collect [
        keep/line maybe ensure [<opt> text!] select meta 'description

        types: ensure [<opt> frame! object!] select meta 'parameter-types
        notes: ensure [<opt> frame! object!] select meta 'parameter-notes

        return-type: ensure [<opt> block!] select maybe types 'return
        return-note: ensure [<opt> text!] select maybe notes 'return

        if return-type or return-note [
            keep spread compose [
                return: (maybe return-type) (maybe return-note)
            ]
        ]

        for-each param parameters of action [
            keep spread compose [
                (param)
                    (maybe select maybe types param)
                    (maybe select maybe notes param)
            ]
        ]
    ]
]


description-of: function [
    {One-line summary of a value's purpose}

    return: [<opt> text!]
    v [<maybe> any-value!]
][
    return (switch/type :v [
        any-array! [spaced ["array of length:" length of v]]
        type-word! [
            mold v
        ]
        action! [
            if let m: meta-of :v [
                copy maybe get 'm.description
            ] else [null]
        ]
        object! [mold words of v]
        parameter! [mold v]
        port! [mold reduce [v.spec.title v.spec.ref]]
    ] else [null])
]

browse: function [
    "stub function for browse* in extensions/process/ext-process-init.reb"

    return: <none>
    location [<maybe> url! file!]
][
    print "Browse needs redefining"
]

help: function [
    "Prints information about words and values (if no args, general help)."

    return: [<nihil>]
    'topic [<end> any-value!]
        "WORD! whose value to explain, or other HELP target (try HELP HELP)"
    /doc "Open web browser to related documentation."
][
    if null? :topic [
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
                help system.contexts.user   - your user context
                help system - the system object
                help system/options - special settings

            To see all words of a specific datatype:

                help object!
                help action!
                help type-word!

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
        return nihil
    ]

    ; HELP quotes, but someone might want to use an expression, e.g.
    ; `help (...)`.  However, enfix functions which hard quote the left would
    ; win over a soft-quoting non-enfix function that quotes to the right.
    ; (It is generally discouraged to make hard-quoting left enfix functions,
    ; but they exist...e.g. DEFAULT.)  To make sure HELP DEFAULT works, HELP
    ; must hard quote and simulate its own soft quote semantics.
    ;
    if match [group! get-word! get-path! get-tuple!] :topic [
        topic: reeval topic else [
            print "NULL is a non-valued state that cannot be put in arrays"
            return nihil
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
        libuser: copy system.contexts.lib
        for-each [key val] system.contexts.user [
            if set? 'val [
               append libuser spread reduce [key ^val]
            ]
        ]
        libuser
    ]

    switch/type :topic [
        issue! [
            ; HELP #TOPIC will browse r3n for the topic

            browse join https://r3n.github.io/topics/ as text! topic
            print newline
            return nihil
        ]

        text! [
            ; HELP "TEXT" wildcard searches things w/"TEXT" in their name

            if types: summarize-obj/pattern make-libuser :topic [
                print "Found these related words:"
                for-each line sort types [
                    print line
                ]
            ] else [
                print ["No information on" topic]
            ]
            return nihil
        ]

        path! word! [
            ; PATH! and WORD! fall through for help on what they look up to

            all [
                word? topic
                null? binding of topic
            ] then [
                print [topic "is an unbound WORD!"]
                return nihil
            ]

            if '~attached~ = binding of topic [
                print [topic "is ~attached~ to a context, but has no variable"]
                return nihil
            ]

            switch/type value: get/any topic [
                void! [
                    print [topic "is void"]
                ]
                null! [
                    print [topic "is null"]
                ]
                none! [
                    print [topic "is not defined (e.g. has a NONE! value)"]
                ]
            ] then [
                return nihil
            ]
            enfixed: (action? :value) and (enfixed? :value)
        ]
    ] else [
        ; !!! There may be interesting meanings to apply to things like
        ; `HELP 2`, which could be distinct from `HELP (1 + 1)`, or the
        ; same, and could be distinct from `HELP :(1)` or `HELP :[1]` etc.
        ; For the moment, it just tells you the type.

        print collect [
            if not free? :topic [keep mold topic]
            keep "is"
            if free? :topic [keep "a *freed*"]
            keep any [mold kind of :topic, "VOID"]
        ]
        return nihil
    ]

    ; Open the web page for it?
    all [doc, match [action! type-word!] :value] then [
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

            browse to url! compose [
                https://github.com/gchiu/reboldocs/blob/master/ (item) .MD
            ]
        ] else [
            remove item  ; it's a &type-word, so remove the &
            browse to url! compose [
                http://www.rebol.com/r3/docs/datatypes/ (item) .html
            ]
        ]
    ]

    if type-word? :value [
        if instances: summarize-obj/pattern make-libuser :value [
            print ["Found these" (uppercase form topic) "words:"]
            for-each line instances [
                print line
            ]
        ] else [
            print [topic {is a datatype}]
        ]
        return nihil
    ]

    match [action! activation!] :value else [
        print collect [
            keep uppercase mold topic
            keep "is"
            keep an any [mold kind of :value, "VOID"]
            if free? :value [
                keep "that has been FREEd"
            ] else [
                keep "of value:"
                if match [object! port!] value [
                    keep newline
                    for-each line summarize-obj value [
                        keep line
                        keep newline
                    ]
                ] else [
                    keep mold value
                ]
            ]
        ]
        return nihil
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

    print "USAGE:"

    args: ~  ; required parameters (and "skippable" parameters, at the moment)
    refinements: ~  ; optional parameters (PARAMETERS OF puts at tail)

    parse parameters of :value [
        args: try across some [word! | meta-word! | get-word! | lit-word!]
        refinements: try across some path!  ; as mentioned, these are at tail
    ] except [
        fail ["Unknown results in PARAMETERS OF:" mold parameters of :value]
    ]

    ; Output exemplar calling string, e.g. LEFT + RIGHT or FOO A B C
    ; !!! Should refinement args be shown for enfixed case??
    ;
    all [enfixed, args] then [
        print [_ _ _ _ args.1 (uppercase mold topic) form next args]
    ] else [
        print [
            _ _ _ _ (uppercase mold topic)
                maybe form maybe args, maybe form maybe refinements
        ]
    ]

    meta: meta-of :value else '[]  ; so SELECT just returns NULL

    print newline

    print "DESCRIPTION:"
    print [_ _ _ _ (select meta 'description) else ["(undocumented)"]]
    print [_ _ _ _ (uppercase mold topic) {is an ACTION!}]

    print-args: [list /indent-words] -> [
        for-each param list [
            types: ensure [<opt> block!] (
                select (maybe select meta 'parameter-types) to-word noquote param
            )
            note: ensure [<opt> text!] (try
                select (maybe select meta 'parameter-notes) to-word noquote param
            )

            print [_ _ _ _ param (if types [mold types])]
            if note [
                print [_ _ _ _ _ _ _ _ note]
            ]
        ]
    ]

    ; !!! This is imperfect because it will think a parameter named RETURN
    ; that isn't intended for use as a definitional return is a return type.
    ; The concepts are still being fleshed out.
    ;
    return-type: select maybe select meta 'parameter-types 'return
    return-note: select maybe select meta 'parameter-notes 'return

    print newline
    print [
        "RETURNS:" mold any [return-type, "(undocumented)"]
    ]
    if return-note [
        print [_ _ _ _ return-note]
    ]

    if args [
        print newline
        print "ARGUMENTS:"
        print-args args
    ]

    if refinements [
        print newline
        print "REFINEMENTS:"
        print-args/indent-words refinements
    ]

    return nihil
]


source: function [
    "Prints the source code for an ACTION! (if available)"

    return: [<nihil>]
    'arg [word! path! action! tag!]
][
    switch/type arg [
        tag! [
            f: copy "unknown tag"
            for-each location words of system.locale.library [
                if location: select load get location arg [
                    f: form location.1
                    break
                ]
            ]
        ]

        word! path! [
            name: arg
            f: get arg else [
                print [name "is not set to a value"]
                return nihil
            ]
        ]
    ] else [
        name: "anonymous"
        f: arg
    ]

    if activation? :f [
        f: unrun :f
    ]

    case [
        match [text! url!] f [
            print f
        ]
        not action? f [
            print [
                name "is" an any [mold kind of :f, "NONE"]
                "and not an ACTION!"
            ]
        ]
    ] then [
        return nihil
    ]

    ; ACTION!
    ; The system doesn't preserve the literal spec, so it must be rebuilt
    ; from combining the the META-OF information.

    write-stdout unspaced [
        mold name ":" _ "make action! [" _ mold spec-of f
    ]

    ; While all interfaces as far as invocation is concerned has been unified
    ; under the single ACTION! interface, the issue of getting things like
    ; some kind of displayable "source" would have to depend on the dispatcher
    ; used.  For the moment, BODY OF hands back limited information.  Review.
    ;
    switch/type (body: body of f) [
        block! [  ; FUNC, FUNCTION, PROC, PROCEDURE or (DOES of a BLOCK!)
            print [mold body "]"]
        ]

        frame! [  ; SPECIALIZE (or DOES of an ACTION!)
            print [mold body "]"]
        ]
    ] else [
        print "...native code, no source available..."
    ]

    return nihil
]


what: function [
    {Prints a list of known actions}

    return: [none! block!]
    'name [<end> word! lit-word!]
        "Optional module name"
    /args "Show arguments not titles"
    /as-block "Return data as block"
][
    list: make block! 400
    size: 0

    ; copy to get around error: "temporary hold for iteration"
    ctx: (copy try select system.modules try :name) else [lib]

    for-each [word val] ctx [
        ; word val
        if (action? :val) [
            arg: either args [
                mold parameters of :val
            ][
                description-of :val else ["(undocumented)"]
            ]
            append list spread reduce [word arg]
            size: max size length of to-text word
        ]
    ]
    list: sort/skip list 2

    name: make text! size
    if as-block [
        return list
    ]

    for-each [word arg] list [
        append/dup clear name #" " size
        change name word
        print [
            name
            :arg
        ]
    ]
    return none
]


bugs: func [return: <none>] [
    "View bug database."
][
    browse https://github.com/metaeducation/ren-c/issues
]


; temporary solution to ensuring scripts run on a minimum build
;
require-commit: function [
    "checks current commit against required commit"

    return: <none>
    commit [text!]
][
    c: select system.script.header 'commit else [return none]

    ; If we happen to have commit information that includes a date, then we
    ; can look at the date of the running Rebol and know that a build that is
    ; older than that won't work.
    ;
    all [
        date: select c 'date
        system/build < date

        fail [
            "This script needs a build newer or equal to" date
            "so run `upgrade`"
        ]
    ]

    ; If there's a specific ID then assume that if the current build does not
    ; have that ID then there *could* be a problem.
    ;
    all [id: select c 'id, id <> commit] then [
        print [
            "This script has only been tested again commit" id LF

            "If it doesn't run as expected"
            "you can try seeing if this commit is still available" LF

            "by using the `do <dl-renc>` tool and look for"
            unspaced [
                "r3-" copy/part id 7 "*"
                if find-last form system.version "0.3.4" [%.exe]
            ]
        ]
    ]
]
