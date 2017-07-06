REBOL [
    Title: "Rebol2 and R3-Alpha Future Bridge to Ren-C"
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        These routines can be run from R3-Alpha or Rebol2 to make them act
        more like the vision of Rebol3-Beta and beyond (as conceived by the
        "Ren-C" initiative).

        It also must remain possible to run it from Ren-C without disrupting
        the environment.  This is because the primary motivation for its
        existence is to shim older R3-MAKE utilities to be compatible with
        Ren-C...and the script is run without knowing whether the R3-MAKE
        you are using is old or new.  No canonized versioning strategy has
        been yet chosen, so words are "sniffed" for existing definitions in
        this somewhat simplistic method.

        !!! Because the primary purpose is for Ren-C's bootstrap, the file
        is focused squarely on those needs.  However, it is a beginning for
        a more formalized compatibility effort.  Hence it is awaiting someone
        who has a vested interest in Rebol2 or R3-Alpha code to become a
        "maintenance czar" to extend the concept.  In the meantime it will
        remain fairly bare-bones, but enhanced if-and-when needed.
    }
]


if true = attempt [void? :some-undefined-thing] [
    ;
    ; Ren-C, define an "optional" marker for returns.  (You can't use <opt>
    ; on parameters in code designed to run Ren-C-like code in R3-Alpha.)
    ;
    *opt-legacy*: _

    ; ELSE uses a mechanic (non-tight infix evaluation) that is simply
    ; impossible in R3-Alpha or Rebol2.
    ;
    else: does [
        fail "Do not use ELSE in scripts which want compatibility w/R3-Alpha" 
    ]

    QUIT ;-- !!! stops running if Ren-C here.
]


; Running R3-Alpha/Rebol2, bootstrap VOID? into existence and continue
;
void?: :unset?
void: does []


; Older versions of Rebol had a different concept of what FUNCTION meant
; (an arity-3 variation of FUNC).  Eventually the arity-2 construct that
; did locals-gathering by default named FUNCT overtook it, with the name
; FUNCT deprecated.
;
unless (copy/part words-of :function 2) = [spec body] [
    function: :funct
]


; `func [x [*opt-legacy* integer!]]` is like `func [x [<opt> integer!]]`,
; and with these modifications can work in either Ren-C or R3-Alpha/Rebol2.
;
*opt-legacy*: unset!


; PROTECT/DEEP isn't exactly the same thing as LOCK, since you can unprotect
;
lock: func [x] [protect/deep :x]


blank?: get 'none?
blank!: get 'none!
blank: get 'none
_: none


; ANY-VALUE! is anything that isn't void.
;
any-value!: difference any-type! (make typeset! [unset!])
any-value?: func [item [*opt-legacy* any-value!]] [not void? :item]


; Used in function definitions before the mappings
;
any-context!: :any-object!
any-context?: :any-object?


set?: func [
    "Returns whether a bound word has a value (fails if unbound)"
    any-word [any-word!]
][
    unless bound? any-word [
        fail [any-word "is not bound in set?"]
    ]
    value? any-word ;-- the "old" meaning of value...
]

verify: :assert ;-- ASSERT is a no-op in Ren-C in "release", but verify isn't



leave: does [
    do make error! "LEAVE cannot be implemented in usermode R3-Alpha"
]

proc: func [spec body] [
    func spec compose [(body) void]
]

procedure: func [spec body] [
    function spec compose [(body) void]
]


; Ren-C replaces the awkward term PAREN! with GROUP!  (Retaining PAREN!
; for compatibility as pointing to the same datatype).  Older Rebols
; haven't heard of GROUP!, so establish the reverse compatibility.
;
group?: get 'paren?
group!: get 'paren!


; The HAS routine in Ren-C is used for object creation with no spec, as
; a parallel between FUNCTION and DOES.  It is favored for this purpose
; over CONTEXT which is very "noun-like" and may be better for holding
; a variable that is an ANY-CONTEXT!
;
; Additionally, the CONSTRUCT option behaves like MAKE ANY-OBJECT, sort of,
; as the way of creating objects with parents or otherwise.
;
has: :context

construct-legacy: :construct

construct: function [
    "Creates an ANY-CONTEXT! instance"
    spec [datatype! block! any-context!]
        "Datatype to create, specification, or parent/prototype context"
    body [block! any-context! none!]
        "keys and values defining instance contents (bindings modified)"
    /only
        "Values are kept as-is"
][
    either only [
        if block? spec [spec: make object! spec]
        construct-legacy/only/with body spec
    ][
        if block? spec [
            ;
            ; If they supplied a spec block, do a minimal behavior which
            ; will create a parent object with those fields...then run
            ; the traditional gathering added onto that using the body
            ;
            spec: map-each item spec [
                assert [word? :item]
                to-set-word item
            ]
            append spec none
            spec: make object! spec
        ]
        make spec body
    ]
]


; Lone vertical bar is an "expression barrier" in Ren-C, but a word character
; in other situations.  Having a word behave as a function that returns an
; UNSET! in older Rebols is not quite the same, but can have a similar effect
; in terms of creating errors if picked up by most function args.
;
|: does []


; SET/OPT is the Ren-C replacement for SET/ANY, with /ANY supported
; via <r3-legacy>.  But Rebol2 and R3-Alpha do not know /OPT.
;
lib-set: get 'set ; overwriting lib/set for now
set: func [
    {Sets a word, path, block of words, or context to specified value(s).}

    target [any-word! any-path! block! any-context!]
        {Word, block of words, path, or object to be set (modified)}

    value [*opt-legacy* any-value!]
        "Value or block of values"
    /opt
        "Value is optional, and if no value is provided then unset the word"
    /pad
        {For objects, set remaining words to NONE if block is too short}
    /any
        "Deprecated legacy synonym for /opt"
][
    set_ANY: any
    any: :lib/any ;-- in case it needs to be used
    opt_ANY: opt
    lib-set/any 'opt () ;-- doesn't exist in R3-Alpha

    apply :lib-set [target :value (any [opt_ANY set_ANY]) pad]
]


; GET/OPT is the Ren-C replacement for GET/ANY, with /ANY supported
; via <r3-legacy>.  But Rebol2 and R3-Alpha do not know /OPT.
;
lib-get: get 'get
get: function [
    {Gets the value of a word or path, or values of a context.}
    source
        "Word, path, context to get"
    /opt
        "The source may optionally have no value (allows returning void)"
    /any
        "Deprecated legacy synonym for /OPT"
][
    set_ANY: any
    any: :lib/any ;-- in case it needs to be used
    opt_ANY: opt
    lib-set/any 'opt () ;-- doesn't exist in R3-Alpha

    lib-set/any (quote temp:) lib-get/any source
    either any [opt_ANY set_ANY] [
        :temp ;-- voids okay
    ][
        either void? :temp [blank] [:temp]
    ]
]


; R3-Alpha would only REDUCE a block and pass through other outputs.
; REDUCE in Ren-C (and also in Red) is willing to reduce anything that
; does not require EVAL-like argument consumption (so GROUP!, GET-WORD!,
; GET-PATH!).
;
lib-reduce: get 'reduce
reduce: func [
    {Evaluates expressions and returns multiple results.}
    value
    /no-set
        "Keep set-words as-is. Do not set them."
    /only
        "Only evaluate words and paths, not functions"
    words [block! blank!]
        "Optional words that are not evaluated (keywords)"
    /into
        {Output results into a series with no intermediate storage}
    target [any-block!]
][
    either block? :value [
        apply :lib-reduce [value no-set only words into target]
    ][
        ; For non-blocks, put the item in a block, reduce the block,
        ; then pick the first element out.  This may error (e.g. if you
        ; try to reduce a word looking up to a function taking arguments)
        ;
        ; !!! Simple with no refinements for now--enhancement welcome.
        ;
        assert [not no-set not only not into]
        first (lib-reduce lib-reduce [:value])
    ]
]


; Ren-C's FAIL dialect is still being designed, but the basic is to be
; able to ramp up from simple strings to block-composed messages to
; fully specifying ERROR! object fields.  Most commonly it is a synonym
; for `do make error! form [...]`.
;
fail: func [
    {Interrupts execution by reporting an error (TRAP can intercept it).}
    reason [error! string! block!]
        "ERROR! value, message string, or failure spec"
][
    case [
        error? reason [do error]
        string? reason [do make error! reason]
        block? reason [
            for-each item reason [
                unless any [
                    any-scalar? :item
                    string? :item
                    group? :item
                    all [
                        word? :item
                        not any-function? get :item
                    ]
                ][
                    probe reason
                    do make error! (
                        "FAIL requires complex expressions in a GROUP!"
                    )
                ]
            ]
            do make error! form reduce reason
        ]
    ]
]


unset!: does [
    fail "UNSET! not a type, use *opt-legacy* as <opt> in func specs"
]

unset?: does [
    fail "UNSET? reserved for future use, use VOID? to test no value"
]


; Note: EVERY cannot be written in R3-Alpha because there is no way
; to write loop wrappers, given lack of definitionally scoped return
;
for-each: get 'foreach
foreach: does [
    fail "In Ren-C code, please use FOR-EACH and not FOREACH"
]

for-next: get 'forall
forall: does [
    fail "In Ren-C code, please use FOR-NEXT and not FORALL"
]


; Not having category members have the same name as the category
; themselves helps both cognition and clarity inside the source of the
; implementation.
;
any-array?: get 'any-block?
any-array!: get 'any-block!


; Renamings to conform to ?-means-returns-true-false rule
; https://trello.com/c/BxLP8Nch
;
length: length-of: get 'length?
index-of: get 'index?
offset-of: get 'offset?
type-of: get 'type?


; Source code that comes back from LOAD or is in a module is read-only in
; Ren-C by default.  Non-mutating forms of the "mutate by default"
; operators are suffixed by -OF (APPEND-OF, INSERT-OF, etc.)  There
; is a relationship between historical "JOIN" and "REPEND" that is very
; much like this, and with JOIN the mutating form and JOIN-OF the one
; that copies, it brings about consistency and kills an annoying word.
;
; Rather than change this all at once, JOIN becomes JOIN-OF and REPEND
; is left as it is (as the word has no intent to be reclaimed for other
; purposes.)
;
join-of: get 'join 
join: does [
    fail "use JOIN-OF for JOIN (one day, JOIN will replace REPEND)"
]

; R3-Alpha's version of REPEND was built upon R3-Alpha's notion of REDUCE,
; which wouldn't reduce anything but BLOCK!.  Having it be a no-op on PATH!
; or WORD! was frustrating, so Red and Ren-C made it actually reduce whatever
; it got.  But that affected REPEND so that it arguably became less useful.
;
; With Ren-C retaking JOIN, it makes more sense to take more artistic license
; and make the function more useful than strictly APPEND REDUCE as suggested
; by the name REPEND.  So in that spirit, the JOIN will only reduce blocks.
; This makes it like R3-Alpha's REPEND.
;
; The temporary name is ADJOIN, which will be changed to JOIN someday when
; existing JOIN usages have all been changed to JOIN-OF.
;
adjoin: get 'repend


; It's not possible to write loop wrappers that work correctly with RETURN,
; and so a good forward-compatible version of UNTIL as WHILE-NOT isn't really
; feasible.  So just don't use it.
;
loop-until: get 'until
until: does [
    fail "UNTIL in Ren-C will be arity 2 (WHILE-NOT), can't mimic in R3-Alpha"
]


; Note: any-context! and any-context? supplied at top of file

; *all* typesets now ANY-XXX to help distinguish them from concrete types
; https://trello.com/c/d0Nw87kp
;
any-scalar?: get 'scalar?
any-scalar!: scalar!
any-series?: get 'series?
any-series!: series!
any-number?: get 'number?
any-number!: number!


; "optional" (a.k.a. void) handling
opt: func [
    {Turns blanks to voids, all other value types pass through.}
    value [*opt-legacy* any-value!]
][
    either* blank? :value [()] [:value]
]

to-value: func [
    {Turns voids to blank, with ANY-VALUE! passing through. (See: OPT)}
    value [*opt-legacy* any-value!]
][
    get 'value
]

something?: func [value [*opt-legacy* any-value!]] [
    not any [
        void? :value
        blank? :value
    ]
]

; It is not possible to make a version of eval that does something other
; than everything DO does in an older Rebol.  Which points to why exactly
; it's important to have only one function like eval in existence.
;
eval: get 'do


; R3-Alpha and Rebol2 did not allow you to make custom infix operators.
; There is no way to get a conditional infix AND using those binaries.
; In some cases, the bitwise and will be good enough for logic purposes...
;
and*: get 'and
and?: func [a b] [to-logic all [:a :b]]
and: get 'and ; see above

or+: get 'or
or?: func [a b] [to-logic any [:a :b]]
or: get 'or ; see above

xor+: get 'xor
xor?: func [a b] [to-logic any [all [:a (not :b)] all [(not :a) :b]]]


; UNSPACED in Ren-C corresponds rougly to AJOIN, and SPACED corresponds very
; roughly to REFORM.  A similar "sort-of corresponds" applies to REJOIN being
; like JOIN-ALL.  There are missing features in the handling of voids and
; blanks, as well as CHAR!s and BAR!s.
;
; Since the only code really running modern Ren-C-named constructs through an
; R3-Alpha is the bootstrap, the necessity of making this work well depends
; on how aggressive the use of modern features in bootstrap are.
;
unspaced: :ajoin
spaced: :reform
join-all: :rejoin


; This isn't a full implementation of ENSURE with function-oriented testing,
; but it works well enough for types.
;
ensure: function [type [datatype!] value [*opt-legacy* any-value!]] [
    if type != type-of :value [
        probe :value
        fail ["ENSURE expected:" (mold type) "but got" (mold type-of :value)]
    ]
    return :value
]
