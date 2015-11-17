REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Legacy compatibility"
    Rights: {
        Copyright 1997-2015 REBOL Technologies
        Copyright 2012-2015 Rebol Open Source Contributors

        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        These definitions turn the clock backward for Rebol code that was
        written prior to Ren/C, e.g. binaries available on rebolsource.net
        or R3-Alpha binaries from rebol.com.  Some flags which are set
        which affect the behavior of natives and the evaluator ARE ONLY
        ENABLED IN DEBUG BUILDS OF REN/C...so be aware of that.

        Some "legacy" definitions (like `foreach` as synonym of `for-each`)
        are kept by default for now, possibly indefinitely.  For other
        changes--such as variations in behavior of natives of the same
        name--you need to add the following to your code:

            do <r3-legacy>

        (Dispatch for this from DO is in the DO* function of sys-base.r)

        This statement will be a NO-OP in older Rebols, since executing a
        tag evaluates to just a tag.  Note that the current trick will
        modify the user context directly, and is not module-based...so
        you really are sort of "backdating" the system globally.  A
        more selective version that turns features on and off one at
        a time to ease porting is needed, perhaps like:

            do/args <r3-legacy> [
                new-do: off
                question-marks: on
            ]

        As always, feedback and improvement welcome.  A porting guide Trello
        has been started at:

            https://trello.com/b/l385BE7a/porting-guide
    }
]

op?: func [
    "Returns TRUE if the argument is an ANY-FUNCTION? and INFIX?"
    value [any-value!]
][
    either any-function? :value [:infix? :value] false
]


; It would be nicer to use type!/type? instead of datatype!/datatype?  :-/
; The compatibility layer may have to struggle with that change down the line
;
; The previous /WORD refinement was common because things like SWITCH
; statements wanted to operate on the word for the datatype, since in
; unevaluated contexts INTEGER! would be a WORD! and not a DATATYPE!.  With
; change to allow lax equality comparisons to see a datatype and its word
; as being equal, this is not as necessary.  Cases that truly need it can
; use TO-WORD TYPE-OF.
;
type?: function [
    "Returns the datatype of a value <r3-legacy>."
    value [any-value!]
    /word "No longer in TYPE-OF, as WORD! and DATATYPE! can be EQUAL?"
][
    either word [
        ; Right now TO-WORD is still returning PAREN! for a PAREN! type,
        ; so the EITHER isn't necessary.  But it's a talking point about
        ; TYPE?/WORD's compatibility story if TO-WORD changed.
        ;
        either (word: to-word type-of :value) = 'group! [paren!] [word]
    ][
        type-of :value
    ]
]


; See also prot-http.r, which has an actor with a LENGTH? "method".  Given
; how actors work, it cannot be overriden here.
length?: :length

index?: :index-of

offset?: :offset-of

sign?: :sign-of


; While `foreach` may have been comfortable for some as a word, its hard
; not to see the word `reach` inside of it.  Once you recognize that it's
; not a word and see it with fresh eyes, it looks bad...and also not part
; of the family of other -each functions like `remove-each` and `map-each`.
; The need for the hyphen for `for-each` isn't that bad, but the hyphen
; does break the rhythm a little bit.  `every` was selected as a
; near-synonym of `for-each` (with a different return result).
;
; Because there is no eager need to retake foreach for any other purpose
; and it doesn't convey any fundamentally incorrect idea, it is low on
; the priority list to eliminate completely.  It may be retained as a
; synonym, and `each` may be considered as well.  The support of synonyms
; is controversial and should be balanced against the value of standards.
;
foreach: :for-each


; The distinctions between Rebol's types is important to articulate.
; So using the term "BLOCK" generically to mean any composite
; series--as well as specificially the bracketed block type--is a
; recipe for confusion.
;
; Importantly: it also makes it difficult to get one's bearings
; in the C sources.  It's hard to find where exactly the bits are in
; play that make something a bracketed block...or if that's what you
; are dealing with at all.  Hence some unique name for the typeclass
; is needed.
;
; The search for a new word for the ANY-BLOCK! superclass went on for
; a long time.  LIST! was looking like it might have to be the winner,
; until ARRAY! was deemed more fitting.  The notion that a "Rebol
; Array" contains "Rebol Values" that can be elements of any type
; (including other arrays) separates it from the other series classes
; which can only contain one type of element (codepoints for strings,
; bytes for binaries, numbers for vector).  In the future those may
; have more unification under a typeclass of their own, but ARRAY!
; is for BLOCK!, PAREN!, PATH!, GET-PATH!, SET-PATH!, and LIT-PATH!

any-block!: :any-array!
any-block?: :any-array?


; By having typesets prefixed with ANY-*, it helps cement the realization
; on the user's part that they are not dealing with a concrete type...so
; even though this is in the function prototype, they will not get back
; that answer from TYPE-OF, nor could they pass it to MAKE or TO.  Because
; typsets cannot be returned in that way, they're easier than most things
; to make synonyms for the old ANY-less variations.

number!: :any-number!
number?: :any-number?

scalar!: :any-scalar!
scalar?: :any-scalar?

series!: :any-series!
series?: :any-series?

; ANY-TYPE! had an ambiguity in it, which was that with DATATYPE! around
; (possibly being renamed to TYPE! but maybe not) it sounded as if it might
; mean ANY-DATATYPE!--which is more narrow than what it meant to say which
; is "really, any legal Rebol value, type or otherwise".  So ANY-VALUE! is
; the better word for that.  Added for backwards compatibility.
;
any-type!: :any-value!

; !!! BIND? and BOUND? will have to go, but it's not clear exactly if
; BIND-OF or CONTEXT-OF or what is the right term.  So a mass renaming
; effort has not been undertaken, especially given the number of bootstrap
; references.  When the new name final decision comes, then BIND? and BOUND?
; will be the ones with their home in the legacy module...

bind-of: :bound?
;bind?
;bound?


; !!! These less common cases still linger as question mark routines that
; don't return LOGIC!, and they seem like they need greater rethinking in
; general. What replaces them (for ones that are kept) might be entirely new.

;encoding?
;file-type?
;speed?
;suffix?
;why?
;info?
;exists?


; In word-space, TRY is very close to ATTEMPT, in having ambiguity about what
; is done with the error if one happens.  It also has historical baggage with
; TRY/CATCH constructs. TRAP does not have that, and better parallels CATCH
; by communicating you seek to "trap errors in this code (with this handler)"
; Here trapping the error suggests you "caught it in a trap" and it is
; in the trap (and hence in your possession) to examine.  /WITH is not only
; shorter than /EXCEPT but it makes much more sense.
;
; !!! This may free up TRY for more interesting uses, such as a much shorter
; word to use for ATTEMPT.  Even so, this is not a priority at this point in
; time...so TRY is left to linger without needing `do <r3-legacy>`
;
try: func [
    {Tries to DO a block and returns its value or an error.}
    block [block!]
    /except "On exception, evaluate this code block"
    code [block! any-function!]
][
    either except [trap/with block :code] [trap block]
]


; HAS is targeted for use to be the arity-1 parallel to OBJECT as arity-2,
; (similar to the relationship between DOES and FUNCTION).
;
has: func [
    {A shortcut to define a function that has local variables but no arguments.}
    vars [block!] {List of words that are local to the function}
    body [block!] {The body block of the function}
][
    func (head insert copy vars /local) body
]


; To invoke this function, use `do <r3-legacy>` instead of calling it
; directly, as that will be a no-op in older Rebols.  Notice the word
; is defined in sys-base.r, as it needs to be visible pre-Mezzanine
;
set 'r3-legacy* func [] [

    ; NOTE: these flags only work in debug builds.  A better availability
    ; test for the functionality is needed, as these flags may be expired
    ; at different times on a case-by-case basis.
    ;
    system/options/lit-word-decay: true
    system/options/do-runs-functions: true
    system/options/broken-case-semantics: true
    system/options/exit-functions-only: true
    system/options/refinements-true: true
    system/options/no-switch-evals: true
    system/options/no-switch-fallthrough: true
    system/options/forever-64-bit-ints: true
    system/options/print-forms-everything: true
    system/options/break-with-overrides: true
    system/options/none-instead-of-unsets: true
    system/options/arg1-arg2-arg3-error: true

    append system/contexts/user compose [

        and: (:and*)

        or: (:or+)

        xor: (:xor-)

        ; Not contentious, but trying to excise this ASAP
        funct: (:function)

        ; Add simple parse back in by delegating to split, and return a LOGIC!
        parse: (function [
            {Parses a string or block series according to grammar rules.}
            input [any-series!] "Input series to parse"
            rules [block! string! none!] "Rules (string! is <r3-legacy>, use SPLIT)"
            /case "Uses case-sensitive comparison"
            /all "Ignored refinement for <r3-legacy>"
        ][
            lib/case [
                none? rules [
                    split input charset reduce [tab space cr lf]
                ]

                string? rules [
                    split input to-bitset rules
                ]

                true [
                    ; !!! We could write this as:
                    ;
                    ;     lib/parse/:case input rules
                    ;
                    ; However, system/options/refinements-true has been set.
                    ; We could move the set to after the function is defined,
                    ; but probably best since this is "mixed up" code to use
                    ; the pattern that works either way.
                    ;
                    true? apply :lib/parse [input rules case]
                ]
            ]
        ])

        ; For reasons of optimization, underuse, aesthetics, and a better
        ; future strategy for the feature... /only has been removed from
        ; the conditionals.

        if: (func [
            {If TRUE condition, return arg; evaluate blocks by default.}
            condition
            true-branch [any-value!]
            /only "Return block arg instead of evaluating it."
        ][
            lib/either all [only block? :true-branch] [
                lib/if :condition [:true-branch]
            ][
                lib/if :condition :true-branch
            ]
        ])

        either: (func [
            {If TRUE condition return first arg, else second; evaluate blocks by default.}
            condition
            true-branch [any-value!]
            false-branch [any-value!]
            /only "Suppress evaluation of block args."
        ][
            lib/either :condition [
                lib/either all [only block? :true-branch] [
                    :true-branch
                ] :true-branch
            ][
                lib/either all [only block? :false-branch] [
                    :false-branch
                ] :false-branch
            ]
        ])

        unless: (func [
            {If FALSE condition, return arg; evaluate blocks by default.}
            condition
            false-branch [any-value!]
            /only "Return block arg instead of evaluating it."
        ][
            lib/either all [only block? :false-branch] [
                lib/unless :condition [:false-branch]
            ][
                lib/unless :condition :false-branch
            ]
        ])
    ]

    return none
]
