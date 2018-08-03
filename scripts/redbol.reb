REBOL [
    System: "Rebol 3 (Ren-C Branch)"
    Title: "Rebol2 and Red Compatibility Shim"
    Homepage: https://trello.com/b/l385BE7a/porting-guide
    Rights: {
        Copyright 2012-2018 Rebol Open Source Contributors
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
        If you do not want to have locked source, there is currently an
        option in the debug build: `system/options/unlocked-source`, which
        you can set to "false".
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
    print ["REGISTERING EMULATION EXPORT:" as word! set-word]
]

helper: enfix lib/func [
    return: <void>
    :set-word [set-word!]
    code [block!]
] lib/in lib [
    set set-word do in lib code
]

emulate: enfix lib/func [
    return: <void>
    :set-word [set-word!]
    code [block!]
] lib/in lib [
    set set-word do in lib code
    export set-word
]

emulate-enfix: enfix lib/func [
    return: <void>
    :set-word [set-word!]
    code [block!]
] lib/in lib [
    set/enfix set-word do in lib code
    export set-word
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

any-type!: emulate [any-value!] ;-- !!! does not include any "UNSET!"

any-block!: emulate [any-array!]
any-block?: emulate [:any-array?]

any-object!: emulate [any-context!]
any-object?: emulate [:any-context?]


; Rebol2 missing refinements are #[none], or #[true] if present
; Red missing refinements are #[false], or #[true] if present
; Rebol2 and Red arguments to unused refinements are #[none]
;
; Ren-C missing refinements are a BLANK! (_), or REFINEMENT! w/name if present
; Ren-C arguments to unused refinements are not set.
;
; This is userspace code which transforms a FRAME! from the Ren-C convention
; once the function is running, but before the user code composed into its
; body gets a chance to run.  It uses the Rebol2 #[none] convention, as it
; is more consistent with the Ren-C blank.
;
blankify-refinement-args: helper [
    function [return: <void> f [frame!]] [
        seen-refinement: false
        for-each w (words of action-of f) [
            case [
                refinement? w [
                    seen-refinement: true
                    if f/(to-word w) [
                        f/(to-word w): true ;-- turn REFINEMENT! into #[true]
                    ]
                ]
                seen-refinement [ ;-- turn any null args into BLANK!s
                    ;
                    ; !!! This is better expressed as `: default [_]`, but
                    ; DEFAULT is based on using SET, which disallows GROUP!s
                    ; in PATH!s.  Review rationale and consequences.
                    ;
                    f/(to-word w): to-value :f/(to-word w)
                ]
            ]
        ]
    ]
]

; This transforms a function spec so that UNSET! becomes <opt>, since there is
; no "UNSET! data type" (null is not a value, and has no type).  It also must
; transform `any-type!` into `<opt> any-value!`, as in Rebol2 ANY-TYPE!
; implied not being set was acceptable...but UNSET! is not a type and hence
; not in the types implied by ANY-VALUE!.
;
optify: helper [
    function [spec [block!]] [
        ; R3-Alpha would tolerate blocks in the first position, but didn't do
        ; the Rebol2 feature, e.g. `func [[throw catch] x y][...]`.
        ;
        if block? first spec [spec: next spec] ;-- skip Rebol2's [throw]

        map-each item spec [
            case [
                :item = [any-type!] [
                    [<opt> any-value!]
                ]
                block? :item and (find item 'unset!) [
                    replace (copy item) 'unset! <opt>
                ]
                default [:item]
            ]
        ]
    ]
]

func: emulate [
    function [
        return: [action!]
        spec [block!]
        body [block!]
    ][
        func compose [
           (optify spec) <local> exit
        ] compose [
            blankify-refinement-args binding of 'return
            exit: make action! [[] [unwind binding of 'return]]
            (body)
        ]
    ]
]

function: emulate [
    function [
        return: [action!]
        spec [block!]
        body [block!]
        /with object [object! block! map!]
        /extern words [block!]
    ][
        if block? :object [object: has object]

        ; The shift in Ren-C is to remove the refinements from FUNCTION, and
        ; put everything into the spec...marked with <tags>
        ;
        function compose [
            (optify spec)
            (with ?? <in>) (:object) ;-- <in> replaces functionality of /WITH
            (extern ?? <with>) (:words) ;-- <with> took over what /EXTERN was
            ;-- <local> exit, picked up since using FUNCTION as generator
        ] compose [
            blankify-refinement-args binding of 'return
            exit: make action! [[] [unwind binding of 'return]]
            (body)
        ]
    ]
]

apply: emulate [:applique]

?: emulate [:help]

to-local-file: emulate [:file-to-local]

to-rebol-file: emulate [:local-to-file]

why?: emulate [does [lib/why]] ;-- not exported yet, :why not bound

??: emulate [:dump]

null: emulate [
    #"^@" ; NUL in Ren-C https://en.wikipedia.org/wiki/Null_character
]

unset?: emulate [:null?] ; https://trello.com/c/shR4v8tS
unset!: emulate [:null] ;-- Note: datatype? unset! will fail with this

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

true?: emulate [:did?] ;-- better name https://trello.com/c/Cz0qs5d7
false?: emulate [:not?] ;-- better name https://trello.com/c/Cz0qs5d7

comment: emulate [
    func [
        return: [<opt>] {Not invisible: https://trello.com/c/dWQnsspG}
        :discarded [block! any-string! binary! any-scalar!]
    ][
    ]
]

value?: emulate [
    func [
        {See SET? in Ren-C: https://trello.com/c/BlktEl2M}
        value
    ][
        either any-word? :value [set? value] [true] ;; bizarre.  :-/
    ]
]

type?: emulate [
    function [
        value [<opt> any-value!]
        /word {Note: SWITCH evaluates https://trello.com/c/fjJb3eR2}
    ][
        case [
            not word [type of :value]
            unset? 'value [quote unset!] ;-- https://trello.com/c/rmsTJueg
            blank? :value [quote none!] ;-- https://trello.com/c/vJTaG3w5
            group? :value [quote paren!] ;-- https://trello.com/c/ANlT44nH
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

        apply 'set [
            target: either any-context? target [words of target] [target]
            value: :value
            some: some
            opt: set_ANY
        ]
    ]
]

get: emulate [
    function [
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
        set* quote result: either any-context? source [
            get words of source
        ][
            get source
        ]
        if not any_GET and (null? :result) [
            fail "Legacy GET won't get an unset variable without /ANY"
        ]
        return :result
    ]
]

; R3-Alpha and Rebol2's DO was effectively variadic.  If you gave it
; an action, it could "reach out" to grab arguments from after the
; call.  While Ren-C permits this in variadic actions, the system
; natives should be "well behaved".
;
; https://trello.com/c/YMAb89dv
;
; This legacy bridge is variadic to achieve the result.
;
do: emulate [
    function [
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

        if next_DO [
            if args [fail "Can't use DO/NEXT with ARGS"]
            source: evaluate :source quote result:
            if var [set var source] ;-- DO/NEXT put the *position* in the var
            return :result ;-- DO/NEXT returned the *evaluative result*
        ]

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
    ]
]

to: emulate [
    adapt 'to [
        if :value = group! and (find any-word! type) [
            value: "paren!" ;-- make TO WORD! GROUP! give back "paren!"
        ]
        if any-array? :type [
            if match [text! typeset! map! any-context! vector!] :spec [
                return make :type :value
            ]
            if binary? :spec [ ;-- would scan UTF-8 data
                return make :type as text! :value
            ]
        ]
        ;--fallthrough
    ]
]

try: emulate [
    func [
        return: [<opt> any-value!]
        block [block!]
        /except {TRAP/WITH is better: https://trello.com/c/IbnfBaLI}
        code [block! action!]
    ][
        trap/(except ?? 'with !! _) block :code
    ]
]

default: emulate [
    func [
        {See the new enfixed DEFAULT: https://trello.com/c/cTCwc5vX}
        'word [word! set-word! lit-word!]
        value
    ][
        if unset? word or (blank? get word) [
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
            parse/(case_PARSE ?? 'case !! _) input rules
        ]
    ]
]

reduce: emulate [
    function [
        value "Not just BLOCK!s evaluated: https://trello.com/c/evTPswH3"
        /into "https://forum.rebol.info/t/stopping-the-into-virus/705"
        out [any-array!]
    ][
        case [
            not block? :value [:value]
            into [insert out reduce :value]
        ] else [
            reduce :value
        ]
    ]
]

compose: emulate [
    function [
        value "Ren-C composes ANY-ARRAY!: https://trello.com/c/8WMgdtMp"
            [any-value!]
        /deep "Ren-C recurses into PATH!s: https://trello.com/c/8WMgdtMp"
        /only
        /into "https://forum.rebol.info/t/stopping-the-into-virus/705"
        out [any-array! any-string! binary!]
    ][
        case [
            not block? value [:value]
            into [
                insert out apply 'compose [
                    value: :value
                    deep: deep
                    only: only
                ]
            ]
        ] else [
            apply 'compose [
                value: :value
                deep: deep
                only: only
            ]
        ]
    ]
]

collect: emulate [
    func [
        return: [any-series!]
        body [block!]
        /into "https://forum.rebol.info/t/stopping-the-into-virus/705"
        output [any-series!]
        <local> keeper
    ][
        output: default [make block! 16]

        keeper: specialize (
            enclose 'insert function [
                f [frame!]
                <static> o (:output)
            ][
                f/series: o
                o: do f ;-- update static's position on each insertion
                :f/value
            ]
        )[
            series: <remove-unused-series-parameter>
        ]

        eval func compose [(name) [action!] <with> return] body :keeper
        either into [output] [head of output]
    ]
]

; because reduce has been changed but lib/reduce is not in legacy
; mode, this means the repend and join function semantics are
; different.  This snapshots their implementation.

repend: emulate [
    function [
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
            value: block? :value and [reduce :value] !! :value
            if part [limit: :limit]
            only: only
            if dup [count: :count]
        ]
    ]
]

join: emulate [
    function [
        value
        rest
    ][
        ;-- double-inline of R3-alpha `repend value :rest`
        ;
        apply 'append [
            series: if series? :value [copy value] else [form :value]
            value: if block? :rest [reduce :rest] else [rest]
        ]
    ]
]

ajoin: emulate [:unspaced]

reform: emulate [:spaced]

; To be on the safe side, the PRINT in the box won't do evaluations on
; blocks unless the literal argument itself is a block
;
print: emulate [specialize 'print [eval: true]]

quit: emulate [
    function [
        /return {use /WITH in Ren-C: https://trello.com/c/3hCNux3z}
        value
    ][
        apply 'quit [
            with: ensure [refinement! blank!] return
            if return [value: :value]
        ]
    ]
]

does: emulate [
    func [
        return: [action!]
        :code [group! block!]
    ][
        func [<local> return:] compose/only [
            return: does [
                fail "No RETURN from DOES: https://trello.com/c/KgwJRlyj"
            ]
            | (as group! code)
        ]
    ]
]

; In Ren-C, HAS is the arity-1 parallel to OBJECT as arity-2 (similar
; to the relationship between DOES and FUNCTION).  In Rebol2 and
; R3-Alpha it just broke out locals into their own block when they
; had no arguments.
;
has: emulate [
    func [
        return: [action!]
        vars [block!]
        body [block!]
    ][
        r3-alpha-func (head of insert copy vars /local) body
    ]
]

construct: emulate [
    func [
        {CONSTRUCT is arity-2 object constructor}
        spec [block!]
        /with object [object!]
        /only
    ][
        if only [
            fail [
                {/ONLY not yet supported in emulation layer for CONSTRUCT}
                {see %redbol.reb if you're interested in adding support}
            ]
        ]
        object: default [object!]
        to object spec
    ]
]

break: emulate [
    func [
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
    ]
]

++: emulate [
    function [
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
    ]
]

--: emulate [
    function [
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
    ]
]

compress: emulate [
    function [
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
    ]
]

decompress: emulate [
    function [
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
    ]
]

and: emulate-enfix [tighten :intersect]
or: emulate-enfix [tighten :union]
xor: emulate-enfix [tighten :difference]

; Ren-C NULL means no branch ran, Rebol2 this is communicated by #[none]
; Ren-C #[void] when branch ran w/null result, Rebol2 would call that #[unset]
;
devoider: helper [
    func [action [action!]] [
        chain [
            :action
                |
            func [x [<opt> any-value!]] [
                if null? :x [return blank] ;-- "none"
                if void? :x [return null] ;-- "unset"
                :x
            ]
        ]
    ]
]

if: emulate [devoider :if]
unless: emulate [devoider :if-not]
case: emulate [devoider :case]
switch: emulate [redescribe [
    {Ren-C SWITCH evaluates matches: https://trello.com/c/9ChhSWC4/}
](
    chain [
        adapt 'switch [
            cases: collect [
                for-each c cases [
                    keep/only either block? :c [:c] [uneval :c]
                ]
                if default [ ;-- convert to fallout
                    keep/only as group! default-branch
                    default: false
                    unset 'default-branch
                ]
            ]
        ]
            |
        :to-value ;-- wants blank on failed SWITCH, not null
    ]
)]


while: emulate [devoider :while]
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
            for-each item vars [if set-word? item [break] true]
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
    ]
]

loop: emulate [devoider :loop]
repeat: emulate [devoider :repeat]
forall: emulate [devoider :for-next]
forskip: emulate [devoider :for-skip]

any: emulate [devoider :any]
all: emulate [devoider :all]

find: emulate [devoider :find]
select: emulate [devoider :select]
pick: emulate [devoider :pick]

first: emulate [devoider :first]
first+: emulate [
    enclose 'first func [f] [
        use [loc] [
            loc: f/location
            do f
            elide take loc
        ]
    ]
]
second: emulate [devoider :second]
third: emulate [devoider :third]
fourth: emulate [devoider :fourth]
fifth: emulate [devoider :fifth]
sixth: emulate [devoider :sixth]
seventh: emulate [devoider :seventh]
eighth: emulate [devoider :eighth]
ninth: emulate [devoider :ninth]
tenth: emulate [devoider :tenth]

query: emulate [devoider :query]
wait: emulate [devoider :wait]
bind?: emulate [devoider specialize 'of [property: 'binding]]
bound?: emulate [devoider specialize 'of [property: 'binding]]


; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
oldsplicer: helper [
    func [action [action!]] [
        adapt :action [
            all [not only | any-array? :series | any-path? :value] then [
                value: as block! value ;-- guarantees splicing
            ]
        ]
    ]
]

append: emulate [oldsplicer :append]
insert: emulate [oldsplicer :insert]
change: emulate [oldsplicer :change]
