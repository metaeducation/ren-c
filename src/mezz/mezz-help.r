Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Help"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
]

/decorated-words-of: func [
    "Get the decorated parameters as a block (useful for testing)"
    return: [block!]
    frame [frame!]
][
    return map-each [key param] (parameters of frame) [
        decorate param key
    ]
]

/description-of: lambda [
    "One-line summary of a value's purpose"

    []: [<null> text!]
    v [any-stable?]
][
    switch:type v [
        any-list?/ [
            spaced ["list of length:" length of v]
        ]
        datatype! [
            mold v
        ]
        frame! [
            (return of v).text
        ]
        object! [mold words of v]
        parameter! [mold v]
        port! [mold reduce [v.spec.title v.spec.ref]]
    ] else [
        null
    ]
]


print-general-help: proc [] [
    print trim:auto copy --[
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
    ]--
]


help-action: proc [
    ^action [action!]
    :name [word! tuple! path!]
][
    name: default [label of ^action]
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

    for-each [key param] ^action [
        if param.optional [
            append refinements key
            append deco-refinements decorate param key
        ] else [
            append args key
            append deco-args decorate param key
        ]
    ]

    ; Output exemplar calling string, e.g. LEFT + RIGHT or FOO A B C
    ;
    all [infix? ^action, not empty? deco-args] then [
        print [____ @deco-args.1 name @(spread next deco-args)]
    ] else [
        print [____ name @(spread deco-args) @(spread deco-refinements)]
    ]

    print newline

    ; !!! Note that an action can have an ordinary parameter named RETURN.
    ; If it does then it will be listed in the arguments.  RETURN OF works
    ; on all functions, and the `text` of the PARAMETER! is treated as the
    ; description of the function on the whole.
    ;
    let return-param: return of ^action

    print "DESCRIPTION:"
    print [____ (any [return-param.text, "(undocumented)"])]

    let print-args: [list :indent-words] -> [
        for-each key list [
            let param: get:dual key
            print [____ @(decorate param key) @(opt param.spec)]
            if param.text [
                print [____ ____ param.text]
            ]
        ]
    ]

    if return-param [
        print newline
        print [
            "RETURNS:" (any [mold opt return-param.spec, "(undocumented)"])
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
]


help-value: proc [
    "Give non-dialected help for an atom with any datatype"

    ^value [any-value?]
    :name [word! tuple! path!]
][
    if name [
        name: uppercase form name
    ]

    case [  ; !!! should come from %types.r
        void? ^value ['void!]
        pack? ^value ['pack!]
        trash? ^value ['trash!]
        action? ^value ['action!]
        failure? ^value ['failure!]

        logic? ^value ['logic!]
        splice? ^value ['splice!]

        antiform? ^value [
            panic "Invalid Antiform Heart Found, Please Report:" @value'
        ]
    ] then (antitype -> [
        print [(opt name) "is" (an antitype) "antiform"]
        if action? ^value [
            help-action ^value
            return
        ]
        let [molded truncated]: mold:limit lift ^value 2000  ; quasiform
        print unspaced [molded (if truncated ["..."]) _ _ "; anti"]
        return
    ])

    value: decay ^value  ; Note: synonym for (value: ^value)

    print [opt name "is an element of type" to word! type of value]
    if free? value [
        print "!!! contents no longer available, as it has been FREE'd !!!"
        return
    ]
    if match [object! port!] value [
        for-each 'line summarize-obj value [
            print line
        ]
        return
    ]
    let [molded truncated]: mold:limit value 2000
    print unspaced [molded (if truncated ["..."])]
]


/help: proc [
    --[HELP is a dialected function.  If you want non-dialected help on any
    particular value, then pass that value in a GROUP! to get some very
    literal information back:

        help ('insert)   ; will tell you that INSERT is a WORD!
        help (10 + 20)   ; will tell you 30 is an INTEGER!

        help-value 10 + 20  ; alternative way to get this information

    Other usages have meaning dependent on the datatype.

    [WORD! TUPLE!] - Behavior depends on the looked-up value

        help insert  ; describe argument types and return value of INSERT
        help integer!  ; shows all the integer variables in the system

    [TEXT!] - Search help strings in system for the given text as substring:

        help "insert"  ; will find INSERT, INSERT-ITEMS, DO-INSERT, etc.

    [RUNE!] - Browse online topics with the given tag:

        help #compiling

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
        help datatype!]--

    @topic "WORD! to explain, or other HELP target (if no args, general help)"
        [<hole> element?]
    :web "Open web browser to related documentation."
][
    if not topic [  ; just `>> help` or `eval [help]` or similar
        print-general-help
        return
    ]

    if web [
        if not word? topic [
            panic "HELP:WEB only works on WORD! at this time"
        ]

        let string: to text! topic
        if "!" = last string [
            browse join url! [
                http://www.rebol.com/r3/docs/datatypes/ (string) ".html"
            ]
            return
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
        return
    ]

    let make-libuser: does [  ; hacky unified context for searching
        let libuser: copy system.contexts.user
        for-each [key ^val] system.contexts.lib [
            if not has libuser key [
               ignore set meta (extend libuser key) ^val
            ]
        ]
        libuser
    ]

    switch:type topic [
        group! [
            help-value eval topic
        ]

        word! tuple! path! [
            let ^value: get meta topic except (e -> [
                print form e  ; not bound, etc.
                return
            ])
            if action? ^value [
                help-action:name ^value topic  ; bypass print name
            ] else [
                help-value:name ^value topic  ; prints name (should it?)
            ]
        ]

        rune! [  ; look up hashtag on web
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

    return
]


source: proc [
    "Prints the source code for an ACTION! (if available)"

    @(arg) [
        word! tuple! "treat as variable, look it up"
        frame! "give function source, if available"
        tag! "get URL! of library identified by tag, e.g. (source <json>)"
    ]
][
    let name
    let f
    switch:type arg [
        tag! [
            f: copy "unknown tag"
            for-each [key url] system.locale.library [
                if location: select load url arg [
                    f: form location.1
                    break
                ]
            ]
        ]

        word! path! [
            name: arg
            if action? (^f: get meta arg else [
                print [name "is not set to a value"]
                return
            ]) [
                f: unrun f/
            ]
        ]
    ] else [
        name: '~anonymous~
        f: arg
    ]

    case [
        match [text! url!] f [
            print f
        ]
        not frame? (unrun f) [
            print [
                mold name "is" an any [mold opt type of f, "NULL"]
                "and not a FRAME!"
            ]
        ]
    ] then [
        return
    ]

    let ret: ensure parameter! return of f
    let spec: collect [
        keep:line (opt ret.text)  ; RETURN:'s text is overall description

        if ret.spec [
            keep compose ~[
                return: (ret.spec)
            ]~
        ]

        for-each [key param] runs f [  ; ACTION! enumeration gives PARAMETER!
            keep compose ~[
                (decorate param key) (opt param.text)
                    (opt param.spec)
            ]~
        ]
    ]

    write stdout unspaced [
        mold name ":" _ "lambda" _ mold spec
    ]

  ; !!! A native implementation could give a link to a GitHub repo so you
  ; could read the source.

    let body: try body of f

    switch:type body [
        null?/ [
            print "...native code, no source available..."
        ]

        block! [  ; FUNC, LAMBDA
            print [mold body]
        ]

        frame! [  ; SPECIALIZE (or DOES of an ACTION!)
            print [mold body]
        ]
    ]
]


/what: func [
    "Prints a list of known actions"

    return: [trash! block!]
    @name [<hole> word! 'word!]
        "Optional module name"
    :args "Show arguments not titles"
    :as-block "Return data as block"
][
    let list: make block! 400
    let size: 0

    ; copy to get around error: "temporary hold for iteration"
    let ctx: (copy cond select system.modules cond name) else [lib]

    for-each [word ^val] ctx [
        ; word val
        if action? ^val [
            arg: either args [
                mold words of ^val
            ][
                description-of ^val else ["(undocumented)"]
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
    return
]


bugs: proc [
    "View bug database."
][
    browse https://github.com/metaeducation/ren-c/issues
]


; temporary solution to ensuring scripts run on a minimum build
;
require-commit: proc [
    "checks current commit against required commit"

    commit [text!]
][
    let c: select system.script.header 'commit else [return]

    ; If we happen to have commit information that includes a date, then we
    ; can look at the date of the running Rebol and know that a build that is
    ; older than that won't work.
    ;
    all [
        let date: select c 'date
        system.build < date

        panic [
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
