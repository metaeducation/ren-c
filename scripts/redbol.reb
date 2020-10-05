REBOL [
    System: "Rebol 3 (Ren-C Branch)"
    Title: "Rebol2 and Red Compatibility Shim"
    Homepage: https://trello.com/b/l385BE7a/porting-guide
    Rights: {
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This module attempts to adapt Ren-C so that basic functionality will
        respond similarly to the compatible subset of Rebol2 and Red.

        The current lack of a GUI in Ren-C means that this will only be
        useful for command-line scripts and utilities.  However, it serves as
        a test of the system's flexibility, as well as a kind of "living
        documentation" of the nuances of what has been changed.

        (The comments in this file are deliberately brief...see %r2warn.reb
        for the warnings and comments that would be included if that file
        was able to be folded in with this one--not possible, *yet*)
    }
    Notes: {
        * Ren-C does not allow the mutation of PATH!.  You can JOIN a path to
          make a new one, and FOREACH a path to enumerate one, but you can't
          APPEND or INSERT into them.  Calling code that expects to do these
          kinds of mutations needs to be changed to do them on BLOCK! and
          convert to PATH! when done.
    }
]

; !!! The general workings of modules is to scan them for top-level set-words,
; and then bind the module itself to those words.  This module is redefining
; the workings of the system fundamentally.  While doing those definitions
; it's preferable to not have to say `lib/switch` or otherwise prefix each
; call in the implementation so it doesn't use its own new definitions.  Until
; that becomes some kind of module feature, this folds the binding to lib
; into EMULATE, which lets you select whether you want to replace the
; functionality or just warn about it.
;
export: lib/func [
    {!!! `export` should be a module feature !!!}
    set-word [set-word!]
] lib/in lib [
    ; !!! Not actually "exporting" yet...
]

helper: enfixed lib/func [
    return: <void>
    :set-word [set-word!]
    code [block!]
] lib/in lib [
    set set-word do in lib code
]

emulate: enfixed lib/func [
    return: [<opt> any-value!]
    :set-word [set-word!]
    code [block!]
] lib/in lib [
    set set-word do in lib code
    elide export set-word
]


any-function!: emulate [action!]
function!: emulate [action!]
any-function?: emulate [:action?]
function?: emulate [:action?]

string!: emulate [text!]
string?: emulate [:text?]
to-string: emulate [specialize 'to [type: text!]]

paren!: emulate [group!]
paren?: emulate [:group?]
to-paren: emulate [specialize 'to [type: group!]]

number!: emulate [any-number!]
number?: emulate [:any-number?]
scalar!: emulate [any-scalar!]
scalar?: emulate [:any-scalar?]
series!: emulate [any-series!]
series?: emulate [:any-series?]

any-type!: emulate [any-value!]  ; !!! does not include any "UNSET!"

any-block!: emulate [any-array!]
any-block?: emulate [:any-array?]

any-object!: emulate [any-context!]
any-object?: emulate [:any-context?]



; Refinement arguments in Ren-C are conveyed via the refinement value itself:
;
; https://trello.com/c/DaVz9GG3/
;
; The old behavior is simulated by creating locals for the refinement args
; and then having a bit of code at the beginning of the body that moves the
; refinement's value into it.
;
; Also adds a specialization of the definitional return to act as EXIT.
;
rewrite-spec-and-body: helper [
    function [
        spec "(modified)" [block!]
        body "(modified)" [block!]
    ][
        ; R3-Alpha didn't implement the Rebol2 `func [[throw catch] x y][...]`
        ; but it didn't error on the block in the first position.  It just
        ; ignored it.  For now, do the same in the emulation.
        ;
        if block? first spec [take spec]  ; skip Rebol2's [throw]

        ; Rebol2 and R3-Alpha hid refinements that appeared after /LOCAL
        ; https://forum.rebol.info/t/analogue-to-rebol2s-hidden-parameters/1273
        ; Don't hide them, but make them work by pushing them before <local>.
        ;
        local-tag-pos: null

        swap-if-after-local: does [
            if local-tag-pos [
                assert [local-tag-pos/1 = <local>]
                local-tag-pos: insert/only local-tag-pos take spec
                assert [local-tag-pos/1 = <local>]
            ]
        ]

        spool-descriptions-and-locals: does [
            while [match [text! set-word!] first spec] [  ; end-tolerant (null)
                if not set-word? spec/1 [
                    swap-if-after-local  ; description for hidden refinement..?
                ]
                spec: my next
            ]
        ]

        while [not tail? spec] [
            refinement: to word! try match path! spec/1

            ; Refinements with multiple arguments are no longer allowed, and
            ; there weren't many of those so it's not a big deal.  But there
            ; are *many* instances of the non-refinement usage of /LOCAL.
            ; These translate in Ren-C to the <local> tag.
            ;
            if refinement = 'local [
                change spec <local>
                local-tag-pos: spec  ; see note about hidden refinements above
                spec: my next
                continue
            ]

            if not refinement [
                spec: my next  ; ordinary args (or local WORD! after /LOCAL)
                continue
            ]

            ; if we get here it's a refinement that is *not* /LOCAL.  This
            ; means if local-tag-pos isn't null, we need to be moving
            ; everything we do back to before the <local> (spool does this too)
            ;
            swap-if-after-local
            spec: my next

            spool-descriptions-and-locals

            if not argument: match [word! lit-word! get-word!] first spec [
                insert body compose/deep [
                    (refinement): either (refinement) [true] [blank]
                ]
                continue
            ]

            if tail? spec [break]

            take spec  ; don't want argument between refinement + type block

            spool-descriptions-and-locals

            ; may be at tail, if so need the [any-value!] injection

            if types: match block! spec/1 [  ; explicit arg types
                swap-if-after-local
                spec: my next
            ]
            else [
                insert/only spec [any-value!]  ; old refinement-arg default
                swap-if-after-local
                spec: my next
            ]

            append spec as set-word! argument  ; SET-WORD! in specs are locals

            ; Take the value of the refinement and assign it to the argument
            ; name that was in the spec.  Then set refinement to true/blank.
            ;
            ; (Rebol2 missing refinements are #[none], or #[true] if present
            ; Red missing refinements are #[false], or #[true] if present
            ; Rebol2 and Red arguments to unused refinements are #[none]
            ; Since there's no agreement, Redbol goes with the Rebol2 way,
            ; since NONE! is closer to Ren-C's NULL for unused refinements.)

            insert body compose/deep [
                (argument): :(refinement)
                (refinement): either value? :(refinement) [true] [blank]
            ]

            if tail? spec [break]
            spool-descriptions-and-locals
            if tail? spec [break]

            if extra: match any-word! first spec [
                fail [
                    {Refinement} refinement {can't take more than one}
                    {argument in the Redbol emulation, so} extra {must be}
                    {done some other way.  (We should be *able* to do}
                    {it via variadics, but would be much more involved.)}
                ]
            ]
        ]

        spec: head spec  ; At tail, so seek head for any debugging!
    
        ; We don't go to an effort to provide a non-definitional return.  But
        ; add support for an EXIT that's a synonym for returning void.
        ;
        insert body [
            exit: specialize 'return [value: void]
        ]
        append spec [<local> exit]  ; FUNC needs it (function doesn't...)
    ]
]

; If a Ren-C function suspects it is running code that may happen more than
; once (e.g. a loop or function body) it marks that parameter `<const>`.
; That prevents casual mutations.
;
; !!! See notes in RESKINNED for why an ADAPT must be used (for now)

func-nonconst: emulate [
    reskinned [body [block!]] adapt :func []
]

function-nonconst: emulate [
    reskinned [body [block!]] adapt :function []
]

redbol-func: func: emulate [
    function [
        return: [action!]
        spec [block!]
        body [block!]
    ][
        if find spec <local> [
            return func-nonconst spec body  ; assume "new style" function 
        ]

        spec: copy spec
        body: copy body
        rewrite-spec-and-body spec body

        return func-nonconst spec body
    ]
]

redbol-function: function: emulate [
    function [
        return: [action!]
        spec [block!]
        body [block!]
        /with [object! block! map!]  ; from R3-Alpha, not adopted by Red
        /extern [block!]  ; from R3-Alpha, adopted by Red
    ][
        if find spec <local> [
            return function-nonconst spec body  ; assume "new style" function 
        ]

        if block? with [with: make object! with]

        spec: copy spec
        body: copy body
        rewrite-spec-and-body spec body

        ; The shift in Ren-C is to remove the refinements from FUNCTION, and
        ; put everything into the spec dialect...marked with <tags>
        ;
        if with [
            append spec compose [<in> (with)]  ; <in> replaces /WITH
        ]
        if extern [
            append spec compose [<with> ((extern))]  ; <with> replaces /EXTERN
        ]

        return function-nonconst spec body
    ]
]

apply: emulate [
    ; Historical Rebol had an APPLY which would take refinements themselves
    ; as arguments in the block.
    ;
    ; `APPEND/ONLY/DUP A B 2` => `apply :append [a b none none true true 2]`
    ;
    ; This made the apply call aware of the ordering of refinements in the
    ; spec, which is not supposed to be a thing.  So Ren-C's APPLY requires
    ; you to account for any refinements in your call by naming them in the
    ; path that you are applying, then the array should have exactly that
    ; number of arguments: https://trello.com/c/P2HCcu0V
    ;
    ; This emulation is a good example of how FRAME! can be used to build
    ; customized apply-like functions.
    ;
    function [
        return: [<opt> any-value!]
        action [action!]
        block [block!]
        /only
    ][
        frame: make frame! :action
        params: parameters of :action
        using-args: true

        while [block] [
            block: if only [
                arg: block/1
                try next block
            ] else [
                try evaluate/result block (lit arg:)
            ]

            if refinement? params/1 [
                using-args: did set (in frame second params/1) :arg
            ] else [
                if using-args [
                    set (in frame params/1) :arg
                ]
            ]

            params: try next params
        ]

        comment [
            ;
            ; Too many arguments was not a problem for R3-alpha's APPLY, it
            ; would evaluate them all even if not used by the function.  It
            ; may or may not be better to have it be an error.
            ;
            if not tail? block [
                fail "Too many arguments passed in R3-ALPHA-APPLY block."
            ]
        ]

        do frame  ; nulls are optionals
    ]
]

?: emulate [:help]

to-local-file: emulate [
    get/any 'file-to-local  ; not available in web build
]

to-rebol-file: emulate [
    get/any 'local-to-file  ; not available in web build
]

why?: emulate [does [lib/why]]  ; not exported yet, :why not bound

null: emulate [
    make char! 0  ; NUL in Ren-C https://en.wikipedia.org/wiki/Null_character
]

; Ren-C's VOID! is nearly identical to UNSET!, but the concept is that
; *variables* are "unset", not *values*.  So the name is different.  An
; "unset" value is one that holds VOID!.
;
; https://forum.rebol.info/t/947
;
unset!: emulate [:void!]
unset?: emulate [:void?]

; NONE is reserved for `if none [x = 1 | y = 2] [...]`
;
none: emulate [:blank]
none!: emulate [:blank!]
none?: emulate [:blank?]

any-function!: emulate [:action!]
any-function?: emulate [:action?]

native!: emulate [:action!]
native?: emulate [:action?]

function!: emulate [:action!]
function?: emulate [:action?]

; Some of CLOSURE's functionality was subsumed into all FUNCTIONs, but
; the indefinite lifetime of all locals and arguments was not.
; https://forum.rebol.info/t/234
;
closure: emulate [:function]
clos: emulate [:func]

closure!: emulate [:action!]
closure?: emulate [:action?]

true?: emulate [:did?]  ; better name https://trello.com/c/Cz0qs5d7
false?: emulate [:not?]  ; better name https://trello.com/c/Cz0qs5d7

comment: emulate [
    func [
        return: [<opt>] {Not invisible: https://trello.com/c/dWQnsspG}
        :discarded [block! any-string! binary! any-scalar!]
    ][
    ]
]

value?: emulate [
    func [
        {See DEFINED? in Ren-C: https://trello.com/c/BlktEl2M}
        value
    ][
        either any-word? :value [defined? value] [true]  ; bizarre.  :-/
    ]
]

type?: emulate [
    function [
        value [<opt> any-value!]
        /word {Note: SWITCH evaluates https://trello.com/c/fjJb3eR2}
    ][
        case [
            not word [type of :value]
            undefined? 'value ['unset!]  ; https://trello.com/c/rmsTJueg
            blank? :value ['none!]  ; https://trello.com/c/vJTaG3w5
            group? :value ['paren!]  ; https://trello.com/c/ANlT44nH
            (match ['word!] :value) ['lit-word!]
            (match ['path!] :value) ['lit-path!]
        ] else [
            to-word type of :value
        ]
    ]
]

found?: emulate [
    func [
        {See DID and NOT: https://trello.com/c/Cz0qs5d7}
        value
    ][
        not blank? :value
    ]
]

; Note: R3-Alpha had a /PAD option, which was the inverse of /SOME.
; If someone needs it, they can adapt this routine as needed.
;
set: emulate [
    function [
        return: [<opt> any-value!]
        target [blank! any-word! any-path! block! any-context!]
        value [<opt> any-value!]
        /any "Renamed to /OPT, with SET/OPT specialized as SET*"
        /some
    ][
        set_ANY: any
        any: :lib/any

        all [
            not set_ANY
            undefined? 'value
            fail "Can't SET a value to UNSET! unless SET/ANY is used"
        ]

        applique 'set [
            target: either any-context? target [words of target] [target]
            value: :value
            some: some
        ]
    ]
]

get: emulate [
    func [
        {Now no OBJECT! support, unset vars always null}
        return: [<opt> any-value!]
        source {Legacy handles Rebol2 types, not *any* type like R3-Alpha}
            [blank! any-word! any-path! any-context! block!]
        /any
    ][
        let any_GET: any
        any: :lib/any

        if block? :source [
            return source  ; this is what it did :-/
        ]
        let result: either any-context? source [
            get/(any_GET) words of source
        ][
            get/(any_GET) source
        ]
    ]
]

; R3-Alpha and Rebol2's DO was effectively variadic.  If you gave it an
; action, it could "reach out" to grab arguments from after the call.  Ren-C
; replaced this functionality with EVAL:
;
; https://forum.rebol.info/t/meet-the-eval-native/311
;
; !!! This code contains an early and awkward attempt at emulating the old
; DO behavior for functions in userspace, through an early version of
; variadics.  Ren-C is aiming to have functions that make "writing your own
; EVAL-like-thing" easier.
;
do: emulate [
    function [
        return: [<opt> any-value!]
        source [<opt> blank! block! group! text! binary! url! file! tag!
            error! action!
        ]
        normals [any-value! <variadic>]
        'softs [any-value! <variadic>]
        :hards [any-value! <variadic>]
        /args [any-value!]
        /next [word!]
    ][
        var: next
        next: :lib/next

        if var [  ; DO/NEXT
            if args [fail "Can't use DO/NEXT with ARGS"]
            source: evaluate/result :source (lit result:)
            set var source  ; DO/NEXT put the *position* in the var
            return :result  ; DO/NEXT returned the *evaluative result*
        ]

        if action? :source [
            code: reduce [:source]
            params: parameters of :source
            iterate params [
                append code switch type of params/1 [
                    word! [take normals]
                    lit-word! [take softs]
                    get-word! [take hards]
                    set-word! [[]]  ; empty block appends nothing
                    refinement! [break]

                    fail ["bad param type" params/1]
                ]
            ]
            do code
        ] else [
            applique 'do [
                source: :source
                args: :args
            ]
        ]
    ]
]

to: emulate [
    adapt 'to [
        all [
            :value = group!
            find any-word! type
            value: "paren!"  ; make TO WORD! GROUP! give back "paren!"
        ]
        if any-array? :type [
            if match [text! typeset! map! any-context! vector!] :spec [
                return make :type :value
            ]
            if binary? :spec [  ; would scan UTF-8 data
                return make :type as text! :value
            ]
        ]
        ; fallthrough
    ]
]

try: emulate [
    function [
        {See TRAP: https://trello.com/c/IbnfBaLI}
        return: [<opt> any-value!]
        block [block!]
        /except "Note TRAP doesn't take a handler...use THEN instead"
            [block! action!]
    ][
        trap [
            result: do block
        ] then err -> [
            case [
                blank? :except [err]
                block? :except [do except]
                action? :except [try except err]  ; NULL result runs ELSE (!)
            ]
        ] else [
            result
        ]
    ]
]

default: emulate [
    func [
        {See the new enfixed DEFAULT: https://trello.com/c/cTCwc5vX}
        'word [word! set-word! lit-word!]
        value
    ][
        if (undefined? word) or [blank? get word] [
            set word :value
        ] else [
            :value
        ]
    ]
]

also: emulate [
    func [
        {Supplanted by ELIDE: https://trello.com/c/pGhk9EbV}
        return: [<opt> any-value!]
        returned [<opt> any-value!]
        discarded [<opt> any-value!]
    ][
        :returned
    ]
]

parse: emulate [
    function [
        {Non-block rules replaced by SPLIT: https://trello.com/c/EiA56IMR}
        return: [logic! block!]
        input [any-series!]
        rules [block! text! blank!]
        /case
        /all "Ignored refinement in <r3-legacy>"
    ][
        case_PARSE: case
        case: :lib/case

        comment [all_PARSE: all]  ; Not used
        all: :lib/all

        switch type of rules [
            blank! [split input charset reduce [tab space CR LF]]
            text! [split input to-bitset rules]
        ] else [
            if not pos: parse/(case_PARSE) input rules [
                return false
            ]
            return tail? pos
        ]
    ]
]

reduce: emulate [
    function [
        value "Not just BLOCK!s evaluated: https://trello.com/c/evTPswH3"
        /into "https://forum.rebol.info/t/stopping-the-into-virus/705"
            [any-array!]
    ][
        case [
            not block? :value [:value]
            into [insert into reduce :value]
        ] else [
            reduce :value
        ]
    ]
]

enblock-devoid: chain [:devoid | :enblock]

compose: emulate [
    function [
        value "Ren-C composes ANY-ARRAY!: https://trello.com/c/8WMgdtMp"
            [any-value!]
        /deep "Ren-C recurses into PATH!s: https://trello.com/c/8WMgdtMp"
        /only
        /into "https://forum.rebol.info/t/stopping-the-into-virus/705"
            [any-array! any-string! binary!]
    ][
        if not block? :value [return :value]  ; `compose 1` is `1` in Rebol2

        composed: applique 'compose [
            value: :value
            deep: deep

            ; The predicate is a function that runs on whatever is generated
            ; in the COMPOSE'd slot.  If you put it in a block, that will
            ; splice but protect its contents from splicing (the default).
            ; We add the twist that VOID!s ("unset") won't compose in Rebol2.
            ;
            ;    rebol2> type? either true [] []
            ;    == unset!
            ;
            ;    rebol2> compose [(either true [] [])]
            ;    == []  ; would be a #[void] in Ren-C
            ;
            predicate: either only [:enblock-devoid] [:devoid]
        ]

        either into [insert into composed] [composed]
    ]
]

collect: emulate [
    func [
        return: [any-series!]
        body [block!]
        /into "https://forum.rebol.info/t/stopping-the-into-virus/705"
            [any-series!]
    ][
        let out: any [into | make block! 16]

        let keeper: specialize* (
            enclose* 'insert func* [
                f [frame!]
                <with> out
            ][
                f/series: out  ; want new series position capture each time
                :f/value  ; evalutate input before the DO to be return result
                elide out: do f  ; update position on each insertion

                ; original f/value will be returned due to ELIDE
            ]
        )[
            series: <remove-unused-series-parameter>
        ]

        reeval func* compose [(name) [action!] <with> return] body :keeper
        either into [out] [head of out]
    ]
]

; because reduce has been changed but lib/reduce is not in legacy
; mode, this means the repend and join function semantics are
; different.  This snapshots their implementation.

repend: emulate [
    function [
        series [any-series! port! map! object! bitset!]
        value
        /part [any-number! any-series! pair!]
        /only
        /dup [any-number! pair!]
    ][
        ; R3-alpha REPEND with block behavior called out
        ;
        applique 'append/part/dup [
            series: series
            value: (block? :value) and [reduce :value] or [:value]
            part: part
            only: only
            dup: dup
        ]
    ]
]

join: emulate [
    function [
        value
        rest
    ][
        ; double-inline of R3-alpha `repend value :rest`
        ;
        applique 'append [
            series: if series? :value [copy value] else [form :value]
            value: if block? :rest [reduce :rest] else [rest]
        ]
    ]
]

ajoin: emulate [:unspaced]

reform: emulate [:spaced]

redbol-form: form: emulate [
    function [
        value [<opt> any-value!]
        /unspaced "Outer level, append "" [1 2 [3 4]] => {123 4}"
    ][
        case [
            issue? :value [
                as text! value  ; e.g. Rebol2 said `form #<<` was `<<`
            ]
            word? :value [
                as text! value
            ]
            decimal? :value [
                ;
                ; Regarding IEEE `double` values, Wikipedia says:
                ;
                ;    "The 53-bit significand precision gives from 15 to 17
                ;     significant decimal digits precision"
                ;
                ; Rebol2 printed 15 digits after the decimal point.  R3-Alpha gave
                ; 16 digits...as does Red and seemingly JavaScript.
                ;
                ;     rebol2>> 1 / 3
                ;     == 0.333333333333333
                ;
                ;     r3-alpha>> 1 / 3
                ;     == 0.3333333333333333
                ;
                ;     red>> 1 / 3
                ;     == 0.3333333333333333
                ;
                ;     JavaScript> 1 / 3
                ;     -> 0.3333333333333333  ; Chrome
                ;     -> 0.3333333333333333  ; Firefox
                ;
                ; While this may seem a minor issue, generated output in diff
                ; gets thrown off, making it hard to see what has changed.
                ; It can't be addressed via rounding, because rounding
                ; floating point numbers can't guarantee a digit count when
                ; printing--since some numbers aren't evenly representible.
                ;
                ; This truncates the number to the right length but doesn't
                ; round it.  That would be more complicated, and is probably
                ; best done via C code once Redbol is an extension.
                ;
                value: form value
                if not find value "E" [
                    use [pos] [
                        all [
                            pos: skip (try find value ".") 15
                            clear pos
                        ]
                    ]
                ]
                value
            ]
            block? value [
                delimit: either unspaced [:lib/unspaced] [:lib/spaced]
                delimit map-each item value [
                    redbol-form :item
                ]
            ]
            default [
                form value
            ]
        ]
    ]
]

print: emulate [
    func [
        return: <void>
        value [any-value!]  ; Ren-C only takes TEXT!, BLOCK!, BLANK!, CHAR!
    ][
        write-stdout case [
            block? :value [spaced value]
            default [form :value]
        ]
        write-stdout newline
    ]
]

quit: emulate [
    function [
        /return "Ren-C is variadic, 0 or 1 arg: https://trello.com/c/3hCNux3z"
            [<opt> any-value!]
    ][
        applique 'quit [
            value: :return
        ]
    ]
]

does: emulate [
    specialize 'redbol-func [spec: []]
]

has: emulate [
    func [
        return: [action!]
        vars [block!]
        body [block!]
    ][
        redbol-func (head of insert copy vars /local) body
    ]
]

; OBJECT is a noun-ish word; Ren-C tried HAS for a while and did not like it.
; A more generalized version of CONSTRUCT is being considered:
;
; https://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
;
object: emulate [
    specialize 'make [type: object!]
]

construct: emulate [
    func [
        spec [block!]
        /with [object!]
        /only
    ][
        if only [
            fail [
                {/ONLY not yet supported in emulation layer for CONSTRUCT}
                {see %redbol.reb if you're interested in adding support}
            ]
        ]
        to any [with object!] spec
    ]
]

break: emulate [
    func [
        /return "/RETURN is deprecated: https://trello.com/c/cOgdiOAD"
            [any-value!]
    ][
        if return [
            fail [
                "BREAK/RETURN not implemented in Redbol emulation, use THROW"
                "and CATCH.  See https://trello.com/c/uPiz2jLL/"
            ]
        ]
        break
    ]
]

++: emulate [
    func [] [
        fail 'return [
            {++ and -- are not in the Redbol layer by default, as they were}
            {not terribly popular to begin with...but also because `--` is}
            {a very useful and easy-to-type dumping construct in Ren-C, that}
            {comes in very handy when debugging Redbol.  Implementations of}
            {++ and -- are available in %redbol.reb if you need them.}
            {See also ME and MY: https://trello.com/c/8Bmwvwya}
        ]
    ]
]

comment [  ; ^-- see remark above
    ++: emulate [
        function [
            {Deprecated, use ME and MY: https://trello.com/c/8Bmwvwya}
            'word [word!]
        ][
            value: get word  ; returned value
            elide (set word case [
                any-series? :value [next value]
                integer? :value [value + 1]
            ] else [
                fail "++ only works on ANY-SERIES! or INTEGER!"
            ])
        ]
    ]

    --: emulate [
        function [
            {Deprecated, use ME and MY: https://trello.com/c/8Bmwvwya}
            'word [word!]
        ][
            value: get word  ; returned value
            elide (set word case [
                any-series? :value [next value]
                integer? :value [value + 1]
            ] else [
                fail "-- only works on ANY-SERIES! or INTEGER!"
            ])
        ]
    ]
]

compress: emulate [
    function [
        {Deprecated, use DEFLATE or GZIP: https://trello.com/c/Bl6Znz0T}
        return: [binary!]
        data [binary! text!]
        /part [any-value!]
        /gzip
        /only
    ][
        if not any [gzip only] [  ; assume caller wants "Rebol compression"
            data: to-binary copy/part data part
            zlib: zdeflate data

            length-32bit: modulo (length of data) (to-integer power 2 32)
            loop 4 [
                append zlib modulo (to-integer length-32bit) 256
                length-32bit: me / 256
            ]
            return zlib  ; ^-- plus size mod 2^32 in big endian
        ]

        return deflate/part/envelope data :lim [
            gzip [assert [not only] 'gzip]
            not only ['zlib]
        ]
    ]
]

decompress: emulate [
    function [
        {Deprecated, use DEFLATE or GUNZIP: https://trello.com/c/Bl6Znz0T}
        return: [binary!]
        data [binary!] "Red assumes GZIP, Rebol assumed 'Rebol compressed'"
        /part [binary!] "R3-Alpha refinement, must match end of compression"
        /gzip "R3-Alpha refinement (no size argument, envelope stores)"
        /limit [integer!] "R3-Alpha refinement, error if larger"
        /zlib [integer!] "Red refinement (RFC 1951), uncompressed size"
        /deflate [integer!] "Red refinement (RFC 1950), uncompressed size"
    ][
        if not any [gzip zlib deflate] [
            ;
            ; Assume data is "Rebol compressed".  Could get more compatibility
            ; by testing for gzip header or otherwise having a fallback, as
            ; Red went with a Gzip default.
            ;
            return zinflate/part/max data (skip part -4) limit
        ]

        return inflate/part/max/envelope data part limit case [
            gzip [assert [not zlib not deflate] 'gzip]
            zlib [assert [not deflate] 'zlib]
            deflate [_]
            fail
        ]
    ]
]

and: emulate [enfixed :intersect]
or: emulate [enfixed :union]
xor: emulate [enfixed :difference]

; Ren-C NULL means no branch ran, Rebol2 this is communicated by #[none]
; Ren-C #[void] when branch ran w/null result, Rebol2 would call that #[unset]
;
denuller: helper [
    func [action [action!]] [
        chain [
            :action
                |
            func [x [<opt> any-value!]] [
                either-match any-value! get/any 'x [blank]
            ]
        ]
    ]
]

if: emulate [denuller :if]
unless: emulate [denuller adapt 'if [condition: not :condition]]
case: emulate [denuller :case]

switch: emulate [  ; Ren-C evaluates cases: https://trello.com/c/9ChhSWC4/
    enclose (augment 'switch [
        /default "Default case if no others are found"
            [block!]
    ]) func [f [frame!]] [
        f/cases: map-each c f/cases [
            either block? :c [c] [quote c]  ; suppress eval on non-blocks
        ]
        let def: f/default  ; the DO expires frame right now (for safety)
        try (do f else (def))  ; try for BLANK! on failed SWITCH, not null
    ]
]


for-each-nonconst: emulate [
    reskinned [
        body [block!]  ; no <const> annotation
    ] adapt :for-each []  ; see RESKINNED for why this is an ADAPT for now
]

while: emulate [denuller :while]
foreach: emulate [
    function [
        {No SET-WORD! capture, see https://trello.com/c/AXkiWE5Z}
        return: [<opt> any-value!]
        'vars [word! block!]
        data [any-series! any-context! map! blank!]
        body [block!]
    ][
        any [
            not block? vars
            for-each-nonconst item vars [if set-word? item [break] true]
        ] then [
            return for-each-nonconst :vars data body  ; normal FOREACH
        ]

        ; Weird FOREACH, transform to WHILE: https://trello.com/c/AXkiWE5Z
        ;
        use :vars [
            position: data
            while [not tail? position] compose [
                ((collect [
                    for-each item vars [
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
                            fail "non SET-WORD?/WORD? in FOREACH vars"
                        ]
                    ]
                ]))
                ((body))
            ]
        ]
    ]
]

loop: emulate [denuller :loop]
repeat: emulate [denuller :repeat]
forall: emulate [denuller :iterate]
forskip: emulate [denuller :iterate-skip]

any: emulate [denuller :any]
all: emulate [denuller :all]

find: emulate [denuller :find]
select: emulate [denuller :select]
pick: emulate [denuller :pick]

first: emulate [denuller :first]
first+: emulate [
    enclose 'first func [f] [
        use [loc] [
            loc: f/location
            do f
            elide take loc
        ]
    ]
]
second: emulate [denuller :second]
third: emulate [denuller :third]
fourth: emulate [denuller :fourth]
fifth: emulate [denuller :fifth]
sixth: emulate [denuller :sixth]
seventh: emulate [denuller :seventh]
eighth: emulate [denuller :eighth]
ninth: emulate [denuller :ninth]
tenth: emulate [denuller :tenth]

query: emulate [denuller :query]
wait: emulate [denuller :wait]
bind?: emulate [denuller specialize 'of [property: 'binding]]
bound?: emulate [denuller specialize 'of [property: 'binding]]


; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
oldsplicer: helper [
    func [action [action!]] [
        adapt :action [
            all [not only | any-array? series | any-path? :value] then [
                value: as block! value  ; guarantees splicing
            ]

            ; Rebol2 converted integers to their string equivalent when
            ; appending to BINARY!.  R3-Alpha considers INTEGER! to be byte:
            ;
            ;     rebol2> append bin [1234]
            ;     == #{32353731323334}
            ;
            ;     r3-alpha/red> append bin [1234]
            ;     *** Script Error: value out of range: 1234
            ;
            ; It would also spell WORD!s as their Latin1 values.
            ;
            all [
                match [any-string! binary!] series
                (type of series) != (type of :value)  ; changing breaks /PART
            ] then [
                value: redbol-form/unspaced :value
            ]
        ]
    ]
]

append: emulate [oldsplicer :append]
insert: emulate [oldsplicer :insert]
change: emulate [oldsplicer :change]

quote: emulate [:lit]

cloaker: helper [function [  ; specialized as CLOAK and DECLOAK
    {Simple and insecure data scrambler, was native C code in Rebol2/R3-Alpha}

    return: [binary!] "Same series as data"
    decode [logic!] "true if decode, false if encode"
    data [binary!] "Binary series to descramble (modified)"
    key [text! binary! integer!] "Encryption key or pass phrase"
    /with "Use a text! key as-is (do not generate hash)"
][
    if length of data = 0 [return]

    switch type of key [
        integer! [key: to binary! to string! key]  ; UTF-8 string conversion
        text! [key: to binary! key]  ; UTF-8 encoding of string
        binary! []
        fail
    ]

    klen: length of key
    if klen = 0 [
        fail "Cannot CLOAK/DECLOAK with length 0 key"
    ]

    if not with [  ; hash key (only up to first 20 bytes?)
        src: make binary! 20
        count-up i 20 [
            append src key/(1 + modulo (i - 1) klen)
        ]

        key: checksum 'sha1 src
        assert [length of key = 20]  ; size of an SHA1 hash
        klen: 20
    ]

    dlen: length of data

    ; Indexing in this routine doesn't try to get too clever; it uses the
    ; same range as the C but just indexes to `1 +` that.  Anyone who wants
    ; to "optimize" it can also worry about debugging the incompatibilities.
    ; The routines are not used anywhere relevant, AFAIK.

    if decode [
        i: dlen - 1
        while [i > 0] [
            data/(1 + i): data/(1 + i) xor+
                (data/(1 + i - 1) xor+ key/(1 + modulo i klen))
            i: i - 1
        ]
    ]

    ; Change starting byte based all other bytes.

    n: first #{A5}

    ; In the C code this just kept adding to a 32-bit number, allowing 
    ; overflow...then using a C cast to a byte.  Try to approximate this by
    ; just doing the math in modulo 256
    ;
    i: 1
    while [i < dlen] [
        n: modulo (n + data/(1 + i)) 256
        i: i + 1
    ]

    data/1: me xor+ n

    if not decode [
        i: 1
        while [i < dlen] [
            data/(1 + i): data/(1 + i) xor+ 
                (data/(1 + i - 1) xor+ key/(1 + modulo i klen))
            i: i + 1
        ]
    ]

    return data
]]

decloak: emulate [
    redescribe [
        {Decodes a binary string scrambled previously by encloak.}
    ](
        specialize 'cloaker [decode: true]
    )
]

encloak: emulate [
    redescribe [
        {Scrambles a binary string based on a key.}
    ](
        specialize 'cloaker [decode: false]
    )
]


write: emulate [
    adapt (augment 'write [
        /binary "Preserves contents exactly."
        /direct "Opens the port without buffering."
        /no-wait "Returns immediately without waiting if no data."
        /with "Specifies alternate line termination."
            [char! string!]
;        /allow "Specifies the protection attributes when created."
;            [block!]  ; this is still on WRITE, but not implemented (?)
        /mode "Block of above refinements."
            [block!]
        /custom "Allows special refinements."
            [block!]
        /as {(Red) Write with the specified encoding, default is 'UTF-8}
            [word!]
    ]) [
        all [binary? data | not binary] then [
            fail [
                {Rebol2 would do LF => CR LF substitution in BINARY! WRITE}
                {unless you specified /BINARY.  Doing this quietly is a bad}
                {behavior.  Use /BINARY, or WRITE AS TEXT! for conversion.}
            ]
        ]

        for-each w [direct no-wait with part allow mode custom as] [
            if get w [
                fail [unspaced ["write/" w] "not currently in Redbol"]
            ]
        ]
    ]
]

read: emulate [
    enclose (augment 'read [
        /binary "Preserves contents exactly."
        /direct "Opens the port without buffering."
        /no-wait "Returns immediately without waiting if no data."
        /with "Specifies alternate line termination."
            [char! string!]
        /mode "Block of above refinements."
            [block!]
        /custom "Allows special refinements."
            [block!]
        /as {(Red) Read with the specified encoding, default is 'UTF-8}
            [word!]
    ]) func [f [frame!]] [
        for-each w [direct no-wait with part mode custom as] [
            if get compose 'f/(w) [
                fail [unspaced ["write/" w] "not currently in Redbol"]
            ]
        ]

        ; !!! Rebol2 defaulted READ to be TEXT!.  Is Red preserving this?
        ;
        return if f/binary [do f] else [as text! do f]
    ]
]


; Rebol2 was extended ASCII-based, typically expected to be Latin1.  This
; means some files depended on being able to LOAD characters that were
; arbitrary bytes, representing the first 255 characters of unicode.
;
; Red, R3-Alpha, and Ren-C are UTF-8-based by default.  However, this means
; that some Rebol2 scripts which depend on reading Latin1 files will fail.
; One example is %pdf-maker.r, which embeds a Latin1 font metrics file as
; compressed data in the script itself.
;
; It's relatively unlikely that a Latin1 file using high-bit characters would
; decode as valid UTF-8:
;
; "To appear as a valid UTF-8 multi-byte sequence, a series of 2 to 4 extended
;  ASCII 8-bit characters would have to be an unusual combination of symbols
;  and accented letters (such as an accented vowel followed immediately by
;  certain punctuation). In short, real-world extended ASCII character
;  sequences which look like valid UTF-8 multi-byte sequences are unlikely."
;
; So what we do as a heuristic is to try UTF-8 first and fall back on Latin1
; interpretation.  This means bad UTF-8 input that isn't Latin1 will be
; misinterpreted...but since Rebol2 would accept any bytes, it's no worse.
;
hijack 'lib/transcode enclose copy :lib/transcode function [f [frame!]] [
    trap [
        result: lib/do copy f  ; COPY so we can DO it again if needed
    ] then e -> [
        if e/id != 'bad-utf8 [
            fail e
        ]

        f/source: copy f/source
        assert [binary? f/source]  ; invalid UTF-8 can't be in an ANY-STRING!
        pos: f/source
        iterate pos [
            if pos/1 < 128 [continue]  ; ASCII
            if pos/1 < 192 [
                lib/insert pos #{C2}
                pos: next pos
                continue
            ]
            lib/change pos pos/1 - 64  ; want byte not FORM, use LIB/change!
            lib/insert pos #{C3}
            pos: next pos
        ]

        result: lib/do f  ; this time if it fails, we won't TRAP it
    ]
    result
]


call: emulate [  ; brings back the /WAIT switch (Ren-C waits by default)
    get/any 'call*  ; use GET/ANY because not available in web build
]


=== LEAVE AS LAST LINE ===
void  ; so that `do <redbol>` doesn't show any output
