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
    Note: {
        !!! This file currenly only works in older/bootstrap Ren-C.  It
        needs attention to make it work in R3-Alpha or Rebol2 again, which
        probably wouldn't take much work, but is not currently a priority.
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

; Neither R3-Alpha nor early bootstrapping Ren-C have "choose", but it's a
; very useful form of CASE
;
if :choose = () [
    choose: function [choices [block!] /local result] [
        while [not tail? choices] [
            set 'result do/next choices 'choices
            if :result [
                return do/next choices 'choices
            ]
        ]
        return ()
    ]
]

; Renamed, tightened, and extended with new features (add /PASS feature?)
;
if :file-to-local = () [
    file-to-local: :to-local-file
    local-to-file: :to-rebol-file
]

; As 3-letter words go, TRY is more useful to turn words to blank, but
; R3-Alpha and older Ren-C had this as a synonym for TRAP.
;
if find words-of :try 'block [
    try: :to-value
]


; OF is an action-like dispatcher which is used for property extraction.  It
; quotes its left argument, which R3-Alpha cannot do.  One might think it
; could be quoted by turning the properties being asked for by WORD! into
; functions, and letting them handle the dispatch...ignoring the OF:
;
;     length: func ['w [word!] series] [
;         assert [w = 'of]
;         return length of series
;     ]
;
; The problem with this is that you now can't call your variables LENGTH or
; TYPE or WORDS--which are all very common.  Instead they are set by default
; to functions that warn you about this.  They can be overwritten.
;
if :of = () [
    index: func [dummy:] [
        fail/where "INDEX OF not supported in R3-Alpha, use INDEX-OF" 'dummy
    ]
    offset: func [dummy:] [
        fail/where "OFFSET OF not supported in R3-Alpha, use OFFSET-OF" 'dummy
    ]
    length: func [dummy:] [
        fail/where "LENGTH OF not supported in R3-Alpha, use LENGTH-OF" 'dummy
    ]
    type: func [dummy:] [
        fail/where "TYPE OF not supported in R3-Alpha, use TYPE-OF" 'dummy
    ]
    words: func [dummy:] [
        fail/where "WORDS OF not supported in R3-Alpha, use WORDS-OF" 'dummy
    ]
]

if true = attempt [void? :some-undefined-thing] [
    ;
    ; With libRebol parity of Rebol's NULL being committed to as C's NULL,
    ; sharing names makes more sense than the "illusion of void"

    null: :void
    null?: :void?
    unset 'void
    unset 'void?

    append lib compose [
        text!: (string!)
        text?: (:string?)
        to-text: (:to-string)
    ]
    text!: lib/text!
    text?: :lib/text?
    to-text: :lib/to-text

    ; THEN and ELSE use a mechanic (non-tight infix evaluation) that is simply
    ; impossible in R3-Alpha or Rebol2.
    ;
    else: does [
        fail "Do not use ELSE in scripts which want compatibility w/R3-Alpha" 
    ]
    then: does [
        fail "Do not use THEN in scripts which want compatibility w/R3-Alpha"
    ]

    ; UNTIL was deprecated under its old meaning, but ultimately reverted
    ; so put it back...
    ;
    until: :loop-until

    ; WHILE-NOT can't be written correctly in usermode R3-Alpha (RETURN won't
    ; work definitionally)
    ;
    while-not: does [
        fail "Don't use WHILE-NOT when you want R3-Alpha compatibility"
    ]
    until-not: does [
        fail "Don't use UNTIL-NOT when you want R3-Alpha compatibility"
    ]

    either () = :really [
        ;-- Ren-Cs up to around Jan 27, 2018

        assert [find words-of :ensure 'test]
        really: func [optional [<opt> any-value!]] [
            if any [null? :optional blank? :optional] [
                fail/where [
                    "REALLY expects argument to be SOMETHING?"
                ] 'optional
            ]
            :optional
        ]
    ][
        assert [find words-of :ensure 'test]
    ]

    did: func [optional [<opt> any-value!]] [
        either all [lib/not null? :optional | :optional] [true] [false]
    ]
    not: func [optional [<opt> any-value!]] [
        either any [null? :optional | :optional] [false] [true]
    ]

    ; COMPRESS no longer supports "Rebol format compression" (which was
    ; non-raw zlib envelope plus 32-bit length)
    ;
    if (set? 'compress) and (find words-of :compress /gzip) [
        deflate: specialize 'compress [gzip: false | only: true] ;; "raw"
        inflate: specialize 'decompress [gzip: false | only: true] ;; "raw"

        gzip: specialize 'compress [gzip: true]
        gunzip: specialize 'decompress [gzip: true]

        compress: decompress: does [
            fail "COMPRESS/DECOMPRESS replaced by gzip/gunzip/inflate/deflate"
        ]
    ]

    if not error? trap [switch 3 [1 + 2 [3]]] [ ;-- no error is non-evaluative
        switch: adapt 'switch [
            cases: map-each c cases [
                case [
                    lit-word? :c [to word! c]
                    lit-path? :c [to path! c]

                    path? :c [
                        fail/where ["Switch now evaluative" c] 'cases
                    ]
                    word? :c [
                        if not datatype? get c [
                            fail/where ["Switch now evaluative" c] 'cases
                        ]
                        get c
                    ]

                    true [:c]
                ]
            ]
        ]
    ]
                    
    QUIT ;-- !!! stops running if Ren-C here.
]

if true == attempt [null? :some-undefined-thing] [
    QUIT ;-- !!! a Ren-C post VOID? => NULL? conversion, circa 2-May-2018
]

write-stdout: func [value] [prin :value]
print-newline: does [prin newline]


; Running R3-Alpha/Rebol2, bootstrap NULL? into existence and continue
;
null?: :unset?
null: does []


unset 'function ;-- we'll define it later, use FUNC until then


; Capture the UNSET! datatype, so that `func [x [*opt-legacy* integer!]]` can
; be used to implement `func [x [<opt> integer!]]`.
; 
*opt-legacy*: unset!


; Ren-C changed the function spec dialect to use TAG!s.  Although MAKE of
; an ACTION! as a fundamental didn't know keywords (e.g. RETURN), FUNC
; was implemented as a native that could also be implemented in usermode.
; FUNCTION added some more features.
;
old-func: :func
func: old-func [
    spec [block!]
    body [block!]
    /local pos type
][
    spec: copy/deep spec
    parse spec [while [
        pos:
        [
            <local> (change pos quote /local)
        |
            ; WITH is just commentary in FUNC, but for it to work we'd have
            ; to go through and take words that followed it out.
            ;
            <with> (fail "<with> not supported in R3-Alpha FUNC")
        |
            and block! into [any [
                type:
                [
                    <opt> (change type '*opt-legacy*)
                |
                    <end> (fail "<end> not supported in R3-Alpha mode")
                |
                    skip
                ]
            ]]
        |
            ; Just get rid of any RETURN: specifications (purely commentary)
            ;
            remove [quote return: opt block! opt text!]
        |
            ; We could conceivably gather the SET-WORD!s and put them into
            ; the /local list, but that's annoying work.
            ;
            copy s set-word! (
                fail ["SET-WORD!" s "not supported for <local> in R3-Alpha"]
            )
        |
            skip
        ]
    ]]

    ; R3-Alpha did not copy the spec or body in MAKE ACTION!, but FUNC and
    ; FUNCTION would do it.  Since we copied above in order to mutate to
    ; account for differences in the spec language, don't do it again.
    ;
    make function! reduce [spec body]
]


; PROTECT/DEEP isn't exactly the same thing as LOCK, since you can unprotect
;
lock: func [x] [protect/deep :x]


blank?: get 'none?
blank!: get 'none!
blank: get 'none
_: none

; BAR! is really just a WORD!, but can be recognized
;
bar?: func [x] [x = '|]

; ANY-VALUE! is anything that isn't void.
;
any-value!: difference any-type! (make typeset! [unset!])
value?: any-value?: func [optional [<opt> any-value!]] [not null? :optional]


; Used in function definitions before the mappings
;
any-context!: :any-object!
any-context?: :any-object?

set?: func [
    "Returns whether a bound word has a value (fails if unbound)"
    any-word [any-word!]
][
    if not bound? any-word [
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


did: :to-logic

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

construct: func [
    spec [datatype! block! any-context!]
    body [block! any-context! none!]
    /only
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
; UNSET! in older Rebols is not the same, e.g. `do [1 + 2 |]` will be UNSET!
; as opposed to 3.  But since UNSET!s do not vote in many ANY/ALL situations
; it can act similarly.
;
|: does []


; Ren-C's SET acts like SET/ANY, always accept null assignments.
;
lib-set: get 'set ; overwriting lib/set for now
set: function [
    target [any-word! any-path! block!]
    value [<opt> any-value!]
    /pad
][
    set_ANY: true
    apply :lib-set [target :value set_ANY pad]
]


; Ren-C's GET acts like GET/ANY, always getting null for unset variables.
;
lib-get: get 'get
get: function [
    source [any-word! any-path! block!]
][
    lib-get/any source
]


; R3-Alpha would only REDUCE a block and pass through other outputs.
; REDUCE in Ren-C (and also in Red) is willing to reduce anything that
; does not require EVAL-like argument consumption (so GROUP!, GET-WORD!,
; GET-PATH!).
;
lib-reduce: get 'reduce
reduce: func [
    value
    /no-set
    /only
    words [block! blank!]
    /into
    target [any-array!]
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
    reason [error! text! block!]
][
    switch type-of reason [
        error! [do error]
        string! [do make error! reason]
        block! [
            for-each item reason [
                if not any [
                    any-scalar? :item
                    text? :item
                    group? :item
                    all [
                        word? :item
                        not action? get :item
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
    fail "UNSET? reserved for future use, use NULL? to test no value"
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


decompress: compress: does [
    fail [
        "COMPRESS in R3-Alpha produced corrupt data using the /GZIP option."
        "Ren-C uses gzip by default, and does not support 'Rebol compression'"
        "(which was a non-raw zlib envelope plus 32-bit length, which can be"
        "implemented using the new DEFLATE native and appending 4 bytes)."
        "For forward compatibility, use DEFLATE, and store the size somewhere"
        "to use with a Ren-C INFLATE call for more efficient decompression"
    ]
]

gzip: gunzip: does [
    fail [
        "R3-Alpha's COMPRESS/GZIP was broken.  If bootstrap for Ren-C ever"
        "is made to work with R3-Alpha again, either DEFLATE would need to be"
        "used (storing the size elsewhere) or a usermode fake up of GZIP"
        "would have to be implemented.  The latter is the better idea, and"
        "isn't that difficult to do."
     ]
]

deflate: func [data [binary! text!]] [
    compressed: compress data
    loop 4 [take/last compressed] ;-- 32-bit size at tail in "Rebol format"
    compressed
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
    value [<opt> any-value!]
][
    either* blank? :value [()] [:value]
]

trap: func [
    code [block! function!]
    /with
    handler [block! function!]
][
    either with [
        lib/try/except :code :handler
    ][
        lib/try :code
    ]
]

something?: func [value [<opt> any-value!]] [
    not any [
        null? :value
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
and+: get 'and
and?: func [a b] [to-logic all [:a :b]]
and: get 'and ; see above

or+: get 'or
or?: func [a b] [to-logic any [:a :b]]
or: get 'or ; see above

xor+: get 'xor
xor?: func [a b] [to-logic any [all [:a (not :b)] all [(not :a) :b]]]


; UNSPACED in Ren-C corresponds rougly to AJOIN, and SPACED corresponds very
; roughly to REFORM.  A similar "sort-of corresponds" applies to REJOIN being
; like JOIN-ALL.
;
delimit: func [x delimiter] [
    either block? x [
        pending: false
        out: make text! 10
        while [not tail? x] [
            if bar? first x [
                pending: false
                append out newline
                x: next x
                continue
            ]
            set/opt 'item do/next x 'x
            case [
                any [blank? :item | null? :item] [
                    ;-- append nothing
                ]

                true [
                    case [
                        ; Characters (e.g. space or newline) are not counted
                        ; in delimiting.
                        ;
                        char? :item [
                            append out item
                            pending: false
                        ]
                        all [pending | not blank? :delimiter] [
                            append out form :delimiter
                            append out form :item
                            pending: true
                        ]
                        true [
                            append out form :item
                            pending: true
                        ]
                    ]
                ]
            ]
        ]
        out
    ][
        reform :x
    ]
]

unspaced: func [x] [
    delimit x blank
]

spaced: func [x] [
    delimit :x space
]

join-all: :rejoin


make-action: func [
    {Internal generator used by FUNCTION and PROCEDURE specializations.}
    return: [action!]
    generator [action!]
        {Arity-2 "lower"-level generator to use (e.g. FUNC or PROC)}
    spec [block!]
    body [block!]
    <local>
        new-spec var other
        new-body exclusions locals defaulters statics
][
    exclusions: copy []
    new-spec: make block! length-of spec
    new-body: _
    statics: _
    defaulters: _
    var: _
    locals: copy []

    ;; dump [spec]

    ; Gather the SET-WORD!s in the body, excluding the collected ANY-WORD!s
    ; that should not be considered.  Note that COLLECT is not defined by
    ; this point in the bootstrap.
    ;
    ; !!! REVIEW: ignore self too if binding object?
    ;
    parse spec [any [
        if (set? 'var) [
            set var: any-word! (
                append exclusions :var ;-- exclude args/refines
                append new-spec :var ;-- need GET-WORD! for R3-Alpha lit decay
            )
        |
            set other: [block! | text!] (
                append/only new-spec other ;-- spec notes or data type blocks
            )
        ]
    |
        other:
        [group!] (
            if not var [
                fail [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other/1)
                ]
            ]
            if not defaulters [
                defaulters: copy []
            ]
            append defaulters compose/deep [
                (to set-word! var) default [(reduce other/1)]
            ]
        )
    |
        (unset 'var) ;-- everything below this line clears var
        fail ;-- failing here means rolling over to next rule
    |
        <local>
        any [set var: word! (other: _) opt set other: group! (
            append locals var
            append exclusions var
            if other [
                if not defaulters [
                    defaulters: copy []
                ]
                append defaulters compose/deep [
                    (to set-word! var) default [(reduce other)]
                ]
            ]
        )]
        (unset 'var) ;-- don't consider further GROUP!s or variables
    |
        <in> (
            if not new-body [
                append exclusions 'self
                new-body: copy/deep body
            ]
        )
        any [
            set other: [word! | path!] (
                other: really any-context! get other
                bind new-body other
                for-each [key val] other [
                    append exclusions key
                ]
            )
        ]
    |
        <with> any [
            set other: [word! | path!] (append exclusions other)
        |
            text! ;-- skip over as commentary
        ]
    |
        <static> (
            if not statics [
                statics: copy []
            ]
            if not new-body [
                append exclusions 'self
                new-body: copy/deep body
            ]
        )
        any [
            set var: word! (other: quote ()) opt set other: group! (
                append exclusions var
                append statics compose/only [
                    (to set-word! var) (other)
                ]
            )
        ]
        (unset 'var)
    |
        end accept
    |
        other: (
            fail [
                ; <where> spec
                ; <near> other
                "Invalid spec item:" (mold other/1)
            ]
        )
    ]]

    collected-locals: collect-words/deep/set/ignore body exclusions

    ;; dump [{before} statics new-spec exclusions]

    if statics [
        statics: has statics
        bind new-body statics
    ]

    ; !!! The words that come back from COLLECT-WORDS are all WORD!, but we
    ; need SET-WORD! to specify pure locals to the generators.  Review the
    ; COLLECT-WORDS interface to efficiently give this result, as well as
    ; a possible COLLECT-WORDS/INTO
    ;
    append new-spec <local> ;-- SET-WORD! not supported in R3-Alpha mode
    for-next collected-locals [
        append new-spec collected-locals/1
    ]
    for-next locals [
        append new-spec locals/1
    ]

    ;; dump [{after} new-spec defaulters]

    generator new-spec either defaulters [
        append/only defaulters as group! any [new-body body]
    ][
        any [new-body body]
    ]
]

;-- These are "redescribed" after REDESCRIBE is created
;
function: func [spec body] [
    make-action :func spec body
]

procedure: func [spec body] [
    make-action :proc spec body
]


; This isn't a full implementation of REALLY with function-oriented testing,
; but it works well enough for types.
;
really: function [type [datatype! block!] value [<opt> any-value!]] [
    either block? type [
        type: make typeset! type
        if not find type type-of :value [
            probe :value
            fail [
                "REALLY expected:" (mold type) "but got" (mold type-of :value)
            ]
        ]
    ][        
        if type != type-of :value [
            probe :value
            fail [
                "REALLY expected:" (mold type) "but got" (mold type-of :value)
            ]
        ]
    ]
    return :value
]
