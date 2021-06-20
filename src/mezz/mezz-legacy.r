REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Legacy compatibility"
    Homepage: https://trello.com/b/l385BE7a/porting-guide
    Rights: {
        Copyright 2012-2018 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        These are a few compatibility scraps left over from extracting the
        R3-Alpha emulation layer into %redbol.reb.
    }
]


; LIT now has new meanings, but is used some places (like <json>) until the
; "THE" change has propagated completely.
; https://forum.rebol.info/t/just-vs-lit-literal-literally/1453
;
lit: :the


while: func [] [
    fail ^return [
        "The previous functionality of WHILE is now done by LOOP."
        "Ultimately, WHILE will be arity-1 to help match the behavior in"
        "PARSE, and better parallel UNTIL."
    ]
]


; !!! Compatibility for VOID!, remove as time goes on.  BAD-WORD! has come
; to stay.
;
void!: bad-word!
void?: :bad-word?


; !!! As a first step of removing the need for APPEND/ONLY (and friends), this
; moves the behavior into mezzanine code for testing.

onlify: func [
    {Add /ONLY behavior to APPEND, INSERT, CHANGE}
    action [action!]
    /param [word!]
][
    param: default ['value]
    adapt (
        augment :action [/only "DEPRECATED: Use QUOTE, JUST, [] or ^^ instead"]
    ) compose/deep [
        all [only, any-array? series] then [
            (param): ^(param)
        ]
        ; ...fall through to normal handling
    ]
]

append: my onlify
insert: my onlify
change: my onlify
find: my onlify/param 'pattern


; See notes on the future where FUNC and FUNCTION are synonyms (same will be
; true of METH and METHOD:
;
; https://forum.rebol.info/t/rethinking-auto-gathered-set-word-locals/1150
;
method: func [/dummy] [
    fail ^dummy [
        {The distinction between FUNC vs. FUNCTION, and METH vs. METHOD was}
        {the gathering of SET-WORD! as locals.  This behavior led to many}
        {problems with gathering irrelevant locals in the frame (e.g. any}
        {object fields for MAKE OBJECT! [KEY: ...]), and also made it hard}
        {to abstract functions.  With virtual binding, there is now LET...}
        {which has some runtime cost but is much more versatile.  If you}
        {don't want to pay the cost then use <local> in the spec.}
    ]
]


REBOL: function [] [
    fail ^return [
        "The REBOL [] header of a script must be interpreted by LOAD (and"
        "functions like DO).  It cannot be executed directly."
    ]
]

eval: function [] [
    fail ^return [
        "EVAL is now REEVAL (EVAL is slated to be a synonym for EVALUATE):"
        https://forum.rebol.info/t/eval-evaluate-and-reeval-reevaluate/1173
    ]
]


; The pattern `foo: enfix function [...] [...]` is probably more common than
; enfixing an existing function, e.g. `foo: enfix :add`.  Hence making a
; COPY of the ACTION! identity is probably a waste.  It may be better to go
; with mutability-by-default, so `foo: enfix copy :add` would avoid the
; mutation.  However, it could also be that the function spec dialect gets
; a means to specify enfixedness.  See:
;
; https://forum.rebol.info/t/moving-enfixedness-back-into-the-action/1156
;
enfix: :enfixed


; INPUT is deprecated--but making sure ASK TEXT! works for its purpose first
; https://forum.rebol.info/t/1124
;
input: does [ask text!]


=== EXTENSION DATATYPE DEFINITIONS ===

; Even though Ren-C does not build in things like IMAGE! or GOB! to the core,
; there's a mechanical issue about the words for the datatypes being defined
; so that the extension can load.  This means at least some kind of stub type
; has to be available when the specs are being processed, else they could not
; mention the types.  The extension mechanism should account for this with
; some kind of "preload" script material (right now it only has a "postload"
; hook to run a script).  Until then, define here.  (Note these types won't
; be usable for anything but identity comparisons until the extension loads.)

image!: make datatype! http://datatypes.rebol.info/image
image?: typechecker image!
vector!: make datatype! http://datatypes.rebol.info/vector
vector?: typechecker vector!
gob!: make datatype! http://datatypes.rebol.info/gob
gob?: typechecker gob!
struct!: make datatype! http://datatypes.rebol.info/struct
struct?: typechecker struct!

; LIBRARY! is a bit different, because it may not be feasible to register it
; in an extension, because it's used to load extensions from DLLs.  But it
; doesn't really need all 3 cell fields, so it gives up being in the base
; types list for types that need it.
;
library!: make datatype! http://datatypes.rebol.info/library
library?: typechecker library!


; CONSTRUCT is a "verb-ish" word slated to replace the "noun-ish" CONTEXT:
;
; http://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
;
; Note: Historically OBJECT was essentially a synonym for CONTEXT with the
; ability to tolerate a spec of `[a:]` by transforming it to `[a: none].
; Ren-C hasn't decided yet, but will likely support `construct [a: b: c:]`
;
context: specialize :make [type: object!]


uneval: func [] [
    fail ^return "QUOTE has replaced UNEVAL"
]

=>: func [] [
    fail ^return "=> for lambda has been replaced by ->"
]

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
length-of: specialize :reflect [property: 'length]
words-of: specialize :reflect [property: 'words]
values-of: specialize :reflect [property: 'values]
index-of: specialize :reflect [property: 'index]
type-of: specialize :reflect [property: 'type]
binding-of: specialize :reflect [property: 'binding]
head-of: specialize :reflect [property: 'head]
tail-of: specialize :reflect [property: 'tail]
file-of: specialize :reflect [property: 'file]
line-of: specialize :reflect [property: 'line]
body-of: specialize :reflect [property: 'body]


; General renamings away from non-LOGIC!-ending-in-?-functions
; https://trello.com/c/DVXmdtIb
;
index?: specialize :reflect [property: 'index]
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


; The legacy PRIN construct is replaced by WRITE-STDOUT SPACED and similar
;
prin: function [
    "Print without implicit line break, blocks are SPACED."

    return: <none>
    value [<opt> any-value!]
][
    write-stdout switch type of :value [
        null [return]
        text! char! [value]
        block! [spaced value]
    ] else [
        form :value
    ]
]


join-of: func [] [
    fail ^return [
        "JOIN has returned to Rebol2 semantics, JOIN-OF is no longer needed"
        https://forum.rebol.info/t/its-time-to-join-together/1030
    ]
]


; REJOIN in R3-Alpha meant "reduce and join".  JOIN-ALL is a friendlier
; name, suggesting the join result is the type of the first reduced element.
;
; But JOIN-ALL doesn't act exactly the same as REJOIN--in fact, most cases
; of REJOIN should be replaced not with JOIN-ALL, but with UNSPACED.  Note
; that although UNSPACED always returns a TEXT!, the AS operator allows
; aliasing to other string types (`as tag! unspaced [...]` will not create a
; copy of the series data the way TO TAG! would).
;
rejoin: func [
    {Reduces and joins a block of values}

    return: "Same type as first non-null item produced by evaluation"
        [issue! any-series! any-sequence!]
    block "Values to reduce and join together"
        [block!]
    <local> base
][
    cycle [  ; Keep evaluating until a usable BASE is found

        if not block: evaluate/result block 'base [
            return copy []  ; we exhausted block without finding a base value
        ]

        any [
            quoted? block  ; just ran an invisible like COMMENT or ELIDE
            null? :base  ; consider to have dissolved
            blank? :base  ; treat same as NULL
        ] then [
            continue  ; do another evaluation step
        ]

        ; !!! Historical Rebol would default to a TEXT! if the first thing
        ; found wasn't JOIN-able.  This is questionable.
        ;
        if not match [issue! any-sequence! any-series!] :base [
            base: to text! :base
        ]

        return join base block  ; JOIN with what's left of the block
    ]
]


; The name FOREVER likely dissuades its use, since many loops aren't intended
; to run forever.  CYCLE gives similar behavior without suggesting the
; permanence.  It also is unique among loop constructs by supporting a value
; return via STOP, since it has no "normal" loop termination condition.
;
forever: :cycle


apply: func [.dummy] [
    fail ^dummy [
        {APPLY is being reverted to a reimagination of the positional}
        {APPLY from Rebol2/R3-Alpha, but with a different way of dealing with}
        {refinements.  The Ren-C APPLY experiment has been moved to the name}
        {APPLIQUE, which runs a block of code that is bound into a frame.}
        {APPLY will be reverted when all Ren-C style apply are switched to}
        {use APPLIQUE.}  https://forum.rebol.info/t/1103
    ]
]


hijack :find adapt copy :find [
    if reverse or (last) [
        fail ^reverse [
            {/REVERSE and /LAST on FIND have been deprecated.  Use FIND-LAST}
            {or FIND-REVERSE specializations: https://forum.rebol.info/t/1126}
        ]
    ]
]


to-integer: func [
    {Deprecated overload: see https://forum.rebol.info/t/1270}

    value [
       integer! decimal! percent! money! char! time!
       issue! binary! any-string!
    ]
    /unsigned "For BINARY! interpret as unsigned, otherwise error if signed."
][
    either binary? value [
        debin [be (either unsigned ['+] ['+/-])] value
    ][
        to integer! value  ; could check for error, but deprecated
    ]
]
