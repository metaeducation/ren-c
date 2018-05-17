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
        so that it operates more like R3-Alpha.  It was used for efforts in
        porting, as well as to test the flexiblity of the system to "bend"
        back to old semantics, using usermode constructs.  The R3-Alpha
        porting purpose is mostly done, and so this code would ultimately be
        targeting compatibility with Rebol2/Red.

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


; To be more visually pleasing, properties like LENGTH can be extracted using
; a reflector as simply `length of series`, with no hyphenation.  This is
; because OF quotes the word on the left, and passes it to REFLECT.
;
; There are bootstrap reasons to keep versions like WORDS-OF alive.  Though
; WORDS OF syntax could be faked in R3-Alpha (by making WORDS a function that
; quotes the OF and throws it away, then runs the reflector on the second
; argument), that faking would preclude naming variables "words".
;
; Beyond the bootstrap, there could be other reasons to have hyphenated
; versions.  It could be that performance-critical code would want faster
; processing (a TYPE-OF specialization is slightly faster than TYPE OF, and
; a TYPE-OF native written specifically for the purpose would be even faster).
;
; Also, HELP isn't designed to "see into" reflectors, to get a list of them
; or what they do.  (This problem parallels others like not being able to
; type HELP PARSE and get documentation of the parse dialect...there's no
; link between HELP OF and all the things you could ask about.)  There's also
; no information about specific return types, which could be given here
; with REDESCRIBE.
;
length-of: specialize 'reflect [property: 'length]
words-of: specialize 'reflect [property: 'words]
values-of: specialize 'reflect [property: 'values]
index-of: specialize 'reflect [property: 'index]
type-of: specialize 'reflect [property: 'type]
context-of: specialize 'reflect [property: 'context]
head-of: specialize 'reflect [property: 'head]
tail-of: specialize 'reflect [property: 'tail]
file-of: specialize 'reflect [property: 'file]
line-of: specialize 'reflect [property: 'line]
body-of: specialize 'reflect [property: 'body]


; General renamings away from non-LOGIC!-ending-in-?-functions
; https://trello.com/c/DVXmdtIb
;
index?: specialize 'reflect [property: 'index]
offset?: :offset-of
sign?: :sign-of
suffix?: :suffix-of
length?: :length-of
head: :head-of
tail: :tail-of

comment [
    ; !!! Less common cases still linger as question mark routines that
    ; don't return LOGIC!, and they seem like they need greater rethinking in
    ; general. What replaces them (for ones that are kept) might be new.
    ;
    encoding?: _
    file-type?: _
    speed?: _
    info?: _
    exists?: _
]


; FOREACH isn't being taken for anything else, may stay a built-in synonym
; https://trello.com/c/cxvHGNha
;
foreach: :for-each


; FOR-NEXT lets you switch series (unlike FORALL), see also FOR-BACK
; https://trello.com/c/StCADPIB
;
forall: :for-next
forskip: :for-skip


; BIND? and BOUND? didn't fit the naming convention of returning LOGIC! if
; they end in a question mark.  Also, CONTEXT-OF is more explicit about the
; type of the return result, which makes it more useful than BINDING-OF or
; BIND-OF as a name.  (Result can be an ANY-CONTEXT!, including FRAME!)
;
bound?: bind?: specialize 'reflect [property: 'context]


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
        {You can test with NULL? (), but the TYPE-OF () is a NONE! *value*}
        {So NONE? TYPE-OF () will be TRUE.}
    ] 'dummy
]

true?: func [dummy:] [
    fail/where [
        {Historical TRUE? is ambiguous, use either TO-LOGIC or `= TRUE`} LF
        {(experimental alternative of DID as "anti-NOT" is also offered)}
    ] 'dummy
]

false?: func [dummy:] [
    fail/where [
        {Historical FALSE? is ambiguous, use either NOT or `= FALSE`}
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
        {TYPE OF is the current replacement, with no TYPE-OF/WORD}
        {Use soft quotes, e.g. SWITCH TYPE OF 1 [:INTEGER! [...]]}
        {If running in <r3-legacy> mode, old TYPE? meaning is available.}
    ] 'dummy
]

found?: func [dummy:] [
    fail/where [
        {FOUND? is deprecated, use DID (e.g. DID FIND)}
        {See: https://trello.com/c/Cz0qs5d7}
    ] 'dummy
]

op?: func [dummy:] [
    fail/where [
        {OP? can't work in Ren-C because there are no "infix ACTION!s"}
        {"infixness" is a property of a word binding, made via SET/ENFIX}
        {See: https://trello.com/c/mfqTGmcv}
    ] 'dummy
]

hijack 'also adapt copy :also [
    if (block? :branch) and (not semiquoted? 'branch) [
        fail/where [
            {ALSO serves a different and important purpose in Ren-C as a}
            {complement to ELSE, so use ELIDE for old-ALSO-like tasks.}
            {See: https://trello.com/c/Y03HJTY4}
        ] 'branch
    ]
    ;-- fall through to normal ALSO implementation
]

compress: decompress: func [dummy:] [
    fail/where [
        {COMPRESS and DECOMPRESS are deprecated in Ren-C, in favor of the}
        {DEFLATE/INFLATE natives and GZIP/GUNZIP natives.}
        {See: https://trello.com/c/Bl6Znz0T}
    ] 'dummy
]

clos: closure: func [dummy:] [
    fail/where [
        {All ACTION!s (such as made with FUNC, FUNCTION, PROC, PROCEDURE)}
        {have "specific binding", so closure is not needed for that.  The}
        {indefinite survival of args is on the back burner for Ren-C.}
        {See: https://forum.rebol.info/t/234}
    ] 'dummy
]

exit: func [dummy:] [
    fail/where [
        {EXIT as an arity-1 form of RETURN was replaced in *definitional*}
        {returns by LEAVE, and is only available in PROC and PROCEDURE.}
        {See: https://trello.com/c/TXqLos1q}
    ] 'dummy
]

hijack 'try adapt copy :try [
    ;
    ; Most historical usages of TRY took literal blocks as arguments.  This
    ; is a good way of catching them, while allowing new usages.
    ;
    if block? :optional and (semiquoted? 'optional) [
        fail/where [
            {TRY/EXCEPT was replaced by TRAP/WITH, which matches CATCH/WITH}
            {and is more coherent.  See: https://trello.com/c/IbnfBaLI}
            {TRY now converts voids to blanks, passing through ANY-VALUE!}
        ] 'optional
    ]
    ;-- fall through to native TRY implementation
]


unless: enfix function [ ;-- error-checking shim, overrides real UNLESS
    {Returns left hand side, unless the right hand side is something}

    return: [any-value!]
    left [<end> any-value!]
    right [<opt> any-value! <...>]
    :look [any-value! <...>]
    /try {Consider right being BLANK! a value to override the left}
][
    any [
        unset? 'left
        elide (right: take* right)
        block? first look
    ] then [
        fail/where [
            "UNLESS has been repurposed in Ren-C as an enfix operator"
            "which defaults to the left hand side, unless the right"
            "side has a value which overrides it.  You may use IF-NOT"
            "as a replacement, or even define UNLESS: :LIB/IF-NOT,"
            "though actions like OR, DEFAULT, etc. are usually better"
            "replacements for the intents that UNLESS was used for."
            "!!! NOTE: `if not` as two words isn't the same in Ren-C,"
            "as `if not x = y` is read as `if (not x) = y` since `=`"
            "completes its left hand side.  Be careful rewriting."
        ] 'look
    ]

    either-test (try ?? :value? !! :something?) :right [:left]
]


unless*: enfix redescribe [ ;-- error-checking shim, overrides real UNLESS*
    {Same as UNLESS/TRY (right hand side being BLANK! overrides the left)}
](
    specialize 'unless [try: true]
)


; The legacy PRIN construct is replaced by WRITE-STDOUT SPACED and similar
;
prin: procedure [
    "Print without implicit line break, blocks are SPACED."

    value [<opt> any-value!]
][
    write-stdout <- switch type of :value [
        null [leave] ;-- !!! outputs nothing, should it be an error?
        text! [value]
        char! [value]
        block! [spaced value]
    ] else [
        form :value
    ]
]


to-rebol-file: func [dummy:] [
    fail/where [
        {TO-REBOL-FILE is now LOCAL-TO-FILE} LF
        {Take note it only accepts STRING! input and returns FILE!} LF
        {(unless you use LOCAL-TO-FILE*, which is a no-op on FILE!)}
    ] 'dummy
]

to-local-file: func [dummy:] [
    fail/where [
        {TO-LOCAL-FILE is now FILE-TO-LOCAL} LF
        {Take note it only accepts FILE! input and returns STRING!} LF
        {(unless you use FILE-TO-LOCAL*, which is a no-op on STRING!)}
    ] 'dummy
]

ajoin: func [dummy:] [
    fail/where [
        {AJOIN's functionality is replaced by UNSPACED}
    ] 'dummy
]

reform: func [dummy:] [
    fail/where [
        {REFORM's functionality is replaced by SPACED}
    ] 'dummy
]


; REJOIN in R3-Alpha meant "reduce and join"; the idea of JOIN in Ren-C
; already implies reduction of the appended data.  JOIN-ALL is a friendlier
; name, suggesting the join result is the type of the first reduced element.
;
; But JOIN-ALL doesn't act exactly the same as REJOIN--in fact, most cases
; of REJOIN should be replaced not with JOIN-ALL, but with UNSPACED.  Note
; that although UNSPACED always returns a STRING!, the AS operator allows
; aliasing to other string types (`as tag! unspaced [...]` will not create a
; copy of the series data the way TO TAG! would).
;
rejoin: function [
    "Reduces and joins a block of values."
    return: [any-series!]
        "Will be the type of the first non-void series produced by evaluation"
    block [block!]
        "Values to reduce and join together"
][
    ; An empty block should result in an empty block.
    ;
    if empty? block [return copy []]

    ; Act like REDUCE of expression, but where void does not cause an error.
    ;
    values: copy []
    position: block
    while-not [tail? position][
        value: do/next position 'position
        append/only values :value
    ]

    ; An empty block of values should result in an empty string.
    ;
    if empty? values [append values {}]

    ; Take type of the first element for the result, or default to string.
    ;
    result: either any-series? first values [
        copy first values
    ][
        form first values
    ]
    append result next values
]


; `APPEND/ONLY/DUP A B 2` => `apply :append [a b none none true true 2]` :-/
;
; This implementation of R3-ALPHA-APPLY is a stopgap compatibility measure for
; the positional version.  It shows that such a construct could be written in
; userspace--even implementing the /ONLY refinement.  This is hoped to be a
; "design lab" for figuring out what a better apply might look like.
;
r3-alpha-apply: function [
    {APPLY interface is still evolving, see https://trello.com/c/P2HCcu0V}
    return: [<opt> any-value!]
    action [action!]
    block [block!]
    /only
][
    frame: make frame! :action
    params: words of :action
    using-args: true

    while-not [tail? block] [
        arg: either* only [
            block/1
            elide (block: next block)
        ][
            do/next block 'block
        ]

        either refinement? params/1 [
            using-args: set (in frame params/1) to-logic :arg
        ][
            if using-args [
                set (in frame params/1) :arg
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
        if not tail? block [
            fail "Too many arguments passed in R3-ALPHA-APPLY block."
        ]
    ]

    do frame ;-- voids are optionals
]

; !!! Because APPLY has changed, help warn legacy usages by alerting if the
; first element of the block is not a SET-WORD!.  A BAR! can subvert the
; warning: `apply :foo [| comment {This is a new APPLY} ...]`
;
apply: adapt 'apply [
    if not match [set-word! bar! blank!] first def [
        fail {APPLY takes frame def block (or see r3-alpha-apply)}
    ]
]


; In Ren-C, MAKE for OBJECT! does not use the "type" slot for parent
; objects.  You have to use the arity-2 CONSTRUCT to get that behavior.
; Also, MAKE OBJECT! does not do evaluation--it is a raw creation,
; and requires a format of a spec block and a body block.
;
; Because of the commonality of the alternate interpretation of MAKE, this
; bridges until further notice.
;
lib-make: :make
make: function [
    "Constructs or allocates the specified datatype."
    return: [any-value!]
    type [any-value!]
        "The datatype or an example value"
    def [any-value!]
        "Attributes or size of the new value (modified)"
][
    case [
        all [
            :type = object!
            block? :def and (not block? first def)
        ][
            ;
            ; MAKE OBJECT! [x: ...] vs. MAKE OBJECT! [[spec][body]]
            ; This old style did evaluation.  Must use a generator
            ; for that in Ren-C.
            ;
            return has :def
        ]

        match [object! struct! gob!] :type [
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
    if not datatype? :type [
        type: type of :type
    ]

    if find any-array! :type and (any-array? :def) [
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


; R3-Alpha gave BLANK! when a refinement argument was not provided,
; while Ren-C enforces this as being void (with voids not possible
; to pass to refinement arguments otherwise).  This is some userspace
; code to convert a frame to that policy.
;
blankify-refinement-args: procedure [f [frame!]] [
    seen-refinement: false
    for-each w (words of action-of f) [
        case [
            refinement? w [
                seen-refinement: true
                if not f/(to-word w) [
                    f/(to-word w): _ ;-- turn LOGIC! false to a BLANK!
                ]
            ]
            seen-refinement [ ;-- turn any voids into BLANK!s
                ;
                ; !!! This is better expressed as `: default [_]`, but DEFAULT
                ; is based on using SET, which disallows GROUP!s in PATH!s.
                ; Review rationale and consequences.
                ;
                f/(to-word w): to-value :f/(to-word w)
            ]
        ]
    ]
]


r3-alpha-func: function [
    return: [action!]
    spec [block!]
    body [block!]
][
    ; R3-Alpha would tolerate blocks in the first position, but didn't do the
    ; Rebol2 feature, e.g. `func [[throw catch] x y][...]`.
    ;
    if block? first spec [spec: next spec] ;-- skip Rebol2's [throw]

    ; Also, ANY-TYPE! must be expressed as <OPT> ANY-VALUE! in Ren-C, since
    ; typesets cannot contain no-type.
    ;
    if find spec [[any-type!]] [
        spec: copy spec ;-- deep copy not needed
        replace/all spec [[any-type!]] [[<opt> any-value!]]
    ]

    ; Policy requires a RETURN: annotation to say if one returns functions
    ; or void in Ren-C--there was no such requirement in R3-Alpha.
    ;
    func compose [
        return: [<opt> any-value!]
        (spec)
        <local> exit
    ] compose [
        blankify-refinement-args context of 'return
        exit: make action! [[] [unwind context of 'return]]
        (body)
    ]
]

r3-alpha-function: function [
    return: [action!]
    spec [block!]
    body [block!]
    /with object [object! block! map!]
    /extern words [block!]
][
    if block? first spec [spec: next spec] ;-- See comments in R3-ALPHA-FUNC

    if find spec [[any-type!]] [ ;-- See comments in R3-ALPHA-FUNC
        spec: copy spec ;-- deep copy not needed
        replace/all spec [[any-type!]] [[<opt> any-value!]]
    ]

    if block? :object [object: has object]

    ; The shift in Ren-C is to remove the refinements from FUNCTION, and put
    ; everything into the spec...marked with <tags>
    ;
    function compose [
        return: [<opt> any-value!]
        (spec)
        (with ?? <in>) (:object) ;-- <in> replaces functionality of /WITH
        (extern ?? <with>) (:words) ;-- then <with> took over what /EXTERN was
        ;-- <local> exit, picked up since using FUNCTION as generator
    ] compose [
        blankify-refinement-args context of 'return
        exit: make action! [[] [unwind context of 'return]]
        (body)
    ]
]


; To invoke this function, use `do <r3-legacy>` instead of calling it
; directly, as that will be a no-op in older Rebols.  Notice the word
; is defined in sys-base.r, as it needs to be visible pre-Mezzanine
;
; !!! There are a lot of SET-WORD!s in this routine inside an object append.
; So it's a good case study of how one can get a very large number of
; locals if using FUNCTION.  Study.
;
set 'r3-legacy* func [<local>] [

    if r3-legacy-mode [return blank]

    ; NOTE: these flags only work in debug builds.  A better availability
    ; test for the functionality is needed, as these flags may be expired
    ; at different times on a case-by-case basis.
    ;
    do in system/options [
        forever-64-bit-ints: true
        break-with-overrides: true
        unlocked-source: true
    ]

    append system/contexts/user compose [

        ?: (:help)

        why?: (does [lib/why]) ;-- not exported yet, :why not bound

        ??: (:dump)

        null: (#"^@") ; now NUL https://en.wikipedia.org/wiki/Null_character

        unset?: (:null?) ; https://trello.com/c/shR4v8tS

        ; Result from TYPE OF () is a BLANK!, so this should allow writing
        ; `unset! = type of ()`.  Also, a BLANK! value in a typeset spec is
        ; used to indicate a willingness to tolerate optional arguments, so
        ; `foo: func [x [unset! integer!] x][...]` should work in legacy mode
        ; for making an optional x argument.
        ;
        ; Note that with this definition, `datatype? unset!` will fail.
        ;
        unset!: _

        ; NONE is reserved for `if none [x = 1 | y = 2] [...]`
        ;
        none: (:blank)
        none!: (:blank!)
        none?: (:blank?)

        any-function!: (:action!)
        any-function?: (:action?)

        native!: (:action!)
        native?: (:action?)

        function!: (:action!)
        function?: (:action?)

        ; Some of CLOSURE's functionality was subsumed into all FUNCTIONs, but
        ; the indefinite lifetime of all locals and arguments was not.
        ; https://forum.rebol.info/t/234
        ;
        closure: (:function)
        clos: (:func)

        closure!: (:action!)
        closure?: (:action?)

        true?: (:did?) ;-- better name https://trello.com/c/Cz0qs5d7
        false?: (:not?) ;-- better name https://trello.com/c/Cz0qs5d7

        comment: (func [
            return: [<opt>] {Not invisible: https://trello.com/c/dWQnsspG}
            :discarded [block! any-string! binary! any-scalar!]
        ][
        ])

        value?: (func [
            {See SET? in Ren-C: https://trello.com/c/BlktEl2M}
            value
        ][
            either any-word? :value [set? value] [true] ;; bizarre.  :-/
        ])

        type?: (function [
            value [<opt> any-value!]
            /word {Note: SWITCH soft quotes https://trello.com/c/fjJb3eR2}
        ][
            case [
                not word [type of :value]
                unset? 'value [quote unset!] ;-- https://trello.com/c/rmsTJueg
                blank? :value [quote none!] ;-- https://trello.com/c/vJTaG3w5
                group? :value [quote paren!] ;-- https://trello.com/c/ANlT44nH
            ] else [
                to-word type of :value
            ]
        ])

        found?: (func [
            {See DID and NOT: https://trello.com/c/Cz0qs5d7}
            value
        ][
            not blank? :value
        ])

        ; SET had a refinement called /ANY which doesn't communicate as well
        ; in the Ren-C world as ONLY.  ONLY marks an operation as being
        ; fundamental and not doing "extra" stuff (e.g. APPEND/ONLY is the
        ; lower-level append that "only appends" and doesn't splice blocks).
        ;
        ; Note: R3-Alpha had a /PAD option, which was the inverse of /SOME.
        ; If someone needs it, they can adapt this routine as needed.
        ;
        set: (function [
            {SET does not need /ANY to unset a variable, use <- to check null}
            return: [<opt> any-value!]
            target [blank! any-word! any-path! block! any-context!]
            value [<opt> any-value!]
            /any
            /some
        ][
            set_ANY: any
            any: :lib/any
            set_SOME: some
            some: :lib/some

            if not set_ANY and (null? :value) [
                fail "Legacy SET won't unset a variable without /ANY"
            ]
            apply 'set [
                target: either any-context? target [words of target] [target]
                value: :value
                some: set_SOME
            ]
        ])

        get: (function [
            {Now no OBJECT! support, unset vars always null, use <- to check}
            return: [<opt> any-value!]
            source {Legacy handles Rebol2 types, not *any* type like R3-Alpha}
                [blank! any-word! any-path! any-context! block!]
            /any {/ANY in Ren-C is covered by TRY, only used with BLOCK!}
        ][
            any_GET: any
            any: :lib/any

            if block? :source [
                return source ;-- this is what it did :-/
            ]
            result: either* any-context? source [
                get words of source
            ][
                get source
            ]
            if not any_GET and (null? :result) [
                fail "Legacy GET won't get an unset variable without /ANY"
            ]
            return :result
        ])

        to: (adapt 'to [
            if :value = group! and (find any-word! type) [
                value: "paren!" ;-- make TO WORD! GROUP! give back "paren!"
            ]
        ])

        ; R3-Alpha and Rebol2's DO was effectively variadic.  If you gave it
        ; an action, it could "reach out" to grab arguments from after the
        ; call.  While Ren-C permits this in variadic actions, the system
        ; natives should be "well behaved".
        ;
        ; https://trello.com/c/YMAb89dv
        ;
        ; This legacy bridge is variadic to achieve the result.
        ;
        do: (function [
            return: [<opt> any-value!]
            source [<opt> blank! block! group! text! binary! url! file! tag!
                error! action!
            ]
            normals [any-value! <...>]
            'softs [any-value! <...>]
            :hards [any-value! <...>]
            /args
            arg
            /next
            var [word! blank!]
        ][
            next_DO: next
            next: :lib/next

            if action? :source [
                code: reduce [:source]
                params: words of :source
                for-next params [
                    append code switch type of params/1 [
                        word! [take normals]
                        lit-word! [take softs]
                        get-word! [take hards]
                        set-word! [[]] ;-- empty block appends nothing
                        refinement! [break]
                    ] else [
                        fail ["bad param type" params/1]
                    ]
                ]
                do code
            ] else [
                apply 'do [
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

        try: (func [
            return: [<opt> any-value!]
            block [block!]
            /except {TRAP/WITH is better: https://trello.com/c/IbnfBaLI}
            code [block! action!]
        ][
            trap/(all [except 'with]) block :code
        ])

        default: (func [
            {See the new enfixed DEFAULT: https://trello.com/c/cTCwc5vX}
            'word [word! set-word! lit-word!]
            value
        ][
            if unset? word or (blank? get word) [
                set word :value
            ] else [
                :value
            ]
        ])

        also: (func [
            {Supplanted by ELIDE: https://trello.com/c/pGhk9EbV}
            return: [<opt> any-value!]
            returned [<opt> any-value!]
            discarded [<opt> any-value!]
        ][
            :returned
        ])

        parse: (function [
            {Non-block rules replaced by SPLIT: https://trello.com/c/EiA56IMR}
            input [any-series!]
            rules [block! text! blank!]
            /case
            /all {Ignored refinement in <r3-legacy>}
        ][
            case_PARSE: case
            case: :lib/case

            comment [all_PARSE: all] ;-- Not used
            all: :lib/all

            switch type of rules [
                blank! [split input charset reduce [tab space CR LF]]
                text! [split input to-bitset rules]
            ] else [
                parse/(all [case_PARSE 'case]) input rules
            ]
        ])

        foreach: (function [
            {No SET-WORD! capture, see https://trello.com/c/AXkiWE5Z}
            return: [<opt> any-value!]
            'vars [word! block!]
            data [any-series! any-context! map! blank!]
            body [block!]
        ][
            any [
                not block? vars
                not for-each item vars [if set-word? item [break]]
            ] then [
                return for-each :vars data body ;; normal FOREACH
            ]

            ; Weird FOREACH, transform to WHILE: https://trello.com/c/AXkiWE5Z
            ;
            use :vars [
                position: data
                while-not [tail? position] compose [
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

        reduce: (function [
            value {Not just BLOCK!s evaluated: https://trello.com/c/evTPswH3}
            /into target [any-array!]
        ][
            if not block? :value [return :value]

            apply 'reduce/into [
                value: :value
                target: :target
            ]
        ])

        compose: (function [
            value {Ren-C composes ANY-ARRAY!: https://trello.com/c/8WMgdtMp}
            /deep {Ren-C recurses into PATH!s: https://trello.com/c/8WMgdtMp}
            /only
            /into out [any-array! any-string! binary!]
        ][
            either block? :value [
                apply 'concoct [
                    pattern: quote ()
                    value: :value
                    deep: deep
                    only: only
                    into: into
                    out: :out
                ]
            ][
                :value
            ]
        ])

        ; because reduce has been changed but lib/reduce is not in legacy
        ; mode, this means the repend and join function semantics are
        ; different.  This snapshots their implementation.

        repend: (function [
            series [any-series! port! map! gob! object! bitset!]
            value
            /part limit [any-number! any-series! pair!]
            /only
            /dup count [any-number! pair!]
        ][
            ;-- R3-alpha REPEND with block behavior called out
            ;
            apply 'append/part/dup [
                series: series
                value: block? :value then [reduce :value] !! :value
                limit: :limit
                only: only
                count: :count
            ]
        ])

        join: (function [
            value
            rest
        ][
            ;-- double-inline of R3-alpha `repend value :rest`
            ;
            apply 'append [
                series: if series? :value [copy value] else [form :value]
                value: if block? :rest [reduce :rest] else [rest]
            ]
        ])

        ajoin: (:unspaced)

        reform: (:spaced)

        ; To be on the safe side, the PRINT in the box won't do evaluations on
        ; blocks unless the literal argument itself is a block
        ;
        print: (specialize 'print [eval: true])

        quit: (function [
            /return {use /WITH in Ren-C: https://trello.com/c/3hCNux3z}
            value
        ][
            apply 'quit [
                with: ensure logic! return
                value: :value
            ]
        ])

        func: (:r3-alpha-func)
        function: (:r3-alpha-function)
        apply: (:r3-alpha-apply)

        does: (func [
            return: [action!]
            :code [group! block!]
        ][
            func [<local> return:] compose/only [
                return: does [
                    fail "No RETURN from DOES: https://trello.com/c/KgwJRlyj"
                ]
                | (as group! code)
            ]
        ])

        ; In Ren-C, HAS is the arity-1 parallel to OBJECT as arity-2 (similar
        ; to the relationship between DOES and FUNCTION).  In Rebol2 and
        ; R3-Alpha it just broke out locals into their own block when they
        ; had no arguments.
        ;
        has: (func [
            return: [action!]
            vars [block!]
            body [block!]
        ][
            r3-alpha-func (head of insert copy vars /local) body
        ])

        ; CONSTRUCT is now the generalized arity-2 object constructor.  What
        ; was previously known as CONSTRUCT can be achieved with the /ONLY
        ; parameter to CONSTRUCT or to HAS.

        construct: (func [
            spec [block!]
            /with object [object!]
            /only
        ][
            apply 'construct [
                spec: either with [object] [[]]
                body: spec

                ; It may be necessary to do *some* evaluation here, because
                ; things like loading module headers would tolerate [x: 'foo]
                ; as well as [x: foo] for some fields.
                ;
                only: true
            ]
        ])

        break: (func [
            /return {/RETURN is deprecated: https://trello.com/c/cOgdiOAD}
            value [any-value!]
        ][
            if return [
                fail [
                    "BREAK/RETURN not implemented in <r3-legacy>, see /WITH"
                    "or use THROW+CATCH.  See https://trello.com/c/uPiz2jLL/"
                ]
            ]
            break
        ])

        ++: (function [
            {Deprecated, use ME and MY: https://trello.com/c/8Bmwvwya}
            'word [word!]
        ][
            value: get word ;-- returned value
            elide (set word case [
                any-series? :value [next value]
                integer? :value [value + 1]
            ] else [
                fail "++ only works on ANY-SERIES! or INTEGER!"
            ])
        ])

        --: (function [
            {Deprecated, use ME and MY: https://trello.com/c/8Bmwvwya}
            'word [word!]
        ][
            value: get word ;-- returned value
            elide (set word case [
                any-series? :value [next value]
                integer? :value [value + 1]
            ] else [
                fail "-- only works on ANY-SERIES! or INTEGER!"
            ])
        ])

        compress: (function [
            {Deprecated, use DEFLATE or GZIP: https://trello.com/c/Bl6Znz0T}
            return: [binary!]
            data [binary! text!]
            /part lim
            /gzip
            /only
        ][
            if not any [gzip only] [ ; assume caller wants "Rebol compression"
                data: to-binary copy/part data :lim
                zlib: deflate data

                length-32bit: modulo (length of data) (to-integer power 2 32)
                loop 4 [
                    append zlib modulo (to-integer length-32bit) 256
                    length-32bit: me / 256
                ]
                return zlib ;; ^-- plus size mod 2^32 in big endian
            ]

            return deflate/part/envelope data :lim [
                gzip [assert [not only] 'gzip]
                not only ['zlib]
            ]
        ])

        decompress: (function [
            {Deprecated, use DEFLATE or GUNZIP: https://trello.com/c/Bl6Znz0T}
            return: [binary!]
            data [binary!]
            /part lim
            /gzip
            /limit max
            /only
        ][
            if not any [gzip only] [ ;; assume data is "Rebol compressed"
                lim: default [tail of data]
                return zinflate/part/max data (skip lim -4) :max
            ]

            return inflate/part/max/envelope data :lim :max case [
                gzip [assert [not only] 'gzip]
                not only ['zlib]
            ]
        ])

        switch: (redescribe [
            {Ren-C SWITCH evaluates its branches: }
        ](
            chain [
                adapt 'switch [
                    cases: collect [
                        for-each c cases [
                            keep/only either block? :c [:c] [uneval :c]
                        ]
                    ]
                ]
                    |
                :try ;-- wants blank on failed select, not null
            ]
        ))

        ; The APPEND to the context expects `KEY: VALUE2 KEY2: VALUE2`, which
        ; is why COMPOSE is being used.  `and: (enfix tighten :intersect)`
        ; can't work, because ENFIX needs to quote left and is blocked.
        ;
        and: _
        or: _
        xor: _
    ]

    ; In the object appending model above, can't use ENFIX or SET/ENFIX...
    ;
    system/contexts/user/and: enfix tighten :intersect
    system/contexts/user/or: enfix tighten :union
    system/contexts/user/xor: enfix tighten :difference

    ; The Ren-C invariant for control constructs that don't run their cases
    ; is to return VOID, not a "NONE!" (BLANK!) as in R3-Alpha.  We assume
    ; that converting void results from these operations gives compatibility,
    ; and if it doesn't it's likealy a bigger problem because you can't put
    ; "unset! literals" (voids) into blocks in the first place.
    ;
    ; So make a lot of things like `first: (chain [:first :try])`
    ;
    for-each word [
        if either case
        while for-each loop repeat forall forskip
        select pick
        first second third fourth fifth sixth seventh eighth ninth tenth
    ][
        append system/contexts/user compose [
            (to-set-word word)
            (chain compose [(to-get-word word) :try])
        ]
    ]

    r3-legacy-mode: on
    return blank
]
