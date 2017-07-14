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
        if (length str) > 48 [str: append copy/part str 45 "..."]
        str
    ]

    form-val: func [val [any-value!]] [
        ; Form a limited string from the value provided.
        if any-block? :val [return spaced ["length:" length-of val]]
        if image? :val [return spaced ["size:" val/size]]
        if datatype? :val [return form val] 
        if function? :val [
            return clip-str any [title-of :val mold spec-of :val]
        ]
        if object? :val [val: words-of val]
        if typeset? :val [val: to-block val]
        if port? :val [val: reduce [val/spec/title val/spec/ref]]
        if gob? :val [return spaced ["offset:" val/offset "size:" val/size]]
        clip-str mold :val
    ]

    form-pad: func [val size] [
        ; Form a value with fixed size (space padding follows).
        val: form val
        insert/dup tail val #" " size - length-of val
        val
    ]

    ; Search for matching strings:
    collect [
        wild: all [set? 'pat | string? pat | find pat "*"]

        for-each [word val] obj [
            type: type-of :val

            str: either maybe [function! object!] :type [
                spaced [word _ mold spec-of :val _ words-of :val]
            ][
                form word
            ]

            if any [
                not match
                all [
                    not void? :val
                    either string? :pat [
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
                append str form-pad type 10 - ((length-of str) - 15)
                keep spaced [
                    "  " str
                    if type [form-val :val]
                    newline
                ]
            ]
        ]
    ]
]


dump: proc [
    {Show the name of a value (or block of expressions) with the value itself}
    :value [any-value! <...>]
    <local>
        dump-one dump-val clip-string item set-word result
][
    if bar? first value [
        take value
        leave
    ] ;-- treat this DUMP as disabled, `dump | x`

    clip-string: function [str len][
       either len < length-of str [
          delimit [ copy/part str len - 3 "..." ] _
       ][
          str
       ]
    ]

    dump-val: function [val][
        either object? val [
           unspaced [
              "make object! [" |
              dump-obj val | "]"
           ]
        ][
           clip-string mold val system/options/dump-size
        ]
    ]

    dump-one: proc [item][
        case [
            string? item [ ;-- allow customized labels
                print ["---" clip-string item system/options/dump-size "---"]
            ]

            word? item [
                print [to set-word! item "=>" dump-val get item]
            ]

            path? item [
                print [to set-path! item "=>" dump-val get item]
            ]

            group? item [
                trap/with [
                    print [item "=>" mold eval item]
                ] func [error] [
                    print [item "=!!!=>" mold error]
                ]
            ]
        ] else [
            fail [
                "Item not WORD!, PATH!, or GROUP! in DUMP." item
            ]
        ]
    ]

    case [
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
        
        true [
            dump-one take value
        ]
    ]
]


spec-of: function [
    {Generate a block which could be used as a "spec block" from a function.}

    value [function!]
][
    meta: maybe object! meta-of :value

    specializee: maybe function! select meta 'specializee
    adaptee: maybe function! select meta 'specializee
    original-meta: maybe object! any [
        all [:specializee | meta-of :specializee]
        all [:adaptee | meta-of :adaptee]
    ]

    spec: copy []

    if description: maybe string! any [
        select meta 'description
        select original-meta 'description
    ][
        append spec description
        new-line back spec true
    ]

    return-type: maybe block! any [
        select meta 'return-type
        select original-meta 'return-type
    ]
    return-note: maybe string! any [
        select meta 'return-note
        select original-meta 'return-note
    ]
    if return-type or return-note [
        append spec quote return:
        if return-type [append/only spec return-type]
        if return-note [append spec return-note]
    ]

    types: maybe frame! any [
        select meta 'parameter-types
        select original-meta 'parameter-types
    ]
    notes: maybe frame! any [
        select meta 'parameter-notes
        select original-meta 'parameter-notes
    ]

    for-each param words-of :value [
        append spec param
        if any [type: select types param] [append/only spec type]
        if any [note: select notes param] [append spec note]
    ]

    return spec
]


title-of: function [
    {Extracts a summary of a value's purpose from its "meta" information.}

    value [any-value!]
][
    switch type-of :value [
        :function! [
            all [
                object? meta: meta-of :value
                string? description: select meta 'description
                copy description
            ]
        ]

        :datatype! [
            spec: spec-of value
            assert [string? spec] ;-- !!! Consider simplifying "type specs"
            spec/title
        ]

        (blank)
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
    'word [<end> any-value!]
    /doc "Open web browser to related documentation."
][
    if not set? 'word [
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
                help function!
                help datatype!

            Other debug functions:

                docs - open browser to web documentation
                dump - display a variable and its value
                probe - print a value (molded)
                source func - show source code of func
                trace - trace evaluation steps
                what - show a list of known functions
                why? - explain more about last error (via web)

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

                ;docs - open DocBase document wiki website
                ;demo - run demo launcher (from rebol.com)


;           Word completion:
;
;               The command line can perform word
;               completion. Type a few chars and press TAB
;               to complete the word. If nothing happens,
;               there may be more than one word that
;               matches. Press TAB again to see choices.
;
;               Local filenames can also be completed.
;               Begin the filename with a %.
;
;           Other useful functions:
;
;               about - see general product info
;               usage - view program options
;               license - show terms of user license
;               source func - view source of a function
;               upgrade - updates your copy of REBOL
;
;           More information: http://www.rebol.com/docs.html

    r3n: https://r3n.github.io/

    ;; help #topic (browse r3n for topic)
    if issue? :word [
        say-browser
        browse join-all [r3n "topics/" next to-string :word]
        leave
    ]

    if all [word? :word | blank? context-of word] [
        print [word "is an unbound WORD!"]
        leave
    ]

    if all [word? :word | not set? word] [
        print [word "is bound to a context, but has no value."]
        leave
    ]

    ; Open the web page for it?
    if all [
        doc
        word? :word
        any [function? get :word datatype? get :word]
    ][
        item: form :word
        browse join-of 
        either function? get :word [
            for-each [a b] [ ; need a better method !
                "!" "-ex"
                "?" "-q"
                "*" "-mul"
                "+" "-plu"
                "/" "-div"
                "=" "-eq"
                "<" "-lt"
                ">" "-gt"
                "|" "-bar"
            ][replace/all item a b]
            tmp: %.MD
            https://github.com/gchiu/reboldocs/blob/master/
        ][
            remove back tail item ; the !
            tmp: %.html
            http://www.rebol.com/r3/docs/datatypes/
        ]
        [item tmp]
    ]

    if all [word? :word | set? :word | datatype? get :word] [
        types: dump-obj/match make lib system/contexts/user :word
        if not empty? types [
            print ["Found these" (uppercase form word) "words:" newline types]
        ] else [
            print [word {is a datatype}]
        ]
        leave
    ]

    ; If arg is a string, search the system:
    if string? :word [
        types: dump-obj/match make lib system/contexts/user :word
        sort types
        if not empty? types [
            print ["Found these related words:" newline types]
            leave
        ]
        print ["No information on" word]
        leave
    ]

    ; Print type name with proper singular article:
    type-name: func [value [any-value!]] [
        value: mold type-of :value
        clear back tail value
        spaced [(either find "aeiou" first value ["an"]["a"]) value]
    ]

    ; Print literal values:
    if not any [word? :word path? :word][
        print [mold :word "is" type-name :word]
        leave
    ]

    ; Functions are not infix in Ren-C, only bindings of words to infix, so
    ; we have to read the infixness off of the word before GETting it.

    ; Get value (may be a function, so handle with ":")
    either path? :word [
        print ["!!! NOTE: Infix testing not currently supported for paths !!!"]
        lookback: false
        if any [
            error? set/opt 'value trap [get :word] ;trap reduce [to-get-path word]
            not set? 'value
        ][
            print ["No information on" word "(path has no value)"]
            leave
        ]
    ][
        lookback: lookback? :word
        value: get :word
    ]

    unless function? :value [
        print/only spaced [
            (uppercase mold word) "is" (type-name :value) "of value: "
        ]
        print unspaced collect [
            either maybe [object! port!] value [
                keep newline
                keep dump-obj value
            ][
                keep mold value
            ]
        ]
        leave
    ]

    ; Must be a function...
    ; If it has refinements, strip them:
    ;if path? :word [word: first :word]

    space4: unspaced [space space space space] ;-- use instead of tab

    ;-- Print info about function:
    print "USAGE:"

    args: _ ;-- plain arguments
    refinements: _ ;-- refinements and refinement arguments

    parse words-of :value [
        copy args any [word! | get-word! | lit-word! | issue!]
        copy refinements any [
            refinement! | word! | get-word! | lit-word! | issue!
        ]
    ]

    ; Output exemplar calling string, e.g. LEFT + RIGHT or FOO A B C
    ; !!! Should refinement args be shown for lookback case??
    ;
    either lookback [
        print [space4 args/1 (uppercase mold word) next args]
    ][
        print [space4 (uppercase mold word) args refinements]
    ]

    ; Dig deeply, but try to inherit the most specific meta fields available
    ;
    fields: dig-function-meta-fields :value

    description: fields/description
    return-type: :fields/return-type
    return-note: fields/return-note
    types: fields/parameter-types
    notes: fields/parameter-notes

    ; For reporting what kind of function this is, don't dig at all--just
    ; look at the meta information of the function being asked about
    ;
    meta: meta-of :value
    all [
        original-name: maybe word! (
            any [
                select meta 'specializee-name
                select meta 'adaptee-name
            ]
        )
        original-name: uppercase mold original-name
    ]

    specializee: maybe function! select meta 'specializee
    adaptee: maybe function! select meta 'adaptee
    chainees: maybe block! select meta 'chainees

    classification: case [
        :specializee [
            either original-name [
                spaced [{a specialization of} original-name]
            ][
                {a specialized function}
            ]
        ]

        :adaptee [
            either original-name [
                spaced [{an adaptation of} original-name]
            ][
                {an adapted function}
            ]
        ]

        :chainees [
            {a chained function}
        ]
    ] else {a function}

    print-newline

    print [
        "DESCRIPTION:"
            |
        space4 (any [description | "(undocumented)"])
            |
        space4 (uppercase mold word) {is} classification {.}
    ]

    print-args: procedure [list /indent-words] [
        for-each param list [
            note: maybe string! select notes to-word param
            type: maybe [block! any-word!] select types to-word param

            ;-- parameter name and type line
            either all [type | not refinement? param] [
                print/only [space4 param space "[" type "]" newline]
            ][
                print/only [space4 param newline]
            ]

            if note [
                print/only [space4 space4 note newline]
            ]
        ]
    ]

    either blank? :return-type [
        ; If it's a PROCEDURE, saying "RETURNS: void" would waste space
    ][
        ; For any return besides "always void", always say something about
        ; the return value...even if just to say it's undocumented.
        ;
        print-newline
        print ["RETURNS:" (if set? 'return-type [mold return-type])]
        either return-note [
            print/only [space4 return-note newline]
        ][
            if not set? 'return-type [
                print/only [space4 "(undocumented)" newline]
            ]
        ]
    ]

    unless empty? args [
        print-newline
        print "ARGUMENTS:"
        print-args args
    ]

    unless empty? refinements [
        print-newline
        print "REFINEMENTS:"
        print-args/indent-words refinements
    ]
]


; !!! MAKE is used here to deliberately avoid the use of an abstraction,
; because of the adaptation of SOURCE to be willing to take an index that
; indicates the caller's notion of a stack frame.  (So `source 3` would
; give the source of the function they saw labeled as 3 in BACKTRACE.)
;
; The problem is that if FUNCTION is implemented using its own injection of
; unknown stack levels, it's not possible to count how many stack levels
; the call to source itself introduced.
;
; !!! This is fairly roundabout and probably should just make users type
; `source backtrace 5` or similar.  Being left as-is for the talking point
; of how to implement functions which want to do this kind of thing.
;
source: make function! [[
    "Prints the source code for a function."
    'arg [integer! word! path! function! tag!]
        {If integer then the function backtrace for that index is shown}

    f: name: ; pure locals
][
    case [
        tag? :arg [
            f: copy "unknown tag"
            for-each location words-of system/locale/library [
                if location: select load get location arg [
                    f: location/1
                    break
                ]
            ]
        ]
        maybe [word! path!] :arg [
            name: arg
            f: get :arg
        ]

        integer? :arg [
            name: unspaced ["backtrace-" arg]

            ; We add two here because we assume the caller meant to be
            ; using as point of reference what BACKTRACE would have told
            ; *them* that index 1 was... not counting when SOURCE and this
            ; nested CASE is on the stack.
            ;
            ; !!! A maze of questions are opened by this kind of trick,
            ; which are beyond the scope of this comment.

            ; The usability rule for backtraces is that 0 is the number
            ; given to a breakpoint if it's the top of the stack (after
            ; backtrace removes itself from consideration).  If running
            ; SOURCE when under a breakpoint, the rule will not apply...
            ; hence the numbering will start at 1 and the breakpoint is
            ; now 3 deep in the stack (after SOURCE+CASE).  Yet the
            ; caller is asking about 1, 2, 3... or even 0 for what they
            ; saw in the backtrace as the breakpoint.
            ;
            ; This is an interim convoluted answer to how to resolve it,
            ; which would likely be done better with a /relative refinement
            ; to backtrace.  Before investing in that, some usability
            ; experience just needs to be gathered, so compensate.
            ;
            f: function-of backtrace (
                1 ; if BREAKPOINT, compensate differently (it's called "0")
                + 1 ; CASE
                + 1 ; SOURCE
            )
            f: function-of backtrace (
                arg
                ; if breakpoint there, bump 0 up to a 1, 1 to a 2, etc.
                + (either :f == :breakpoint [1] [0])
                + 1 ; CASE
                + 1 ; SOURCE
            )

            unless :f [
                print ["Stack level" arg "does not exist in backtrace"]
            ]
        ]
    ] else [
        name: "anonymous"
        f: :arg
    ]

    case [
        function? :f [
            print unspaced [mold name ":" space mold :f]
        ]
        any [string? :f url? :f][
            print f
        ]
        true [
            print [name "is a" mold type-of :f "and not a FUNCTION!"]
        ]
    ]
    () ;-- return nothing, as with a PROCEDURE
]]


what: procedure [
    {Prints a list of known functions.}
    'name [<opt> word! lit-word!]
        "Optional module name"
    /args
        "Show arguments not titles"
][
    list: make block! 400
    size: 0

    ctx: any [select system/modules :name | lib]

    for-each [word val] ctx [
        if function? :val [
            arg: either args [
                arg: words-of :val
                clear find arg /local
                mold arg
            ][
                title-of :val
            ]
            append list reduce [word arg]
            size: max size length-of to-string word
        ]
    ]

    vals: make string! size
    for-each [word arg] sort/skip list 2 [
        append/dup clear vals #" " size
        print [head change vals word | :arg]
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
