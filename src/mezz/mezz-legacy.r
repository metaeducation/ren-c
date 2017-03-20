REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Legacy compatibility"
    Homepage: https://trello.com/b/l385BE7a/porting-guide
    Rights: {
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        These definitions attempt to create a compatibility mode for Ren-C,
        so that it operates more like R3-Alpha.

        Some "legacy" definitions (like `foreach` as synonym of `for-each`)
        are enabled by default, and may remain indefinitely.  Other changes
        may be strictly incompatible: words have been used for different
        purposes, or variations in natives of the same name.  Hence it is
        necessary to "re-skin" the environment, by running:

            do <r3-legacy>

        (Dispatch for this from DO is in the DO* function of %sys-base.r)

        This statement will do nothing in older Rebols, since executing a
        tag evaluates to just a tag.

        Though as much of the compatibility bridge as possible is sought to
        be implemented in user code, some flags affect the executable behavior
        of the evaluator.  To avoid interfering with the native performance of
        Ren-C, THESE ARE ONLY ENABLED IN DEBUG BUILDS.  Be aware of that.

        Legacy mode is intended to assist in porting efforts to Ren-C, and to
        exercise the abilities of the language to "flex".  It is not intended
        as a "supported" operating mode.  Contributions making it work more
        seamlessly are welcome, but scheduling of improvements to the legacy
        mode are on a strictly "as-needed" basis.
    }
    Notes: {
        At present it is a one-way street.  Once `do <r3-legacy>` is run,
        there is no clean "shutdown" of legacy mode to go back to plain Ren-C.

        The current trick will modify the user context directly, and is not
        module-based...so you really are sort of "backdating" the system
        globally.  A more selective version that turns features on and off
        one at a time to ease porting is needed, perhaps like:

            do/args <r3-legacy> [
                new-do: off
                question-marks: on
            ]
    }
]

; This identifies if r3-legacy mode is has been turned on, useful mostly
; to avoid trying to turn it on twice.
;
r3-legacy-mode: off


; Ren-C *prefers* the use of GROUP! to PAREN!, both likely to remain legal.
; https://trello.com/c/ANlT44nH
;
paren?: :group?
paren!: :group!
to-paren: :to-group


; CONSTRUCT (arity 2) and HAS (arity 1) have arisen as the OBJECT!-making
; routines, parallel to FUNCTION (arity 2) and DOES (arity 1).  By not being
; nouns like CONTEXT and OBJECT, they free up those words for other usages.
; For legacy support, both CONTEXT and OBJECT are just defined to be HAS.
;
; Note: Historically OBJECT was essentially a synonym for CONTEXT with the
; ability to tolerate a spec of `[a:]` by transforming it to `[a: none].
; The tolerance of ending with a set-word has been added to CONSTRUCT+HAS
; so this distinction is no longer required.
;
context: object: :has


; General renamings away from non-LOGIC!-ending-in-?-functions
; https://trello.com/c/DVXmdtIb
;
index?: :index-of
offset?: :offset-of
sign?: :sign-of
suffix?: :suffix-of

comment [
    ; !!! Less common cases still linger as question mark routines that
    ; don't return LOGIC!, and they seem like they need greater rethinking in
    ; general. What replaces them (for ones that are kept) might be new.
    ;
    encoding?: _
    file-type?: _
    speed?: _
    why?: _
    info?: _
    exists?: _
]


; Semi-controversial choice to take a noun to avoid "lengthening LENGTH?"
; https://trello.com/c/4OT7qvdu
;
length?: :length


; FOREACH isn't being taken for anything else, may stay a built-in synonym
; https://trello.com/c/cxvHGNha
;
foreach: :for-each


; FOR-NEXT lets you switch series (unlike FORALL), see also FOR-BACK
; https://trello.com/c/StCADPIB
;
forall: :for-next
forskip: :for-skip


; Both in user code and in the C code, good to avoid BLOCK! vs. ANY-BLOCK!
; https://trello.com/c/lCSdxtux
;
any-block!: :any-array!
any-block?: :any-array?


; Similarly to the BLOCK! and ANY-BLOCK! problem for understanding the inside
; and outside of the system, ANY-CONTEXT! is a better name for the superclass
; of OBJECT!, ERROR!, PORT! and (likely to be killed) MODULE!

any-object!: :any-context!
any-object?: :any-context?


; Typesets containing ANY- helps signal they are not concrete types
; https://trello.com/c/d0Nw87kp
;
number!: :any-number!
number?: :any-number?
scalar!: :any-scalar!
scalar?: :any-scalar?
series!: :any-series!
series?: :any-series?


; ANY-TYPE! is ambiguous with ANY-DATATYPE!
; https://trello.com/c/1jTJXB0d
;
; It is not legal for user-facing typesets to include the idea of containing
; a void type or optionality.  Hence, ANY-TYPE! cannot include void.  The
; notion of tolerating optionality must be encoded outside a typeset (Note
; that `find any-type! ()` didn't work in R3-Alpha, either.)
;
; The r3-legacy mode FUNC and FUNCTION explicitly look for ANY-TYPE! and
; replaces it with <opt> any-value! in the function spec.
;
any-type!: any-value!


; BIND? and BOUND? didn't fit the naming convention of returning LOGIC! if
; they end in a question mark.  Also, CONTEXT-OF is more explicit about the
; type of the return result, which makes it more useful than BINDING-OF or
; BIND-OF as a name.  (Result can be an ANY-CONTEXT!, including FRAME!)
;
bound?: :context-of
bind?: :context-of


; !!! Technically speaking all frames should be "selfless" in the sense that
; the system does not have a particular interest in the word "self" as
; applied to objects.  Generators like OBJECT may choose to establish a
; self-bearing protocol.
;
selfless?: func [context [any-context!]] [
    fail {selfless? no longer has meaning (all frames are "selfless")}
]

unset!: func [dummy:] [
    fail/where [
        {UNSET! is not a datatype in Ren-C.}
        {You can test with VOID? (), but the TYPE-OF () is a NONE! *value*}
        {So NONE? TYPE-OF () will be TRUE.}
    ] 'dummy
]

unset?: func [dummy:] [
    fail/where [
        {UNSET? is reserved in Ren-C for future use}
        {(Will mean VOID? GET, like R3-Alpha VALUE?, only for WORDs/PATHs}
        {Use VOID? for a similar test, but be aware there is no UNSET! type}
        {If running in <r3-legacy> mode, old UNSET? meaning is available}
    ] 'dummy
]

value?: func [dummy:] [
    fail/where [
        {VALUE? is reserved in Ren-C for future use}
        {(It will be a shorthand for ANY-VALUE! a.k.a. NOT VOID?)}
        {SET? is similar to R3-Alpha VALUE?--but SET? only takes words}
        {If running in <r3-legacy> mode, old VALUE? meaning is available.}
    ] 'dummy
]

none-of: :none ;-- reduce mistakes for now by renaming NONE out of the way

none?: none!: none: func [dummy:] [
    fail/where [
        {NONE is reserved in Ren-C for future use}
        {(It will act like NONE-OF, e.g. NONE [a b] => ALL [not a not b])}
        {_ is now a "BLANK! literal", with BLANK? test and BLANK the word.}
        {If running in <r3-legacy> mode, old NONE meaning is available.}
    ] 'dummy
]

type?: func [dummy:] [
    fail/where [
        {TYPE? is reserved in Ren-C for future use}
        {(Though not fixed in stone, it may replace DATATYPE?)}
        {TYPE-OF is the current replacement, with no TYPE-OF/WORD}
        {Use soft quotes, e.g. SWITCH TYPE-OF 1 [:INTEGER! [...]]}
        {If running in <r3-legacy> mode, old TYPE? meaning is available.}
    ] 'dummy
]

found?: func [dummy:] [
    fail/where [
        {FOUND? is deprecated in Ren-C, see chained function FIND?}
        {FOUND? is available if running in <r3-legacy> mode.}
    ] 'dummy
]

op?: func [dummy:] [
    fail/where [
        {OP? can't work in Ren-C because there are no "infix FUNCTION!s"}
        {"infixness" is a proerty of a word binding, made via SET/LOOKBACK}
        {See: LOOKBACK?, INFIX?, PREFIX?, ENDFIX?}
    ] 'dummy
]


; The legacy PRIN construct is replaced by PRINT/ONLY SPACED
;
prin: procedure [
    "Print spaced w/no added terminal line break, reducing blocks."

    value [<opt> any-value!]
][
    print/only/eval either block? :value [spaced :value] [:value]
]

; Common debug abbreviations that should be console-only (if anything)
;
dt: :delta-time
dp: :delta-profile


; AJOIN is a kind of ugly name for making an unspaced string from a block.
; REFORM is nonsensical looking.  Ren-C has UNSPACED and SPACED.
;
ajoin: :unspaced
reform: :spaced



; REJOIN in R3-Alpha meant "reduce and join"; the idea of cumulative joining
; in Ren-C already implies reduction of the appended data.  JOIN-ALL is a
; friendlier name, suggesting joining with the atomic root type of the first
; reduced element.
;
; JOIN-ALL is not exactly the same as REJOIN; and it is not used as often
; because UNSPACED can be used for strings, with AS allowing aliasing of the
; data as other string types (`as tag! unspaced [...]` will not create a copy
; of the series data the way TO TAG! would).  While REJOIN is tolerant of
; cases like `rejoin [() () ()]` producing an empty block, this makes a
; void in JOIN-ALL...but that is a common possibility.
;
rejoin: chain [
    :join-all
        |
    func [v [<opt> any-series!]] [
        either set? 'v [:v] [copy []]
    ]
]


; SET has a refinement called /ANY which doesn't communicate as well in the
; Ren-C world as OPT.  OPT is the marker on functions to mark parameters as
; optional...OPT is the function to convert NONE! to UNSET! while passing
; all else through.  It has a narrower and more communicative focus of purpose
; than /ANY does (also ANY is a very common function with a very different
; meaning and sense)
;
lib-set: :set ; overwriting lib/set for now
set: function [
    {Sets a word, path, block of words, or context to specified value(s).}

    return: [<opt> any-value!]
        {Just chains the input value (unmodified)}
    target [any-word! any-path! block! any-context!]
        {Word, block of words, path, or object to be set (modified)}
    value [<opt> any-value!]
        "Value or block of values"
    /opt
        "Value is optional, and if no value is provided then unset the word"
    /pad
        {For objects, set remaining words to NONE if block is too short}
    /lookback
        {If value is a function, then make the bound word dispatch infix}
    /any
        "Deprecated legacy synonym for /opt"
][
    set_ANY: any
    any: :lib/any
    set_OPT: opt
    opt: :lib/opt

    apply 'lib-set [
        target: target
        value: :value
        opt: any? [set_ANY set_OPT]
        pad: pad
        lookback: lookback
    ]
]


; This version of get supports the legacy /ANY switch that has been replaced
; by /OPT (but since the switch is new, it would be disruptive to remove it
; entirely immediately... /ANY will be moved to the r3-legacy mode after
; more codebases have adapted to /OPT.  If it is to stay even longer, it may
; need to be done via a thinner legacy proxy which can rename refinements but
; not cost a function body execution.)
;
; Historical GET in Rebol allowed any type that wasn't UNSET!.  If you said
; something like `get 1` this would be passed through as `1`.  Both Ren-C and
; Red have removed that feature, so it's only enabled in legacy mode.  R3-Gui
; was dependent on the fallthrough behavior and other legacy clients may be
; also, so this is a more tolerant variant of LIB/GET for now.
;
; Note: It is questionable to use it as a way of getting the fields of an
; object (likely better suited to reflection)--the SET parallel actually
; assumes a positional ordering of fields, disallowed in PICK (should be the
; same rule, probably neither should work.)
;
lib-get: :get
get: function [
    {Gets the value of a word or path, or values of a context.}
    return: [<opt> any-value!]
    source
        "Word, path, context to get"
    /opt
        "The source may optionally have no value (allows returning void)"
    /any
        "Deprecated legacy synonym for /OPT"
][
    any_GET: any
    any: :lib/any
    opt_GET: opt
    opt: :lib/opt

    either* maybe? [blank! any-word! any-path! any-context! block!] :source [
        lib-get/(all [any [opt_GET any_GET] 'opt]) :source
    ][
        unless system/options/get-will-get-anything [
            fail [
                "GET takes ANY-WORD!, ANY-PATH!, ANY-CONTEXT!, not" (:source)
            ]
        ]
        :source
    ]
]


; TRAP makes more sense as parallel-to-CATCH, /WITH makes more sense too
; https://trello.com/c/IbnfBaLI
;
try: func [
    {Tries to DO a block and returns its value or an error.}
    return: [<opt> any-value!]
    block [block!]
    /except
        "On exception, evaluate code"
    code [block! function!]
][
    either* except [trap/with block :code] [trap block]
]


; R3-Alpha's APPLY had a historically brittle way of handling refinements,
; based on their order in the function definition.  e.g. the following would
; be how to say saying `APPEND/ONLY/DUP A B 2`:
;
;     apply :append [a b none none true true 2]
;
; Ren-C's default APPLY construct is based on evaluating a block of code in
; the frame of a function before running it.  This allows refinements to be
; specified as TRUE or FALSE and the arguments to be assigned by name.  It
; also accepts a WORD! or PATH! as the function argument which it fetches,
; which helps it deliver a better error message labeling the applied function
; (instead of the stack frame appearing "anonymous"):
;
;     apply 'append [
;         series: a
;         value: b
;         only: true
;         dup: true
;         count: true
;     ]
;
; For most usages this is better, though it has the downside of becoming tied
; to the names of parameters at the callsite.  One might not want to remember
; those, or perhaps just not want to fail if the names are changed.
;
; This implementation of R3-ALPHA-APPLY is a stopgap compatibility measure for
; the positional version.  It shows that such a construct could be written in
; userspace--even implementing the /ONLY refinement.  This is hoped to be a
; "design lab" for figuring out what a better positional apply might look like.
;
r3-alpha-apply: function [
    "Apply a function to a reduced block of arguments."

    return: [<opt> any-value!]
    action [function!]
        "Function value to apply"
    block [block!]
        "Block of args, reduced first (unless /only)"
    /only
        "Use arg values as-is, do not reduce the block"
][
    frame: make frame! :action
    params: words-of :action
    using-args: true

    until [tail? block] [
        arg: either* only [
            also block/1 (block: next block)
        ][
            do/next block 'block
        ]

        either refinement? params/1 [
            using-args: set (in frame params/1) true? :arg
        ][
            if using-args [
                set/opt (in frame params/1) :arg
            ]
        ]

        params: next params
    ]

    comment [
        ;
        ; Too many arguments was not a problem for R3-alpha's APPLY, it would
        ; evaluate them all even if not used by the function.  It may or
        ; may not be better to have it be an error.
        ;
        unless tail? block [
            fail "Too many arguments passed in R3-ALPHA-APPLY block."
        ]
    ]

    do frame ;-- voids are optionals
]

; In Ren-C, FUNCTION's variables have indefinite extent (aka <durable>), and
; the body is specifically bound to those variables.  (There is no dynamic
; binding in Ren-C)
;
closure: func [
    return: [<opt> any-value!]
    spec
    body
][
    function compose [
        return: [<opt> any-value!]
        (spec)
    ] body
]

; FUNC variables are not durable by default, it must be specified explicitly.
;
clos: func [
    "Defines a closure function."
    spec [block!]
        {Help string (opt) followed by arg words (and opt type and string)}
    body [block!]
        "The body block of the function"
][
    func compose [<durable> (spec)] body
]

closure!: :function!
closure?: :function?

; All other function classes are also folded into the one FUNCTION! type ATM.

any-function!: :function!
any-function?: :function?

native!: function!
native?: func [f [<opt> any-value!]] [
    all [function? :f | 1 = func-class-of :f]
]

;-- If there were a test for user-written functions, what would it be called?
;-- it would be function class 2 ATM

action!: function!
action?: func [f [<opt> any-value!]] [
    all [function? :f | 3 = func-class-of :f]
]

command!: function!
command?: func [f [<opt> any-value!]] [
    all [function? :f | 4 = func-class-of :f]
]

routine!: function!
routine?: func [f [<opt> any-value!]] [
    all [function? :f | 5 = func-class-of :f]
]

callback!: function!
callback?: func [f [<opt> any-value!]] [
    all [function? :f | 6 = func-class-of :f]
]


; In Ren-C, MAKE for OBJECT! does not use the "type" slot for parent
; objects.  You have to use the arity-2 CONSTRUCT to get that behavior.
; Also, MAKE OBJECT! does not do evaluation--it is a raw creation,
; and requires a format of a spec block and a body block.
;
; Because of the commonality of the alternate interpretation of MAKE, this
; bridges until further notice.
;
; Also: bridge legacy calls to MAKE ROUTINE!, MAKE COMMAND!, and MAKE CALLBACK!
; while still letting ROUTINE!, COMMAND!, and CALLBACK! be valid to use in
; typesets invokes the new variadic behavior.  This can only work if the
; source literally wrote out `make routine!` vs an expression that evaluated
; to the routine! datatype (for instance) but should cover most cases.
;
lib-make: :make
make: function [
    "Constructs or allocates the specified datatype."
    return: [any-value!]
    :lookahead [any-value! <...>]
    type [<opt> any-value! <...>]
        "The datatype or an example value"
    def [<opt> any-value! <...>]
        "Attributes or size of the new value (modified)"
][
    switch first lookahead [
        callback! [
            verify [function! = take type]
            def: ensure block! take def
            ffi-spec: ensure block! first def
            action: ensure function! reduce second def
            return make-callback :action ffi-spec
        ]
        routine! [
            verify [function! = take type]
            def: ensure block! take def
            ffi-spec: ensure block! first def
            lib: ensure [integer! library!] reduce second def
            if integer? lib [ ;-- interpreted as raw function pointer
                return make-routine-raw lib ffi-spec
            ]
            name: ensure string! third def
            return make-routine lib name ffi-spec
        ]
        command! [
            verify [function! = take type]
            def: ensure block! take def
            return make-command def
        ]
    ]

    type: take type
    def: take def

    case [
        all [
            :type = object!
            block? :def
            not block? first def
        ][
            ;
            ; MAKE OBJECT! [x: ...] vs. MAKE OBJECT! [[spec][body]]
            ; This old style did evaluation.  Must use a generator
            ; for that in Ren-C.
            ;
            return has :def
        ]

        any [
            object? :type | struct? :type | gob? :type
        ][
            ;
            ; For most types in Rebol2 and R3-Alpha, MAKE VALUE [...]
            ; was equivalent to MAKE TYPE-OF VALUE [...].  But with
            ; objects, MAKE SOME-OBJECT [...] would interpret the
            ; some-object as a parent.  This must use a generator
            ; in Ren-C.
            ;
            ; The STRUCT!, GOB!, and EVENT! types had a special 2-arg
            ; variation as well, which is bridged here.
            ;
            return construct :type :def
        ]
    ]

    ; R3-Alpha would accept an example value of the type in the first slot.
    ; This is of questionable utility.
    ;
    unless datatype? :type [
        type: type-of :type
    ]

    if all [find any-array! :type | any-array? :def] [
        ;
        ; MAKE BLOCK! of a BLOCK! was changed in Ren-C to be
        ; compatible with the construction syntax, so that it lets
        ; you combine existing array data with an index used for
        ; aliasing.  It is no longer a synonym for TO ANY-ARRAY!
        ; that makes a copy of the data at the source index and
        ; changes the type.  (So use TO if you want that.)
        ;
        return to :type :def
    ]

    lib-make :type :def
]


; To invoke this function, use `do <r3-legacy>` instead of calling it
; directly, as that will be a no-op in older Rebols.  Notice the word
; is defined in sys-base.r, as it needs to be visible pre-Mezzanine
;
; !!! There are a lot of SET-WORD!s in this routine inside an object append.
; So it's a good case study of how one can get a very large number of
; locals if using FUNCTION.  Study.
;
set 'r3-legacy* func [<local> if-flags] [

    if r3-legacy-mode [return blank]

    ; NOTE: these flags only work in debug builds.  A better availability
    ; test for the functionality is needed, as these flags may be expired
    ; at different times on a case-by-case basis.
    ;
    ; (We don't flip these switches until after the above functions have been
    ; created, so that the shims can use Ren-C features like word-valued
    ; refinements/etc.)
    ;
    do in system/options [
        lit-word-decay: true
        broken-case-semantics: true
        exit-functions-only: true
        refinements-blank: true
        no-switch-evals: true
        no-switch-fallthrough: true
        forever-64-bit-ints: true
        print-forms-everything: true
        break-with-overrides: true
        none-instead-of-voids: true
        arg1-arg2-arg3-error: true
        dont-exit-natives: true
        paren-instead-of-group: true
        get-will-get-anything: true
        no-reduce-nested-print: true
        unlocked-source: true
    ]

    append system/contexts/user compose [

        ; UNSET! as a reified type does not exist in Ren-C.  There is still
        ; a "void" state as the result of `do []` or just `()`, and it can be
        ; passed around transitionally.  Yet this "meta" result cannot be
        ; stored in blocks.
        ;
        ; Over the longer term, UNSET? should be something that takes a word
        ; or path to tell whether a variable is unset... but that is reserved
        ; for NOT SET? until legacy is adapted.
        ;
        unset?: (:void?)

        ; Result from TYPE-OF () is a NONE!, so this should allow one to write
        ; `unset! = type-of ()`.  Also, a NONE! value in a typeset spec is
        ; used to indicate a willingness to tolerate optional arguments, so
        ; `foo: func [x [unset! integer!] x][...]` should work in legacy mode
        ; for making an optional x argument.
        ;
        ; Note that with this definition, `datatype? unset!` will fail.
        ;
        unset!: _

        ; NONE is reserved for NONE-OF in the future
        ;
        none: (:blank)
        none!: (:blank!)
        none?: (:blank?)

        ; The bizarre VALUE? function would look up words, return TRUE if they
        ; were set and FALSE if not.  All other values it returned TRUE.  The
        ; parameter was not optional, so you couldn't say `value?`.
        ;
        value?: (func [
            {If a word, return whether word is set...otherwise TRUE}
            value
        ][
            either any-word? :value [set? value] [true]
        ])

        ; Note that TYPE?/WORD is less necessary since SWITCH can soft quote
        ; https://trello.com/c/fjJb3eR2
        ;
        type?: (function [
            "Returns the datatype of a value <r3-legacy>."
            value [<opt> any-value!]
            /word
        ][
            case [
                not word [type-of :value]

                not set? 'value [
                    quote unset! ;-- https://trello.com/c/rmsTJueg
                ]

                blank? :value [
                    quote none! ;-- https://trello.com/c/vJTaG3w5
                ]

                all [
                    group? :value
                    system/options/paren-instead-of-group
                ][
                    quote paren! ;-- https://trello.com/c/ANlT44nH
                ]
            ] else [
                to-word type-of :value
            ]
        ])

        found?: (func [
            "Returns TRUE if value is not NONE."
            value
        ][
            not blank? :value
        ])

        ; These words do NOT inherit the infixed-ness, and you simply cannot
        ; set things infix through a plain set-word.  We have to do this
        ; after the words are appended to the object.

        and: _

        or: _

        xor: _

        apply: (:r3-alpha-apply)

        ; Adapt the TO ANY-WORD! case for GROUP! to give back the
        ; word PAREN! (not the word GROUP!)
        ;
        to: (adapt 'to [
            if all [
                :value = group!
                system/options/paren-instead-of-group
                find any-word! type
            ][
                value: "paren!" ;-- twist it into a string conversion
            ]
        ])

        ; Not contentious, but trying to excise this ASAP
        funct: (:function)

        op?: (func [
            "OP? <r3-legacy> behavior which just always returns FALSE"
            value [<opt> any-value!]
        ][
            false
        ])

        ; R3-Alpha and Rebol2's DO was effectively variadic.  If you gave it
        ; a function, it could "reach out" to grab arguments from after the
        ; call.  While Ren-C permits this in variadic functions, the system
        ; functions should be "well behaved" and there will even likely be
        ; a security setting to turn variadics off (system-wide or per module)
        ;
        ; https://trello.com/c/YMAb89dv
        ;
        ; This legacy bridge is variadic to achieve the result.
        ;
        do: (function [
            {Evaluates a block of source code (variadic <r3-legacy> bridge)}

            return: [<opt> any-value!]
            source [<opt> blank! block! group! string! binary! url! file! tag!
                error! function!
            ]
            normals [any-value! <...>]
                {Normal variadic parameters if function (<r3-legacy> only)}
            'softs [any-value! <...>]
                {Soft-quote variadic parameters if function (<r3-legacy> only)}
            :hards [any-value! <...>]
                {Hard-quote variadic parameters if function (<r3-legacy> only)}
            /args
                {If value is a script, this will set its system/script/args}
            arg
                "Args passed to a script (normally a string)"
            /next
                {Do next expression only, return it, update block variable}
            var [word! blank!]
                "Variable updated with new block position"
        ][
            next_DO: next
            next: :lib/next

            either function? :source [
                code: reduce [:source]
                params: words-of :source
                for-next params [
                    append code switch type-of params/1 [
                        :word! [take normals]
                        :lit-word! [take softs]
                        :get-word! [take hards]
                        :set-word! [()] ;-- unset appends nothing (for local)
                        :refinement! [break]
                        (fail ["bad param type" params/1])
                    ]
                ]
                lib/do code
            ][
                apply :lib/do [
                    source: :source
                    if args: args [
                        arg: :arg
                    ]
                    if next: next_DO [
                        var: :var
                    ]
                ]
            ]
        ])

        ; Ren-C's default is a "lookback" that can see the SET-WORD! to its
        ; left and examine it.  `x: default 10` instead of `default 'x 10`,
        ; with the same effect.
        ;
        default: (func [
            "Set a word to a default value if it hasn't been set yet."
            'word [word! set-word! lit-word!]
                "The word (use :var for word! values)"
            value "The value" ; void not allowed on purpose
        ][
            unless all [set? word | not blank? get word] [set word :value] :value
        ])

        ; Ren-C removed the "simple parse" functionality, which has been
        ; superseded by SPLIT.  For the legacy parse implementation, add
        ; it back in (more or less) by delegating to split.
        ;
        ; Also, as an experiment Ren-C has been changed so that a successful
        ; parse returns the input, while an unsuccessful one returns blank.
        ; Historically PARSE returned LOGIC!, this restores that behavior.
        ;
        parse: (function [
            "Parses a string or block series according to grammar rules."

            input [any-series!]
                "Input series to parse"
            rules [block! string! blank!]
                "Rules (string! is <r3-legacy>, use SPLIT)"
            /case
                "Uses case-sensitive comparison"
            /all
                "Ignored refinement for <r3-legacy>"
        ][
            case_PARSE: case
            case: :lib/case

            comment [all_PARSE: all] ;-- Not used
            all: :lib/all

            case [
                blank? rules [
                    split input charset reduce [tab space cr lf]
                ]

                string? rules [
                    split input to-bitset rules
                ]
            ] else [
                lib/parse/(all [case_PARSE 'case]) input rules
            ]
        ])

        ; There is a feature in R3-Alpha, used by R3-GUI, which allows an
        ; unusual syntax for capturing series positions (like a REPEAT or
        ; FORALL) with a SET-WORD! in the loop words block:
        ;
        ;     >> a: [1 2 3]
        ;     >> foreach [s: i] a [print ["s:" mold s "i:" i]]
        ;
        ;     s: [1 2 3] i: 1
        ;     s: [2 3] i: 2
        ;     s: [3] i: 3
        ;
        ; This feature was removed from Ren-C due to it not deemed to be
        ; "Quality", adding semantic questions and complexity to the C loop
        ; implementation.  (e.g. `foreach [a:] [...] [print "infinite loop"]`)
        ; That interferes with the goal of "modify with confidence" and
        ; simplicity.
        ;
        ; This shim function implements the behavior in userspace.  Should it
        ; arise that MAP-EACH is used similarly in a legacy scenario then the
        ; code could be factored and shared, but it is not likely that the
        ; core construct will be supporting this in FOR-EACH or EVERY.
        ;
        ; Longer-term, a rich LOOP dialect like Lisp's is planned:
        ;
        ;    http://www.gigamonkeys.com/book/loop-for-black-belts.html
        ;
        foreach: (function [
            "Evaluates a block for value(s) in a series w/<r3-legacy> 'extra'."

            'vars [word! block!]
                "Word or block of words to set each time (local)"
            data [any-series! any-context! map! blank!]
                "The series to traverse"
            body [block!]
                "Block to evaluate each time"
        ][
            if any [
                not block? vars
                lib/for-each item vars [
                    if set-word? item [break/with false]
                    true
                ]
            ][
                ; a normal FOREACH
                return lib/for-each :vars data body
            ]

            ; Otherwise it's a weird FOREACH.  So handle a block containing at
            ; least one set-word by doing a transformation of the code into
            ; a while loop.
            ;
            use :vars [
                position: data
                until [tail? position] compose [
                    (collect [
                        every item vars [
                            case [
                                set-word? item [
                                    keep compose [(item) position]
                                ]
                                word? item [
                                    keep compose [
                                        (to-set-word :item) position/1
                                        position: next position
                                    ]
                                ]
                            ] else [
                                fail "non SET-WORD?/WORD? in FOREACH vars"
                            ]
                        ]
                    ])
                    (body)
                ]
            ]
        ])

        ; REDUCE has been changed to evaluate single-elements if those
        ; elements do not require arguments (so effectively a more limited
        ; form of EVAL).  The old behavior was to just pass through non-blocks
        ;
        reduce: (function [
            {Evaluates expressions and returns multiple results.}
            value
            /into
                {Output results into a series with no intermediate storage}
            target [any-block!]
        ][
            unless block? :value [return :value]

            apply :lib/reduce [
                | value: :value
                | if into: into [target: :target]
            ]
        ])

        ; because reduce has been changed but lib/reduce is not in legacy
        ; mode, this means the repend and join function semantics are
        ; different.  This snapshots their implementation.

        repend: (function [
            "Appends a reduced value to a series and returns the series head."
            series [series! port! map! gob! object! bitset!]
                {Series at point to insert (modified)}
            value
                {The value to insert}
            /part
                {Limits to a given length or position}
            length [number! series! pair!]
            /only
                {Inserts a series as a series}
            /dup
                {Duplicates the insert a specified number of times}
            count [number! pair!]
        ][
            ;-- R3-alpha REPEND with block behavior called out
            ;
            apply :append [
                | series: series
                | value: either block? :value [reduce :value] [value]
                | if part: part [limit: length]
                | only: only
                | if dup: dup [count: count]
            ]
        ])

        join: (function [
            "Concatenates values."
            value "Base value"
            rest "Value or block of values"
        ][
            ;-- double-inline of R3-alpha `repend value :rest`
            ;
            apply :append [
                | series: either series? :value [copy value] [form :value]
                | value: either block? :rest [reduce :rest] [rest]
            ]
        ])

        ??: (:dump)

        ; To be on the safe side, the PRINT in the box won't do evaluations on
        ; blocks unless the literal argument itself is a block
        ;
        print: (specialize 'print [eval: true])

        ; QUIT now takes /WITH instead of /RETURN
        ;
        quit: (function [
            {Stop evaluating and return control to command shell or calling script.}

            /with
                {Yield a result (mapped to an integer if given to shell)}
            value [any-value!]
                "See: http://en.wikipedia.org/wiki/Exit_status"
            /return
                "(deprecated synonym for /WITH)"
            return-value
        ][
            apply 'lib/quit [
                with: any? [with | return]
                value: case [with [value] return [return-value]]
            ]
        ])

        ; R3-Alpha would tolerate blocks in the first position, which were
        ; a feature in Rebol2.  e.g. `func [[throw catch] x y][...]`.  Ren-C
        ; does not allow this.  Also, policy requires a RETURN: annotation to
        ; say if one returns functions or void in Ren-C--there was no such
        ; requirement in R3-Alpha.
        ;
        ; Also, ANY-TYPE! must be expressed as <OPT> ANY-VALUE! in Ren-C,
        ; since typesets cannot contain no-type.
        ;
        func: (func [
            {FUNC <r3-legacy>}
            return: [function!]
            spec [block!]
            body [block!]
        ][
            if block? first spec [spec: next spec]

            if find spec [[any-type!]] [
                spec: copy/deep spec
                replace/all spec [[any-type!]] [[<opt> any-value!]] 
            ]

            lib/func compose [
                return: [<opt> any-value!]
                (spec)
            ] body
        ])

        ; The shift in Ren-C is to remove the refinements from FUNCTION.
        ; Previously /WITH is now handles as the tag <in>
        ; /EXTERN then takes over the tag <with>
        ;
        function: (func [
            {FUNCTION <r3-legacy>}
            return: [function!]
            spec [block!]
            body [block!]
            /with
                {Define or use a persistent object (self)}
            object [object! block! map!]
                {The object or spec}
            /extern
                {Provide explicit list of external words}
            words [block!]
                {These words are not local.}
        ][
            if block? first spec [spec: next spec]

            if find spec [[any-type!]] [
                spec: copy/deep spec
                replace/all spec [[any-type!]] [[<opt> any-value!]] 
            ]

            if block? :object [object: has object]

            lib/function compose [
                return: [<opt> any-value!]
                (spec)
                (if with [reduce [<in> object]])
                (if extern [<with>])
                (:words)
            ] body
        ])

        ; In Ren-C, HAS is the arity-1 parallel to OBJECT as arity-2 (similar
        ; to the relationship between DOES and FUNCTION).  In Rebol2 and
        ; R3-Alpha it just broke out locals into their own block when they
        ; had no arguments.
        ;
        has: (func [
            {Shortcut for function with local variables but no arguments.}
            return: [function!]
            vars [block!] {List of words that are local to the function}
            body [block!] {The body block of the function}
        ][
            func (head insert copy vars /local) body
        ])

        ; CONSTRUCT is now the generalized arity-2 object constructor.  What
        ; was previously known as CONSTRUCT can be achieved with the /ONLY
        ; parameter to CONSTRUCT or to HAS.
        ;
        ; !!! There's was code inside of Rebol which called "Scan_Net_Header()"
        ; in order to produce a block out of a STRING! or a BINARY! here.
        ; That has been moved to scan-net-header, and there was not presumably
        ; other code that used the feature.
        ;
        construct: (func [
            "Creates an object with scant (safe) evaluation."

            spec [block!]
                "Specification (modified)"
            /with
                "Create from a default object"
            object [object!]
                "Default object"
            /only
                "Values are kept as-is"
        ][
            apply :lib/construct [
                | spec: either with [object] [[]]
                | body: spec

                ; It may be necessary to do *some* evaluation here, because
                ; things like loading module headers would tolerate [x: 'foo]
                ; as well as [x: foo] for some fields.
                ;
                | only: true
            ]
        ])

        ; There were several different strata of equality checks, and one was
        ; EQUIV? as well as NOT-EQUIV?.  With changes to make comparisons
        ; inside the system indifferent to binding (unless SAME? is used),
        ; these have been shaken up instead focusing on getting more
        ; foundational comparisons working.  Red does not have EQUIV?, for
        ; example, and few could tell you what it was.
        ;
        ; These aren't the same but may work in some cases.  :-/
        ;
        equiv?: (:equal?)
        not-equiv?: (:not-equal?)

        ; BREAK/RETURN had a lousy name to start with (return from what?), but
        ; was axed to give loops a better interface contract:
        ;
        ; https://trello.com/c/uPiz2jLL/
        ;
        ; New features of WITH: https://trello.com/c/cOgdiOAD
        ;
        lib-break: :break ; overwriting lib/break for now
        break: (func [
            {Exit the current iteration of a loop and stop iterating further.}

            /return ;-- Overrides RETURN!
                {(deprecated: use THROW+CATCH)}
            value [any-value!]
        ][
            if return [
                fail [
                    "BREAK/RETURN temporarily not implemented in <r3-legacy>"
                    "see https://trello.com/c/uPiz2jLL/ for why it was"
                    "removed.  It could be accomplished in the compatibility"
                    "layer by climbing the stack via the DEBUG API and"
                    "looking for loops to EXIT, but this will all change with"
                    "the definitional BREAK and CONTINUE so it seems not"
                    "worth it.  Use THROW and CATCH instead (available in"
                    "R3-Alpha) to subvert the loop return value."
                ]
            ]

            lib-break
        ])
    ]

    ; set-infix on PATH! instead of WORD! is still TBD
    ;
    set-infix (bind 'and system/contexts/user) :and*
    set-infix (bind 'or system/contexts/user) :or+
    set-infix (bind 'xor system/contexts/user) :xor+

    if-flags: func [flags [block!] body [block!]] [
        for-each flag flags [
            if system/options/(flag) [return do body]
        ]
    ]

    ;
    ; The Ren-C invariant for control constructs that don't run their cases
    ; is to return VOID, not a "NONE!" (BLANK!) as in R3-Alpha.  We assume
    ; that converting void results from these operations gives compatibility,
    ; and if it doesn't it's likealy a bigger problem because you can't put
    ; "unset! literals" (voids) into blocks in the first place.
    ;
    ; So make a lot of things like `first: (chain [:first :to-value])`
    ;
    if-flags [none-instead-of-voids] [
        for-each word [
            first second third fourth fifth sixth seventh eighth ninth tenth
            select pick
            if unless either case
            while foreach loop repeat forall forskip
        ][
            append system/contexts/user compose [
                (to-set-word word)
                (chain compose [(to-get-word word) :to-value])
            ]
        ]
    ]

    ; SWITCH had several behavior changes--it evaluates GROUP! and GET-WORD!
    ; and GET-PATH!--and values "fall out" the bottom if there isn't a match
    ; (and the last item isn't a block).
    ;
    ; We'll assume these cases are rare in <r3-legacy> porting, but swap in
    ; SWITCH for a routine that will FAIL if the cases come up.  A sufficiently
    ; motivated individual could then make a compatibility construct, but
    ; probably would rather just change it so their code runs faster.  :-/
    ;
    if-flags [no-switch-evals no-switch-fallthrough][
    append system/contexts/user compose [
        switch: (
            chain [
                adapt 'switch [use [last-was-block] [
                    last-was-block: false
                    for-next cases [
                        if maybe [get-word! get-path! group!] cases/1 [
                            fail [{SWITCH non-<r3-legacy> evaluates} (cases/1)]
                        ]
                        if block? cases/1 [
                            last-was-block: true
                        ]
                    ]
                    unless last-was-block [
                        fail [{SWITCH non-<r3-legacy>} last cases {"fallout"}]
                    ]
                ]]
            |
                :to-value
            ]
        )
    ]]

    r3-legacy-mode: on
    return blank
]
