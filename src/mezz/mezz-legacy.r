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

; !!! This is temporary due to a failed experiment of making WHILE arity-1 to
; line up with PARSE.  LOOP was used as the arity-2 version for a time.  On
; the bright side, that made PARSE WHILE easier to find and eliminate.  Once
; that elimination is complete, turn the LOOP back to WHILE and reclaim LOOP
; for a generic dialect like Lisp's.
;
loop: :while

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
    fail @dummy [
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
    fail @return [
        "The REBOL [] header of a script must be interpreted by LOAD (and"
        "functions like DO).  It cannot be executed directly."
    ]
]


input: does [
    fail @return [
        "Use ASK TEXT! or READ-LINE vs INPUT (consider using ASK dialect):"
        https://forum.rebol.info/t/1124
    ]
]


repend: func [<local> dummy] [
    fail @dummy [
        "REPEND is just `adapt :append [value: reduce :value]`, but is not"
        "provided in the box.  Note you can say `append data :[1 + 2 3 + 4]`"
        "and the GET-BLOCK! will reduce, or use `append data reduce stuff`"
    ]
]

remold: func [<local> dummy] [
    fail @dummy [
        "REMOLD is just `adapt :mold [value: reduce :value]`, but is not"
        "provided in the box.  Note you can say `mold :[1 + 2 3 + 4]`"
        "and the GET-BLOCK! will reduce, or use `mold reduce stuff`"
    ]
]

rejoin: func [<local> dummy] [
    fail @dummy [
        "REJOIN is replaced in textual sceanarios by UNSPACED, but in more"
        "general cases by the now-non-reducing JOIN, which accepts datatypes"
        "as a first parameter, e.g. `join binary! :[{ABC} 1 + 2 3 + 4]`"
        https://forum.rebol.info/t/rejoin-ugliness-and-the-usefulness-of-tests/
    ]
]


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


; The name FOREVER likely dissuades its use, since many loops aren't intended
; to run forever.  CYCLE gives similar behavior without suggesting the
; permanence.  It also is unique among loop constructs by supporting a value
; return via STOP, since it has no "normal" loop termination condition.
;
forever: :cycle


hijack :find adapt copy :find [
    if reverse or (last) [
        fail @reverse [
            {/REVERSE and /LAST on FIND have been deprecated.  Use FIND-LAST}
            {or FIND-REVERSE specializations: https://forum.rebol.info/t/1126}
        ]
    ]
]
