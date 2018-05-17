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


; !!! R3-Alpha labeled this "MOVE THIS INTERNAL FUNC" but it is actually used
; to search for patterns in HELP when you type in something that isn't bound,
; so it uses that as a string pattern.  Review how to better factor that
; (as part of a general help review)
;
dump-obj: function [
    "Returns a block of information about an object or port."
    obj [object! port!]
    /match "Include only those that match a string or datatype" pat
][
    clip-str: func [str] [
        ; Keep string to one line.
        trim/lines str
        if (length of str) > 48 [str: append copy/part str 45 "..."]
        str
    ]

    form-val: func [val [any-value!]] [
        ; Form a limited string from the value provided.
        if any-block? :val [return spaced ["length:" length of val]]
        if image? :val [return spaced ["size:" val/size]]
        if datatype? :val [return form val]
        if action? :val [
            return clip-str any [title-of :val mold spec-of :val]
        ]
        if object? :val [val: words of val]
        if typeset? :val [val: to-block val]
        if port? :val [val: reduce [val/spec/title val/spec/ref]]
        if gob? :val [return spaced ["offset:" val/offset "size:" val/size]]
        clip-str mold :val
    ]

    form-pad: func [val size] [
        ; Form a value with fixed size (space padding follows).
        val: form val
        insert/dup tail of val #" " size - length of val
        val
    ]

    ; Search for matching strings:
    collect [
        wild: all [set? 'pat | text? pat | find pat "*"]

        for-each [word val] obj [
            type: type of :val

            str: if lib/match [action! object!] :type [
                spaced [word _ mold spec-of :val _ words of :val]
            ] else [
                form word
            ]

            if any [
                not match
                all [
                    set? 'val
                    either text? :pat [
                        either wild [
                            tail? any [find/any/match str pat pat]
                        ][
                            find str pat
                        ]
                    ][
                        all [
                            datatype? get :pat
                            type = get :pat
                        ]
                    ]
                ]
            ][
                str: form-pad word 15
                append str #" "
                append str form-pad type 10 - ((length of str) - 15)
                keep spaced [
                    "  " str
                    if type [form-val :val]
                    newline
                ]
            ]
        ]
    ]
]


dump: func [
    {Show the name of a value (or block of expressions) with the value itself}

    return: []
        {Doesn't return anything, not even void (so like a COMMENT)}
    :value [any-value! <...>]
    <local>
        dump-one dump-val clip-string item set-word result
][
    if bar? first value [
        take value
        leave
    ] ;-- treat this DUMP as disabled, `dump | x`

    dump-val: function [val [<opt> any-value!]] [
        case [
            not set? 'val [
                "\\ null \\"
            ]
            object? :val [
                unspaced ["make object! [" | dump-obj val | "]"]
            ]
        ] else [
           mold/limit :val system/options/dump-size
        ]
    ]

    dump-one: proc [item][
        switch type of item [
            text! [ ;-- allow customized labels
                print ["---" mold/limit item system/options/dump-size "---"]
            ]

            word! [
                print [to set-word! item "=>" dump-val get item]
            ]

            path! [
                print [to set-path! item "=>" dump-val get item]
            ]

            group! [
                trap/with [
                    print [mold item "=>" mold eval item]
                ] func [error] [
                    print [mold item "=!!!=>" mold error]
                ]
            ]
        ] else [
            fail [
                "Item not TEXT!, WORD!, PATH!, or GROUP! in DUMP." item
            ]
        ]
    ]

    case [
        tail? value [
            fail "No argument provided to DUMP"
        ]

        ; The reason this function is a quoting variadic is so that you can
        ; write `dump x: 1 + 2` and get `x: => 3`.  This is just a convenience
        ; to save typing over `blahblah: 1 + 2 dump blahblah`.
        ;
        ; !!! Should also support `dump [x: 1 + 2 y: 3 + 4]` as a syntax...
        ;
        set-word? first value [
            set-word: first value
            result: do/next value (quote pos:)
            ;-- Note: don't need to TAKE
            print [set-word "=>" result]
        ]

        block? first value [
            for-each item take value [dump-one item]
        ]
    ] else [
        dump-one take value
    ]
]


spec-of: function [
    {Generate a block which could be used as a "spec block" from an action.}

    action [action!]
][
    meta: try match object! meta-of :action

    specializee: try match action! try select meta 'specializee
    adaptee: try match action! try select meta 'adaptee
    original-meta: match object! any [
        :specializee and [meta-of :specializee]
        :adaptee and [meta-of :adaptee]
    ]

    return collect [
        keep/line match text! any [
            try select meta 'description
            try select original-meta 'description
        ]

        return-type: try match block! any [
            try select meta 'return-type
            try select original-meta 'return-type
        ]
        return-note: try match text! any [
            try select meta 'return-note
            try select original-meta 'return-note
        ]
        if return-type or (return-note) [
            keep compose/only [
                return: (opt return-type) (opt return-note)
            ]
        ]

        types: match frame! any [
            to-value select meta 'parameter-types
            to-value select original-meta 'parameter-types
        ]
        notes: match frame! any [
            to-value select meta 'parameter-notes
            to-value select original-meta 'parameter-notes
        ]

        for-each param words of :action [
            keep compose/only [
                (param) (select types param) (select notes param)
            ]
        ]
    ]
]


title-of: function [
    {Extracts a summary of a value's purpose from its "meta" information.}

    value [any-value!]
][
    try switch type of :value [
        action! [
            all [
                object? meta: meta-of :value
                text? description: select meta 'description
                copy description
            ]
        ]

        datatype! [
            spec: spec-of value
            assert [text? spec] ;-- !!! Consider simplifying "type specs"
            spec/title
        ]
    ]
]

browse: procedure [
    "stub function for browse* in extensions/process/ext-process-init.reb"
    location [url! file! blank!]
][
    print "Browse needs redefining"
]

help: procedure [
    "Prints information about words and values (if no args, general help)."
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
        leave
    ]

    ; HELP quotes, but someone might want to use an expression, e.g.
    ; `help (...)`.  However, enfix functions which hard quote the left would
    ; win over a soft-quoting non-enfix function that quotes to the right.
    ; (It is generally discouraged to make hard-quoting left enfix functions,
    ; but they exist...e.g. DEFAULT.)  To make sure HELP DEFAULT works, HELP
    ; must hard quote and simulate its own soft quote semantics.
    ;
    if match [group! get-word! get-path!] :topic [
        topic: reduce target
    ]

    r3n: https://r3n.github.io/

    ;; help #topic (browse r3n for topic)
    if issue? :topic [
        say-browser
        browse join-all [r3n "topics/" next to-text :topic]
        leave
    ]

    if word? :topic and (blank? context of topic) [
        print [topic "is an unbound WORD!"]
        leave
    ]

    if word? :topic and (unset? topic) [
        print [topic "is a WORD! bound to a context, but has no value."]
        leave
    ]

    ; Open the web page for it?
    if doc and (word? topic) and (match [action! datatype!] get :topic) [
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

    if all [word? :topic | set? :topic | datatype? get :topic] [
        types: dump-obj/match make-libuser :topic
        if not empty? types [
            print ["Found these" (uppercase form topic) "words:" newline types]
        ] else [
            print [topic {is a datatype}]
        ]
        leave
    ]

    ; If arg is a text string, search the system:
    if text? :topic [
        types: dump-obj/match make-libuser :topic
        sort types
        if not empty? types [
            print ["Found these related words:" newline types]
            leave
        ]
        print ["No information on" topic]
        leave
    ]

    ; Print type name with proper singular article:
    type-name: func [value [any-value!]] [
        value: mold type of :value
        clear back tail of value
        an value
    ]

    ; Print literal values:
    if not any [word? :topic | path? :topic][
        print [mold :topic "is" type-name :topic]
        leave
    ]

    ; Functions are not infix in Ren-C, only bindings of words to infix, so
    ; we have to read the infixness off of the word before GETting it.

    ; Get value (may be a function, so handle with ":")
    either path? :topic [
        print ["!!! NOTE: Enfix testing not currently supported for paths"]
        enfixed: false
        if any [
            error? value: trap [get :topic] ;trap reduce [to-get-path topic]
            unset? 'value
        ][
            print ["No information on" topic "(path has no value)"]
            leave
        ]
    ][
        enfixed: enfixed? :topic
        value: get :topic
    ]

    if not action? :value [
        print spaced collect [
            keep [
                (uppercase mold topic) "is" (type-name :value) "of value:"
            ]
            if match [object! port!] value [
                keep newline
                keep unspaced dump-obj value
            ] else [
                keep mold value
            ]
        ]
        leave
    ]

    ; Must be a function...
    ; If it has refinements, strip them:
    ;if path? :topic [topic: first :topic]

    space4: unspaced [space space space space] ;-- use instead of tab

    ;-- Print info about function:
    print "USAGE:"

    args: _ ;-- plain arguments
    refinements: _ ;-- refinements and refinement arguments

    parse words of :value [
        copy args any [word! | get-word! | lit-word! | issue!]
        copy refinements any [
            refinement! | word! | get-word! | lit-word! | issue!
        ]
    ]

    ; Output exemplar calling string, e.g. LEFT + RIGHT or FOO A B C
    ; !!! Should refinement args be shown for enfixed case??
    ;
    either enfixed [
        print [space4 args/1 (uppercase mold topic) next args]
    ][
        print [space4 (uppercase mold topic) args refinements]
    ]

    ; Dig deeply, but try to inherit the most specific meta fields available
    ;
    fields: dig-action-meta-fields :value

    description: :fields/description
    return-type: :fields/return-type
    return-note: :fields/return-note
    types: :fields/parameter-types
    notes: :fields/parameter-notes

    ; For reporting what kind of function this is, don't dig at all--just
    ; look at the meta information of the function being asked about
    ;
    meta: meta-of :value
    all [
        original-name: try match word! any [
            try select meta 'specializee-name
            try select meta 'adaptee-name
        ]
        original-name: uppercase mold original-name
    ]

    specializee: match action! try select meta 'specializee
    adaptee: match action! try select meta 'adaptee
    chainees: match block! try select meta 'chainees

    classification: {a function} unless case [
        set? 'specializee [
            either original-name [
                spaced [{a specialization of} original-name]
            ][
                {a specialized function}
            ]
        ]

        set? 'adaptee [
            either original-name [
                spaced [{an adaptation of} original-name]
            ][
                {an adapted function}
            ]
        ]

        set? 'chainees [
            {a chained function}
        ]
    ]

    print-newline

    print [
        "DESCRIPTION:" LF
        space4 (any [description | "(undocumented)"]) LF
        space4 (uppercase mold topic) {is} classification
    ]

    print-args: procedure [list /indent-words] [
        for-each param list [
            note: match text! try select notes to-word param
            type: match [block! any-word!] try select types to-word param

            ;-- parameter name and type line
            if set? 'type and (not refinement? param) [
                print unspaced [space4 param space "[" type "]"]
            ] else [
                print unspaced [space4 param]
            ]

            if set? 'note [
                print unspaced [space4 space4 note]
            ]
        ]
    ]

    either blank? :return-type [
        ; If it's a PROCEDURE, saying "RETURNS: null" would waste space
    ][
        ; For any return besides "always null", try to say something about
        ; the return value...even if just to say it's undocumented.
        ;
        print-newline
        print ["RETURNS:" (if set? 'return-type [mold return-type])]
        either set? 'return-note [
            print unspaced [space4 return-note]
        ][
            if unset? 'return-type [
                print unspaced [space4 "(undocumented)"]
            ]
        ]
    ]

    if not empty? args [
        print-newline
        print "ARGUMENTS:"
        print-args args
    ]

    if not empty? refinements [
        print-newline
        print "REFINEMENTS:"
        print-args/indent-words refinements
    ]
]


source: procedure [
    "Prints the source code for an ACTION! (if available)"
    'arg [word! path! action! tag!]
][
    case [
        tag? :arg [
            f: copy "unknown tag"
            for-each location words of system/locale/library [
                if location: select load get location arg [
                    f: location/1
                    break
                ]
            ]
        ]

        match [word! path!] :arg [
            name: arg
            f: get :arg
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
    ] also [
        leave
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


what: procedure [
    {Prints a list of known actions}
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


bugs: proc [
    "View bug database."
][
    say-browser
    browse https://github.com/metaeducation/ren-c/issues
]


chat: proc [
    "Open REBOL/ren-c developers chat forum"
][
    say-browser
    browse http://chat.stackoverflow.com/rooms/291/rebol
]

; temporary solution to ensuring scripts run on a minimum build
;
require-commit: procedure [
    "checks current commit against required commit"
    commit [text!]
][
    if not c: select system/script/header 'commit [leave]

    ; If we happen to have commit information that includes a date, then we
    ; can look at the date of the running Rebol and know that a build that is
    ; older than that won't work.
    ;
    if did date: select c 'date and (rebol/build < date) [
        fail [
            "This script needs a build newer or equal to" date
            "so run `upgrade`"
        ]
    ]

    ; If there's a specific ID then assume that if the current build does not
    ; have that ID then there *could* be a problem.
    ;
    if did id: select c 'id and (id <> commit) [
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
