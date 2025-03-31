REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Help"
    Rights: --{
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
]


spec-of: func [
    "Generate a block which could be used as a 'spec block' from an action"

    return: [block!]
    action [<unrun> frame!]
][
    let adjunct: match object! maybe adjunct-of action

    return collect [
        if adjunct [
            keep:line ? ensure [~null~ text!] select adjunct 'description
        ]

        let r-param: return of action

        if r-param and (r-param.spec or r-param.text) [
            keep spread compose [
                return: (? r-param.spec)
                    (? r-param.text)
            ]
        ]

        for-each [key param] (parameters of action) [
            keep spread compose [
                (decorate-parameter param key) (? param.spec)
                    (? param.text)
            ]
        ]
    ]
]

decorated-words-of: func [
    "Get the decorated parameters as a block (useful for testing)"
    return: [block!]
    frame [<unrun> frame!]
][
    return map-each [key param] (parameters of frame) [
        decorate-parameter param key
    ]
]

description-of: func [
    "One-line summary of a value's purpose"

    return: [~null~ text!]
    v [<maybe> any-value?]
][
    if action? :v [
        v: unrun v
    ]
    return (switch:type :v [
        any-list?/ [spaced ["list of length:" length of v]]
        datatype! [
            mold v
        ]
        frame! [
            if let adjunct: adjunct-of :v [
                copy maybe adjunct.description
            ] else [null]
        ]
        object! [mold words of v]
        parameter! [mold v]
        port! [mold reduce [v.spec.title v.spec.ref]]
    ] else [null])
]


print-general-help: func [
    return: [~]
][
    print trim:auto copy --{
        You are in a Rebol terminal.  QUIT or Ctrl-C should let you exit.

        Here are some basic commands:

            about - see general product info
            bugs - open GitHub issues website
            changes - show changelog
            chat - open GitHub developer forum
            help - help on functions and values
            install - install (when applicable)
            license - show user license
            topics - open help topics website
            upgrade - check for newer versions
            usage - program cmd line options

        Generally useful commands:

            docs - open browser to web documentation
            dump - display a variable and its value
            probe - print a value (molded)
            source - show source code of function
            trace - trace evaluation steps
            what - show a list of known functions
            why - explain more about last error (via web)

        Try HELP HELP for assistance on use of the help facility.
    }--
    return ~
]


help-action: func [
    return: [~]
    frame [<unrun> frame!]
    :name [word! tuple! path!]
][
    name: default [label of frame]
    if name [
        name: uppercase form name
    ] else [
        name: "(anonymous)"
    ]

    print "USAGE:"

    let args: copy []  ; foo baz bar
    let deco-args: copy []  ; foo 'baz @(bar)
    let refinements: copy []  ; mumble frotz
    let deco-refinements: copy []  ; :mumble :/@(frotz)

    for-each [key param] (parameters of frame) [
        if param.optional [
            append refinements key
            append deco-refinements decorate-parameter param key
        ] else [
            append args key
            append deco-args decorate-parameter param key
        ]
    ]

    ; Output exemplar calling string, e.g. LEFT + RIGHT or FOO A B C
    ;
    all [infix? frame, not empty? deco-args] then [
        print [_ _ _ _ @deco-args.1 name @(spread next deco-args)]
    ] else [
        print [_ _ _ _ name @(spread deco-args) @(spread deco-refinements)]
    ]

    let adjunct: adjunct-of frame
    print newline

    print "DESCRIPTION:"
    print [_ _ _ _ (any [select maybe adjunct 'description, "(undocumented)"])]

    let print-args: [list :indent-words] -> [
        for-each 'key list [
            let param: select frame key
            print [_ _ _ _ @(decorate-parameter param key) @(maybe param.spec)]
            if param.text [
                print [_ _ _ _ _ _ _ _ param.text]
            ]
        ]
    ]

    ; !!! Note that an action can have an ordinary parameter named RETURN.
    ; If it does then it will be listed in the arguments.  This hasn't been
    ; completely worked out, but the idea here is that if it doesn't have
    ; an *actual* by-contract return value documentation, then there will
    ; be no RETURNS: in the help.
    ;
    let return-param: return of frame

    if return-param [
        print newline
        print [
            "RETURNS:" (any [mold ? return-param.spec, "(undocumented)"])
        ]
        if return-param.text [
            print [_ _ _ _ return-param.text]
        ]
    ]

    if not empty? args [
        print newline
        print "ARGUMENTS:"
        print-args args
    ]

    if not empty? refinements [
        print newline
        print "REFINEMENTS:"
        print-args:indent-words refinements
    ]

    return ~
]


help-value: func [
    "Give non-dialected help for an atom with any datatype"

    return: [~]
    ^atom' [any-atom?]
    :name [word! tuple! path!]
][
    if name [
        name: uppercase form name
    ]

    if quasiform? atom' [
        let heart: heart of atom'
        let antitype: switch heart [  ; !!! should come from %types.r
            blank! ["nothing"]
            tag! ["tripwire"]
            word! ["keyword"]
            group! ["splice"]
            frame! ["action"]
            block! ["pack"]
            comma! ["barrier"]
            error! ["raised"]
            object! ["lazy"]

            fail "Invalid Antiform Heart Found - Please Report"
        ]
        print [
            (maybe name) "is" (an antitype) ["(antiform of" _ (mold heart) ")"]
        ]
        if free? atom' [
            print "!!! contents no longer available, as it has been FREE'd !!!"
            return ~
        ]
        if action? unmeta atom' [
            help-action unmeta atom'
            return ~
        ]
        let [molded truncated]: mold:limit atom' 2000  ; quasiform
        print unspaced [molded (if truncated ["..."]) _ _ "; anti"]
        return ~
    ]

    let value: unmeta atom'
    atom': ~

    print [maybe name "is an element of type" to word! type of value]
    if free? value [
        print "!!! contents no longer available, as it has been FREE'd !!!"
        return ~
    ]
    if match [object! port!] value [
        for-each 'line summarize-obj value [
            print line
        ]
        return ~
    ]
    let [molded truncated]: mold:limit value 2000
    print unspaced [molded (if truncated ["..."])]
    return ~
]


help: func [
    --{HELP is a dialected function.  If you want non-dialected help on any
    particular value, then pass that value in a GROUP! to get some very
    literal information back:

        help ('insert)   ; will tell you that INSERT is a WORD!
        help (10 + 20)   ; will tell you 30 is an INTEGER!

        help-value 10 + 20  ; alternative way to get this information

    Other usages have meaning dependent on the datatype.

    [WORD! TUPLE! PATH!] - Give help on what the value looks up to:

        help insert  ; describe argument types and return value of INSERT

    [TEXT!] - Search help strings in system for the given text as substring:

        help "insert"  ; will find INSERT, INSERT-ITEMS, DO-INSERT, etc.

    [ISSUE!] - Browse online topics with the given tag:

        help #compiling

    [TYPE-WORD!] - List all data instances of the type:

        help integer!  ; shows all the integer variables in the system

    [BLOCK!, etc.] - TBD.  Can you think of a good meaning for other types?

    To browse online documentation:

        help/web insert

    To view words and values of a context or object:

        help lib    - the runtime library
        help system.contexts.user   - your user context
        help system - the system object
        help system.options - special settings

    To see all words of a specific datatype:

        help object!
        help datatype!}--

    return: [~]
    @topic "WORD! to explain, or other HELP target (if no args, general help)"
        [<end> element?]
    :web "Open web browser to related documentation."
][
    if unset? $topic [  ; just `>> help` or `eval [help]` or similar
        print-general-help
        return ~
    ]

    if web [
        if not word? topic [
            fail "HELP:WEB only works on WORD! at this time"
        ]

        let string: to text! topic
        if "!" = last string [
            browse join url! [
                http://www.rebol.com/r3/docs/datatypes/ (string) ".html"
            ]
            return ~
        ]

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
            replace item a b
        ]

        browse join url! [
            https://github.com/gchiu/reboldocs/blob/master/ (item) ".MD"
        ]
        return ~
    ]

    let make-libuser: does [  ; hacky unified context for searching
        let libuser: copy system.contexts.lib
        for-each [key val] system.contexts.user [
            if not vacant? $val [
               set (extend libuser key) get $val
            ]
        ]
        libuser
    ]

    switch:type topic [
        group! [
            help-value eval:undecayed topic
        ]

        word! tuple! path! [
            let value: get:any topic except e -> [
                print form e  ; not bound, etc.
                return ~
            ]
            if action? get $value [
                help-action:name value/ topic  ; bypass print name
            ] else [
                help-value:name value topic  ; prints name (should it?)
            ]
        ]

        issue! [  ; look up hashtag on web
            browse join https://r3n.github.io/topics/ as text! topic
            print newline
        ]

        text! [  ; substring search for help
            if let types: summarize-obj:pattern make-libuser topic [
                print "Found these related words:"
                for-each 'line sort types [
                    print line
                ]
            ] else [
                print ["No information on" topic]
            ]
        ]

        datatype! [
            if instances: summarize-obj:pattern make-libuser value [
                print ["Found these" (uppercase form topic) "words:"]
                for-each 'line instances [
                    print line
                ]
            ] else [
                print ["no instances of" mold topic]
            ]
        ]
    ] else [
        print "No dialected meaning for" type of topic "in HELP (yet!)"
        print "For info on what it evaluates to, try" ["HELP (" topic ")"]
    ]

    return ~
]


source: func [
    "Prints the source code for an ACTION! (if available)"

    return: [~]
    @arg [<unrun> word! path! frame! tag!]
][
    let name
    let f
    switch:type arg [
        tag! [
            f: copy "unknown tag"
            for-each 'location words of system.locale.library [
                if location: select load get location arg [
                    f: form location.1
                    break
                ]
            ]
        ]

        word! path! [
            name: arg
            if action? (f: get arg else [
                print [name "is not set to a value"]
                return ~
            ]) [
                f: unrun f/
            ]
        ]
    ] else [
        name: "anonymous"
        f: arg
    ]

    case [
        match [text! url!] f [
            print f
        ]
        not frame? (unrun f) [
            print [
                name "is" an any [mold maybe type of f, "NULL"]
                "and not a FRAME!"
            ]
        ]
    ] then [
        return ~
    ]

    ; ACTION!
    ; The system doesn't preserve the literal spec, so it must be rebuilt
    ; from combining the the ADJUNCT-OF information.

    write-stdout unspaced [
        mold name ":" _ "lambda" _ mold spec-of f
    ]

    ; While all interfaces as far as invocation is concerned has been unified
    ; under the single ACTION! interface, the issue of getting things like
    ; some kind of displayable "source" would have to depend on the dispatcher
    ; used.  For the moment, BODY OF hands back limited information.  Review.
    ;
    let body: body of f
    switch:type body [
        block! [  ; FUNC, LAMBDA
            print [mold body]
        ]

        frame! [  ; SPECIALIZE (or DOES of an ACTION!)
            print [mold body]
        ]
    ] else [
        print "...native code, no source available..."
    ]

    return ~
]


what: func [
    "Prints a list of known actions"

    return: [~ block!]
    @name [<end> word! lit-word?]
        "Optional module name"
    :args "Show arguments not titles"
    :as-block "Return data as block"
][
    let list: make block! 400
    let size: 0

    ; copy to get around error: "temporary hold for iteration"
    let ctx: (copy maybe select system.modules maybe name) else [lib]

    for-each [word val] ctx [
        ; word val
        if (activation? :val) or (action? :val) [
            arg: either args [
                mold words of :val
            ][
                description-of :val else ["(undocumented)"]
            ]
            append list spread reduce [word arg]
            size: max size length of to-text word
        ]
    ]
    let list: sort:skip list 2

    name: make text! size
    if as-block [
        return list
    ]

    for-each [word arg] list [
        append:dup clear name #" " size
        change name word
        print [
            name
            :arg
        ]
    ]
    return ~
]


bugs: func [return: [~]] [
    "View bug database."
][
    browse https://github.com/metaeducation/ren-c/issues
]


; temporary solution to ensuring scripts run on a minimum build
;
require-commit: func [
    "checks current commit against required commit"

    return: [~]
    commit [text!]
][
    let c: select system.script.header 'commit else [return ~]

    ; If we happen to have commit information that includes a date, then we
    ; can look at the date of the running Rebol and know that a build that is
    ; older than that won't work.
    ;
    all [
        let date: select c 'date
        system.build < date

        fail [
            "This script needs a build newer or equal to" date
            "so run `upgrade`"
        ]
    ]

    ; If there's a specific ID then assume that if the current build does not
    ; have that ID then there *could* be a problem.
    ;
    let id: select c 'id
    if id and (id <> commit) [
        print [
            "This script has only been tested again commit" id LF

            "If it doesn't run as expected"
            "you can try seeing if this commit is still available" LF

            "by using the `do @dl-renc` tool and look for"
            unspaced [
                "r3-" copy:part id 7 "*"
                if find-last form system.version "0.3.4" [%.exe]
            ]
        ]
    ]
]
