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

    return collect [
        keep/line ensure [<opt> text!] any [
            select meta 'description
            select original-meta 'description
        ]

        return-type: try ensure [<opt> block!] any [
            select meta 'return-type
            select original-meta 'return-type
        ]
        return-note: try ensure [<opt> text!] any [
            select meta 'return-note
            select original-meta 'return-note
        ]
        if any [return-type return-note] [
            keep compose [
                return: (opt return-type) (opt return-note)
            ]
        ]

        types: try ensure [<opt> frame!] any [
            select meta 'parameter-types
            select original-meta 'parameter-types
        ]
        notes: try ensure [<opt> frame!] any [
            select meta 'parameter-notes
            select original-meta 'parameter-notes
        ]

        for-each param parameters of :action [
            keep compose [
                (param) (select types param) (select notes param)
            ]
        ]
    ]
]


description-of: function [
    {One-line summary of a value's purpose}

    return: [<opt> text!]
    v [<blank> any-value!]
][
    opt switch type of get/any 'v [
        void! [blank]
        any-array! [spaced ["array of length:" length of v]]
        image! [spaced ["size:" v/size]]
        datatype! [
            spec: ensure object! spec of v  ; "type specs" need simplifying
            copy spec/title
        ]
        action! [
            try if meta: meta-of :v [
                copy get 'meta/description
            ]
        ]
        gob! [spaced ["offset:" v/offset "size:" v/size]]
        object! [mold words of v]
        typeset! [mold make block! v]
        port! [mold reduce [v/spec/title v/spec/ref]]
        default [blank]
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

    return: [void!]
    'topic [<end> any-value!]
        "WORD! whose value to explain, or other HELP target (try HELP HELP)"
    /doc "Open web browser to related documentation."
][
    return: specialize 'return [value: ~]  ; unlabeled for no console display

    if undefined? 'topic [
        ;
        ; !!! This should lead to a web page that offers help on the nature
        ; of specific void usages, e.g. to explain what ~branched~ is and
        ; how to use @[...] branches to avoid it.
        ;
        print [mold get/any 'topic "is a literal VOID! value"]
        return
    ]

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
               append libuser reduce [key get/any 'val]
            ]
        ]
        libuser
    ]

    switch type of :topic [
        issue! [
            ; HELP #TOPIC will browse r3n for the topic

            browse join-all [https://r3n.github.io/topics/ as text! topic]
            print newline
            return
        ]

        text! [
            ; HELP "TEXT" wildcard searches things w/"TEXT" in their name

            if types: summarize-obj/match make-libuser :topic [
                print "Found these related words:"
                for-each line sort types [
                    print line
                ]
            ] else [
                print ["No information on" topic]
            ]
            return
        ]

        path! word! [
            ; PATH! and WORD! fall through for help on what they look up to

            all [
                word? topic
                null? binding of topic
            ] then [
                print [topic "is an unbound WORD!"]
                return
            ]

            switch type of value: get/any topic [
                null [
                    print [topic "is null"]
                ]
                void! [
                    print [topic "is not defined (e.g. has a VOID! value)"]
                ]
            ] then [
                return
            ]
            enfixed: did all [action? :value, enfixed? :value]
        ]
    ] else [
        ; !!! There may be interesting meanings to apply to things like
        ; `HELP 2`, which could be distinct from `HELP (1 + 1)`, or the
        ; same, and could be distinct from `HELP :(1)` or `HELP :[1]` etc.
        ; For the moment, it just tells you the type.

        if free? :topic [
            print ["is a freed" mold type of :topic]
        ] else [
            print [mold topic "is" an mold type of :topic]
        ]
        return
    ]

    ; Open the web page for it?
    all [doc, match [action! datatype!] :value] then [
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
            remove back tail of item  ; it's a DATATYPE!, so remove the !
            browse join-all [
                http://www.rebol.com/r3/docs/datatypes/
                item
                tmp: %.html
            ]
        ]
    ]

    if datatype? :value [
        if instances: summarize-obj/match make-libuser :value [
            print ["Found these" (uppercase form topic) "words:"]
            for-each line instances [
                print line
            ]
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
                    for-each line summarize-obj value [
                        keep line
                        keep newline
                    ]
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

    print "USAGE:"

    args: _  ; plain arguments
    refinements: _  ; refinements and refinement arguments

    parse parameters of :value [
        copy args any [word! | get-word! | lit-word! | issue!]
        copy refinements any path!  ; !!! Refinements may become intermixed!
        end
    ]

    ; Output exemplar calling string, e.g. LEFT + RIGHT or FOO A B C
    ; !!! Should refinement args be shown for enfixed case??
    ;
    all [enfixed, not empty? args] then [
        print [_ _ _ _ args/1 (uppercase mold topic) next args]
    ] else [
        print [_ _ _ _ (uppercase mold topic) args refinements]
    ]

    meta: try meta-of :value

    print newline

    print "DESCRIPTION:"
    print [_ _ _ _ (:meta/description or '{(undocumented)})]
    print [_ _ _ _ (uppercase mold topic) {is an ACTION!}]

    print-args: function [list /indent-words] [
        for-each param list [
            type: ensure [<opt> block!] (
                select try :meta/parameter-types to-word dequote param
            )
            note: ensure [<opt> text!] (
                select try :meta/parameter-notes to-word dequote param
            )

            print [_ _ _ _ param (if type [unspaced ["[" type "]"]])]
            if note [
                print [_ _ _ _ _ _ _ _ note]
            ]
        ]
    ]

    print newline
    print [
        "RETURNS:"
        either :meta/return-type [mold :meta/return-type] ["(undocumented)"]
    ]
    if :meta/return-note [
        print [_ _ _ _ meta/return-note]
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

    return  ; use overridden return vs. fallout so void is unlabeled
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
                    f: form location/1
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

    ; ACTION!
    ; The system doesn't preserve the literal spec, so it must be rebuilt
    ; from combining the the META-OF information.

    write-stdout unspaced [
        mold name ":" _ "make action! [" _ mold spec-of :f
    ]

    ; While all interfaces as far as invocation is concerned has been unified
    ; under the single ACTION! interface, the issue of getting things like
    ; some kind of displayable "source" would have to depend on the dispatcher
    ; used.  For the moment, BODY OF hands back limited information.  Review.
    ;
    switch type of (body: body of :f) [
        block! [  ; FUNC, FUNCTION, PROC, PROCEDURE or (DOES of a BLOCK!)
            print [mold body "]"]
        ]

        frame! [  ; SPECIALIZE (or DOES of an ACTION!)
            print [mold body "]"]
        ]
    ] else [
        print "...native code, no source available..."
    ]
]


what: function [
    {Prints a list of known actions}

    return: [void! block!]
    'name [<end> word! lit-word!]
        "Optional module name"
    /args
        "Show arguments not titles"
    /as-block
        "Return data as block"
][
    list: make block! 400
    size: 0

    ; copy to get around error: "temporary hold for iteration"
    ctx: copy all [set? 'name try select system/modules :name ] else [lib]

    for-each [word val] ctx [
        ; word val
        if (action? :val) [
            arg: either args [
                mold parameters of :val
            ][
                description-of :val else ["(undocumented)"]
            ]
            append list reduce [word arg]
            size: max size length of to-text word
        ]
    ]
    list: sort/skip list 2

    name: make text! size
    either not as-block [
        for-each [word arg] list [
            append/dup clear name #" " size
            change name word
            print [
                name
                :arg
            ]
        ]
    ][
         list
    ]
]


pending: does [
    comment "temp function"
    print "Pending implementation."
]


bugs: func [return: <void>] [
    "View bug database."
][
    browse https://github.com/metaeducation/ren-c/issues
]


chat: func [
    "Open REBOL/ren-c developers chat forum"
    return: <void>
][
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
                if find-last form system/version "0.3.4" [%.exe]
            ]
        ]
    ]
]
