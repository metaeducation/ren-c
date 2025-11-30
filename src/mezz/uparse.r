Rebol [
    title: "UPARSE: Usermode Implementation of Evolved PARSE Dialect"
    license: "LGPL 3.0"

    type: module
    name: Usermode-PARSE

    exports: [
        combinator
        parse
        default-combinators
        parse-trace-hook
        using  ; TBD: will become new meaning of USE
    ]

    description: --[
        Rebol's PARSE is a tool for performing RegEx-style tasks using an
        English-like dialect.  It permits breaking down complex expressions
        into named subrules, and has a very freeform model for escaping into
        and back out of imperative code:

        http://www.rebol.com/docs/core23/rebolcore-15.html
        http://www.codeconscious.com/rebol/parse-tutorial.html
        https://www.red-lang.org/2013/11/041-introducing-parse.html

        The implementation of PARSE engines has traditionally been as
        optimized systems-level code (e.g. C or Red/System), built into the
        interpreter.  This does not offer flexibility to make any minor or
        major changes to the dialect that a user might imagine.

        This script attempts to make PARSE more "hackable", by factoring the
        implementation out so that each keyword or data behavior is handled
        by individual functions.  These functions are stylized so that th
        parse engine can compose these smaller parsers together as part of
        larger processing operations.  This approach is typically called
        "Parser Combinators":

        https://en.wikipedia.org/wiki/Parser_combinator

        While this overlaps with typical parser combinators, Rebol's design
        affords it a unique spin.  By building the backbone of the dialect as
        a BLOCK!, there's no SEQUENCE or ALTERNATIVE combinators.  Instead,
        blocks make sequencing implicit just by steps being ordered after one
        another.  The alternates are managed by means of `|` markers, which
        are detected by the implementation of the block combinator--and `|` is
        not a combinator in its own right.  With novel operators and convenient
        ways of switching into imperative processing, it gets a unique and
        literate feel with a relatively clean appearance.

        By making the combinator list passed to UPARSE as a MAP!, is possible
        to easily create overrides or variations of the dialect.  (For
        instance, a version that is compatible with older Rebols.)  But the
        goal is to facilitate even more ambitious features.
    ]--

    notes: --[
      * This implementation will be *extremely* slow for the foreseeable
        future.  But since it is built on usermode facilities, optimizations
        that are applied to it will bring systemic benefits.  Ultimately the
        goal is to merge this architecture in with the "messier" C code...
        hopefully preserving enough of the hackability while leveraging
        low-level optimizations where possible.
    ]--
]


; All combinators receive the INPUT to be processed.  They are also given an
; object representing the STATE of the parse (currently that is just the
; FRAME! of the main UPARSE call which triggered the operation, so any of the
; locals or parameters to that can be accessed, e.g. the list of :COMBINATORS)
;
; The goal of a combinator is to decide whether to match (by returning a
; synthesized value and the updated position in the series) or fail to match
; (by returning an antiform error)
;
; Additional parameters to a combinator are fulfilled by the parse engine by
; looking at the ensuing rules in the rule block.
;
; One of the parameter types that can be given to these functions are another
; parser, to combine them (hence "parser combinator").  So you can take a
; combinator like OPT and parameterize it with SOME which is parameterized
; with "A", to get a composite parser that implements [opt some "a"].
;
; But if a parameter to a combinator is marked as quoted, then that will take
; a value from the callsite literally.
;
; A special COMBINATOR generator is used.  This saves on repetition of
; parameters and also lets the engine get its hooks into the execution of
; parsers...for instance to diagnose the furthest point the parsing reached.
;
; !!! COMBINATOR is used so heavily that having it be usermode makes UPARSE
; prohibitively slow.  The ultimate goal is to have native and usermode
; implementations both exist, with the usermode version being swappable in as
; proof of concept that nothing is being done by the native that the user
; could not have done...just more slowly.
;
; We would like it to be possible to write an "easy" combinator that does not
; do piping of the pending elements, but takes it for granted as coming from
; the success or failure of each parser called in order.  If you formally
; declare a return value for the `pending:` then that means you're going to
; manage it yourself, otherwise it will be automagic.

combinator: func [
    "Make stylized code that fulfills the interface of a combinator"

    return: [frame!]

    spec [block!]
    body [block!]
]
bind construct [
    ^wrapper: lambda [  ; ^wrapper to not store WRAPPER as label
        "Enclosing function for hooking all combinators"
        f [frame!]
    ][
        either f.state.hook [
            run f.state.hook f
        ][
            eval-free f
        ]
    ]
][
    let autopipe: ~
    let input-name: ~  ; whatever they called INPUT

    let ^action: func compose [  ; ^action to not store ACTION as label
        ; Get the text description if given
        (? if text? spec.1 [spec.1, elide spec: my next])

        ; Enforce a RETURN: definition.  RETURN: [...] is allowed w/no text
        (
            assert [spec.1 = 'return:]
            let [description types]: if block? spec.2 [  ; no description
                pack [null spec.2]
                elide spec: my skip 2
            ] else [
                pack [ensure text! spec.2, ensure block! spec.3]
                elide spec: my skip 3
            ]
            spread compose:deep [
                return: (opt description)
                    [~[(types) any-series? [blank? block!]]~]
            ]
        )

        state [frame!]

        (
            input-name: spec.1
            assert [word? input-name]  ; can call it almost anything you want
            let [description types]: if block? spec.2 [  ; no description
                pack [null spec.2]
                elide spec: my skip 2
            ] else [
                pack [ensure text! spec.2, ensure block! spec.3]
                elide spec: my skip 3
            ]
            assert [types = [any-series?]]  ; enforce for now...
            spread reduce [
                input-name, opt description, types
            ]
        )

        (if ':pending = try spec.1 [
            assert [spec.2 = [blank? block!]]
            autopipe: 'no  ; they're asking to handle pending themselves
            spread reduce [':pending spec.2]
            elide spec: my skip 2
        ] else [
            autopipe: 'yes  ; they didn't mention pending, handle automatically
            spread [:pending [blank? block!]]
        ])

        ; Whatever arguments the combinator takes, if any
        ;
        (spread spec)
    ] compose [
        ;
        ; !!! If we are "autopipe" then we need to make it so the parsers that
        ; we receive in will automatically bubble up their pending contents in
        ; order of being called.

        (spread when yes? autopipe '[
            let f: binding of $return

            pending: blank
            let in-args: null
            for-each [key ^val] f [
                if not in-args [
                    if key = input-name [in-args: okay]
                    continue
                ]
                all [
                    not unset? $val
                    action? opt ^val
                ] then [
                    ; All parsers passed as arguments, we want it to be
                    ; rigged so that their results append to an aggregator in
                    ; the order they are run (if they succeed).
                    ;
                    /f.(key): enclose (augment ^val [:modded]) func [
                        f2
                        <local> ^result remainder subpending
                    ][
                        [^result remainder subpending]: trap eval-free f2

                        pending: glom pending spread subpending
                        return pack [^result remainder subpending]
                    ]
                ]
            ]
        ])

        return: lambda [^value] compose2:deep '{} [  ; already composing w/()
            {unrun return/} pack [
                ^value except e -> [
                    {unrun return/} fail e
                ]
                {input-name}
                pending  ; can't change PENDING name at this time
            ]
        ]

        ; ** Currently parsers unify RETURN where a failure is done with
        ; a `return fail`.  Should this instead be ACCEPT and REJECT, as
        ; local functions to the combinator, so that you can ACCEPT a failure
        ; from something like a GROUP! (and remove need for RAISE with the
        ; return?)  Or RETURN ACCEPT and RETURN REJECT to make it clearer,
        ; where ACCEPT makes a pack and REJECT does RAISE?

        eval overbind binding of $return (body)

        ; If the body does not return and falls through here, the function
        ; will fail as it has a RETURN: that needs to be used to type check
        ; (and must always be used so all return paths get covered if hooked)
    ]

    ; Enclosing with the wrapper permits us to inject behavior before and
    ; after each combinator is executed.
    ;
    return unrun enclose augment action/ [
        :rule-start [block!] :rule-end [block!]
    ] wrapper/  ; returns plain ACTION!
]


; It should be possible to find out if something is a combinator in a more
; rigorous way than this.  But just check the parameters for now.
;
; !!! Used to check for INPUT and STATE, but then combinators were allowed
; to call their inputs whatever they wanted, so just checks STATE.
;
combinator?: func [
    "Crude test to try and determine if an ACTION! is a combinator"
    return: [logic?]
    frame [<unrun> frame!]
    <local> keys
][
    return did all [has frame 'state]
]

negatable-parser?: func [
    return: [logic?]
    frame [<unrun> frame!]
][
    return did find words of frame 'negated
]

; An un-lens'd combinator has the input in position 2, but we don't know
; what name it was given.  Help document places that assume it's position 2.
;
set-combinator-input: lambda [f input] [
    f.2: input  ; f.1 is STATE, f.2 is whatever they called INPUT
]

block-combinator: ~  ; need in variable for recursion implementing "..."
span-combinator: ~  ; for doing TWO, THREE, FOUR, FIVE...

; !!! We use a MAP! here instead of an OBJECT! because if it were MAKE OBJECT!
; then the parse keywords would override the Rebol functions (so you couldn't
; use ANY during the implementation of a combinator, because there's a
; combinator named ANY).  This is part of the general issues with binding that
; need to have answers.
;
default-combinators: make map! [

    === NOT COMBINATOR ===

    ; Historical Redbol PARSE considered NOT a synonym for NOT AHEAD.  But
    ; the concept behind UPARSE's NOT is that some parsers can be "negated",
    ; which allows them to actually advance and consume input:
    ;
    ;     >> parse ["a"] [not ahead integer!, text!]
    ;     == "a"
    ;
    ;     >> parse ["a"] [not integer!]
    ;     == "a"
    ;
    ;     >> parse ["a"] [not some integer!]
    ;     ** Error: SOME combinator cannot be negated with NOT
    ;
    ; The approach has some weaknesses, e.g. the BLOCK! combinator isn't
    ; negatable so `not [integer!]` isn't legal but `not integer!` is.  But
    ; the usefulness is high enough that it's believed worth it.

    'not combinator [
        "If the parser argument is negatable, invoke it in the negated sense"
        return: [any-value?]
        input [any-series?]
        parser [action!]
        :negated
    ][
        if negated [  ; NOT NOT, e.g. call parser without negating it
            return [{^} input]: trap parser input
        ]
        if not negatable-parser? parser/ [
            panic "NOT called on non-negatable combinator"
        ]
        return [{^} input]: trap parser:negated input
    ]

    === CONDITIONAL COMBINATOR ===

    'conditional combinator [
        "If parser's synthesized result is null, skip to next alternate"
        return: [any-stable?]
        input [any-series?]
        parser [action!]
        <local> result
    ][
        [^result input]: trap parser input  ; non-matching parser, no match

        if void? ^result [
            panic "CONDITIONAL combinator received VOID antiform result"
        ]
        if null? ^result [
            return fail "CONDITIONAL combinator received NULL antiform result"
        ]
        return ^result  ; can say e.g. [x: cond (next var)] if you like
    ]

    === VETO COMBINATOR ===

    'veto combinator [
        "Stop the current rule chain, and skip to the next `|` alternate"
        return: [<divergent>]
        input [any-series?]
        ; :negated  <- could this be negated to not veto?
    ][
        return fail "VETO to next alternate rule requested"
    ]

    === BASIC KEYWORDS ===

    'try combinator [
        "If parser fails, succeed and return NULL; don't advance input"
        return: "PARSER's result if it succeeds, otherwise NULL"
            [any-value?]
        input [any-series?]
        parser [action!]
        <local> ^result remainder
    ][
        [^result remainder]: parser input except e -> [
            return null  ; succeed w/null on parse fail, don't advance input
        ]
        input: remainder  ; advance input
        return ^result  ; return successful parser result
    ]

    'optional combinator [
        "If parser fails, succeed and return VOID; don't advance input"
        return: "PARSER's result if it succeeds, otherwise VOID"
            [any-value?]
        input [any-series?]
        parser [action!]
        <local> ^result remainder
    ][
        [^result remainder]: parser input except e -> [
            return ^void  ; succeed w/void on parse fail, don't advance input
        ]
        input: remainder  ; advance input
        return ^result  ; return successful parser result
    ]

    'spread combinator [
        "Return antiform group for list arguments"
        return: "Splice antiform if input is list"
            [<null> <void> element? splice!]
        input [any-series?]
        parser [action!]
        <local> ^result
    ][
        [^result input]: trap parser input

        if (not any-list? ^result) [
            panic "SPREAD only accepts ANY-LIST? and QUOTED!"
        ]
        return spread ^result
    ]

    'ahead combinator [
        "Leave the parse position at the same location, but fail if no match"
        return: "parser result if success, NULL if failure"
            [any-value? ~#not~]
        input [any-series?]
        parser [action!]
        :negated
    ][
        if negated [
            parser input except e -> [
                return ~#not~
            ]
            return fail "Negated parser passed to AHEAD succeded"
        ]
        return [{^}]: parser input  ; don't care about parser's remainder
    ]

    'further combinator [
        "Pass through the result only if the input was advanced by the rule"
        return: "parser result if it succeeded and advanced input, else NULL"
            [any-value?]
        input [any-series?]
        parser [action!]
        <local> ^result pos
    ][
        [^result pos]: trap parser input  ; TRAP propagates fail if no match

        if (index of pos) <= (index of input) [
            return fail "FURTHER rule matched, but did not advance the input"
        ]
        input: pos
        return ^result
    ]

    === LOOPING CONSTRUCT KEYWORDS ===

    ; UPARSE uses SOME as a loop operator, with TRY SOME or TRY FURTHER SOME
    ; taking the place of both ANY and WHILE.  ANY is now used for more fitting
    ; semantics for the word, and WHILE is reclaimed as an arity-2 construct.
    ; The progress requirement that previously made ANY different from WHILE is
    ; achieved with FURTHER:
    ;
    ; https://forum.rebol.info/t/1540/12
    ;
    ; Note that TALLY can be used as a substitute for WHILE if the rule product
    ; isn't of interest.
    ;
    ; https://forum.rebol.info/t/1581/2

    'some combinator [
        "Run the parser argument in a loop, requiring at least one match"
        return: "Result of last successful match"
            [any-value?]
        input [any-series?]
        parser [action!]
        <local> ^result
    ][
        append state.loops binding of $return

        [^result input]: parser input except e -> [
            take:last state.loops
            return fail e
        ]
        cycle [  ; if first try succeeds, we'll succeed overall--keep looping
            [^result input]: parser input except [
                take:last state.loops
                return ^result
            ]
        ]
        panic ~#unreachable~
    ]

    'while combinator [
        "Run the body parser in a loop, for as long as condition matches"
        return: "Result of last body parser (or void if body never ran)"
            [any-value?]
        input [any-series?]
        condition-parser [action!]
        body-parser [action!]
        <local> ^result
    ][
        append state.loops binding of $return

        ^result: ^void

        cycle [
            [^ input]: condition-parser input except [
                take:last state.loops
                return ^result
            ]

            ; We don't worry about the body parser's success or failure, but
            ; if it fails we disregard the result
            ;
            [^result input]: body-parser input except [
                ^result: ^void
                continue
            ]
        ]
        panic ~#unreachable~
    ]

    'until combinator [
        "Run the body parser in a loop, until the condition matches"
        return: "Result of last body parser (or void if body never ran)"
            [any-value?]
        input [any-series?]
        condition-parser [action!]
        body-parser [action!]
        <local> ^result
    ][
        append state.loops binding of $return

        ^result: ^void

        cycle [
            [^ input]: condition-parser input except [
                ;
                ; We don't worry about the body parser's success or failure,
                ; but if it fails we disregard the result
                ;
                [^result input]: body-parser input except [
                    ^result: ^void
                    continue
                ]
                continue
            ]
            take:last state.loops
            return ^result
        ]
        panic ~#unreachable~
    ]

    'cycle combinator [
        "Run the body parser continuously in a loop until BREAK or STOP"
        return: "Result of last body parser (or void if body never matched)"
            [any-value?]
        input [any-series?]
        parser [action!]
        <local> ^result
    ][
        append state.loops binding of $return

        ^result: ^void

        cycle [
            [^result input]: parser input except [
                ^result: ~
                continue
            ]
        ]
        panic ~#unreachable~
    ]

    'tally combinator [
        "Iterate a rule and count the number of times it matches"
        return: "Number of matches (can be 0)"
            [integer!]
        input [any-series?]
        parser [action!]
        <local> count
    ][
        append state.loops binding of $return

        count: 0
        cycle [
            [^ input]: parser input except [
                take:last state.loops
                return count
            ]
            count: count + 1
        ]
        panic ~#unreachable~
    ]

    'break combinator [
        "Break an iterated construct like SOME or REPEAT, failing the match"
        return: [<divergent>]
        input [any-series?]
        <local> f
    ][
        f: take:last state.loops except [
            panic "No PARSE iteration to BREAK"
        ]

        f.remainder: input
        f/return fail "BREAK encountered"
    ]

    'stop combinator [
        "Break an iterated construct like SOME or REPEAT, succeeding the match"
        return: [<divergent>]
        input [any-series?]
        parser [<end> action!]
        <local> f ^result
    ][
        ^result: ~  ; default `[stop]` returns trash (tripwire)
        if not unset? $parser [  ; parser argument is optional
            [^result input]: trap parser input
        ]

        f: take:last state.loops except [
            panic "No PARSE iteration to STOP"
        ]

        set-combinator-input f input  ; always set remainder to stop's input?
        f/return ^result
    ]

   === ACCEPT KEYWORD ===

    ; ACCEPT is like R3-Alpha's RETURN keyword, but with the name adjusted to
    ; help distinguish it from the overall return of a function (as well as cue
    ; the reader it is not unconditional, e.g. you can say something like
    ; `[accept integer! | ...]`).
    ;
    ; RETURN was removed for a time in Ren-C due to concerns about how it
    ; could lead to abruptly ending a parse before all the matching was
    ; complete.  Now UPARSE can return ANY-STABLE? (or PACK?) and the only
    ; reason you'd ever use ACCEPT would be specifically for the abrupt exit,
    ; so it's fit for purpose.

    'accept combinator [
        "Return a value explicitly from the parse, terminating early"
        return: [<divergent>]
        input [any-series?]
        parser [action!]
        <local> ^value
    ][
        [^value _]: trap parser input  ; returning, don't need to update input

        ; !!! STATE is filled in as the frame of the top-level UPARSE call.  If
        ; we UNWIND then we bypass any of its handling code, so it won't set
        ; things like PENDING (which gets type checked as a multi-return, so
        ; we can't leave it as unset).  Review.
        ;
        pending: blank
        state/return pack [^value pending]
    ]

    === INDEX and MEASUREMENT COMBINATORS ===

    ; The historical pattern:
    ;
    ;     s: <here>, some rule, e: <here>, (len: (index of e) - (index of s))
    ;
    ; Can be done more conveniently with the <index> tag combinator:
    ;
    ;     s: <index>, some rule, e: <index>, (len: e - s)
    ;
    ; But even more conveniently with the MEASURE combinator:
    ;
    ;     len: measure some rule
    ;
    ; Note this is distinct from TALLY, which is an iterative construct that
    ; counts the number of times it can match the rule it is given:
    ;
    ;     >> parse "ababab" [tally "ab"]
    ;     == 3
    ;
    ;     >> parse "ababab" [measure some "ab"]
    ;     == 6

    <index> combinator [
        "Get the current series index of the PARSE operation"
        return: "The INDEX OF the parse position"
            [integer!]
        input [any-series?]
    ][
        return index of input
    ]

    'measure combinator [
        "Get the length of a matched portion of content"
        return: "Length in series units"
            [integer!]
        input [any-series?]
        parser [action!]
        <local> start end remainder
    ][
        [^ remainder]: trap parser input

        end: index of remainder
        start: index of input

        ; Because parse operations can SEEK, this can potentially create
        ; issues.  We currently fail if the index is before (return negative?)
        ;
        if start > end [
            panic "Can't MEASURE region if rules SEEK'd before the INPUT"
        ]

        input: remainder  ; advance the input now that difference is measured
        return end - start
    ]

    === MUTATING KEYWORDS ===

    ; Topaz moved away from the idea that PARSE was used for doing mutations
    ; entirely.  It does complicate the implementation to be changing positions
    ; out from under things...so it should be considered carefully.
    ;
    ; UPARSE continues with the experiment, but does things a bit differently.
    ; Here CHANGE is designed to be used with value-bearing rules, and the
    ; value-bearing rule is run against the same position as the start of
    ; the input.
    ;
    ; !!! Review what happens if the input rule can modify, too.

    'change combinator [
        "Substitute a match with new data"
        return: [trash!]
        input [any-series?]
        parser [action!]
        replacer [action!]  ; !!! How to say result is used here?
        <local> ^replacement remainder
    ][
        [^ remainder]: trap parser input  ; first find end position

        [^replacement _]: trap replacer input

        ; CHANGE returns tail, use as new position
        ;
        input: change:part input ^replacement remainder
        return ~#change~
    ]

    'remove combinator [
        "Remove data that matches a parse rule"
        return: [trash!]
        input [any-series?]
        parser [action!]
        <local> remainder
    ][
        [^ remainder]: trap parser input  ; first find end position

        input: remove:part input remainder
        return ~#remove~  ; note that REMOVE <END> is considered successful
    ]

    'insert combinator [
        "Insert literal data into the input series"
        return: [trash!]
        input [any-series?]
        parser [action!]
        <local> ^insertion
    ][
        [^insertion _]: trap parser input  ; remainder ignored

        input: insert input ^insertion
        return ~#insert~
    ]

    === SEEKING KEYWORDS ===

    'to combinator [
        "Match up TO a certain rule (result position before succeeding rule)"
        return: "The rule's product"
            [any-value?]
        input [any-series?]
        parser [action!]
        <local> ^result
    ][
        cycle [
            [^result _]: parser input except [
                if tail? input [  ; could be `to <end>`, so check tail *after*
                    return fail "TO did not find desired pattern"
                ]
                input: next input
                continue
            ]
            ; don't factor in parser's advancement here (THRU does)
            return ^result
        ]
        panic ~#unreachable~
    ]

    'thru combinator [
        "Match up THRU a certain rule (result position after succeeding rule)"
        return: "The rule's product"
            [any-value?]
        input [any-series?]
        parser [action!]
        <local> ^result remainder
    ][
        cycle [
            [^result remainder]: parser input except [
                if tail? input [  ; could be `thru <end>`, check TAIL? *after*
                    return fail "THRU did not find desired pattern"
                ]
                input: next input
                continue
            ]
            input: remainder  ; factor in parser's advancement (TO doesn't)
            return ^result
        ]
        panic ~#unreachable~
    ]

    'seek combinator [
        return: "seeked position"
            [any-series?]
        input [any-series?]
        parser [action!]
        <local> where remainder
    ][
        [^where remainder]: trap parser input

        case [
            void? ^where [
                input: remainder
            ]
            integer? ^where [
                input: at (head of input) ^where
            ]
            any-series? ^where [
                if not same? (head of input) (head of ^where) [
                    panic "Series SEEK in UPARSE must be in the same series"
                ]
                input: ^where
            ]
            panic "SEEK requires INTEGER!, series position, or VOID"
        ]
        return input  ; does seeked position as return value make sense?
    ]

    'between combinator [
        return: "Copy of content between the left and right parsers"
            [any-series?]
        input [any-series?]
        parser-left [action!]
        parser-right [action!]
        <local> start limit remainder
    ][
        [^ start]: trap parser-left input

        limit: start
        cycle [
            [^ remainder]: parser-right limit except e -> [
                if tail? limit [  ; remainder of null
                    return fail e
                ]
                limit: next limit
                continue  ; don't try to assign the `[^ remainder]:`
            ]
            input: remainder
            return copy:part start limit
        ]
        panic ~#unreachable~
    ]

    === TAG! SUB-DISPATCHING COMBINATOR ===

    ; Historical PARSE matched tags literally, while UPARSE pushes to the idea
    ; that they are better leveraged as "special nouns" to avoid interfering
    ; with the user variables in wordspace.

    <here> combinator [
        "Get the current parse input position, without advancing input"
        return: "parse position"
            [any-series?]
        input [any-series?]
    ][
        return input
    ]

    <end> ghostable combinator [
        "Only match if the input is at the end"
        return: [ghost!]
        input [any-series?]
        :negated
    ][
        if tail? input [
            if negated [
                return fail "PARSE position at <end> (but parser negated)"
            ]
            return ^ghost
        ]
        if negated [
            return ^ghost
        ]
        return fail "PARSE position not at <end>"
    ]

    <input> combinator [
        "Get the original input of the PARSE operation"
        return: "parse position"
            [any-series?]
        input [any-series?]
    ][
        return state.input
    ]

    <subinput> combinator [
        "Get the input of the SUBPARSE operation"
        return: "parse position"
            [any-series?]
        input [any-series?]
    ][
        return head of input  ; !!! What if SUBPARSE series not at head?
    ]

    === ACROSS (COPY?) ===

    ; Historically Rebol used COPY to mean "match across a span of rules and
    ; then copy from the first position to the tail of the match".  That could
    ; have assignments done during, which extract some values and omit others.
    ; You could thus end up with `y: copy x: ...` and wind up with x and y
    ; being different things, which is not intuitive.

    'across combinator [
        "Copy from the current parse position through a rule"
        return: "Copied series"
            [any-series?]
        input [any-series?]
        parser [action!]
        <local> start
    ][
        start: input
        [^ input]: trap parser input

        if any-list? input [
            return as block! copy:part start input
        ]
        if any-string? input [
            return as text! copy:part start input
        ]
        return copy:part start input
    ]

    'copy combinator [
        "Disabled combinator, included to help guide to use ACROSS"
        return: [<divergent>]
        input [any-series?]
    ][
        panic [
            "Transitionally (maybe permanently?) the COPY function in UPARSE"
            "is done with ACROSS:" https://forum.rebol.info/t/1595
        ]
    ]

    === SUBPARSE COMBINATOR ===

    ; Rebol2 had a INTO combinator which only took one argument: a rule to use
    ; when processing the nested input.  There was a popular proposal that
    ; INTO would take a datatype, which would help simplify a common pattern.
    ;
    ;     ahead text! into [some "a"]  ; arity-1 form
    ;     =>
    ;     into text! [some "a"]  ; arity-2 form, from proposals/Topaz
    ;
    ; The belief being that wanting to test the type you were going "INTO" was
    ; needed more often than not, and that at worst it would incentivize adding
    ; the type as a comment.  Neither R3-Alpha nor Red adopted this proposal
    ; (but Topaz did).
    ;
    ; UPARSE reframes this not to take just a datatype, but a "value-bearing
    ; rule".  This means you can use it with generated data that is not
    ; strictly resident in the series, effectively parameterizing it like
    ; PARSE itself...so it's called SUBPARSE:
    ;
    ;     parse "((aaaa)))" [subparse [between some "(" some ")"] [some "a"]]
    ;
    ; Because any value-bearing rule can be used, GROUP! rules are also legal,
    ; which lets you break the rules up for legibility (and avoids interpreting
    ; lists as rules themselves)
    ;
    ;     parse [| | any any any | | |] [
    ;          content: between some '| some '|
    ;          subparse (content) [some 'any]
    ;     ]
    ;
    ; arity-1 INTO may still be useful as a shorthand for SUBPARSE ONE, but
    ; it's also a little bit obtuse when read in context.
    ;
    ; !!! Note: Refinements aren't working as a general mechanic yet, so the
    ; SUBPARSE:RELAX, and SUBPARSE-MATCH:RELAX, etc. have to be handled by
    ; literal combinator specializations added to the map. While :MATCH is
    ; not a refinement on PARSE itself, it's a refinement here for expedience.
    ;
    ; https://forum.rebol.info/t/the-parse-of-progress/1349/2

    'subparse combinator [
        "Recursion into other data with a rule, result of rule if match"
        return: "Result of the subparser"
            [any-value?]
        input [any-series?]
        parser [action!]  ; !!! Easier expression of value-bearing parser?
        subparser [action!]
        :match "Return input on match, not synthesized value"
        :relax "Don't have to match input completely, just don't mismatch"
        <local> subseries subremainder ^result
    ][
        ; If the parser in the first argument can't get a value to subparse
        ; then TRAP propagates the FAIL and we don't process it.
        ;
        ; !!! Review: should we allow non-value-bearing parsers that just
        ; set limits on the input?
        ;
        [^subseries input]: trap parser input

        if void? ^subseries [
            panic "Cannot SUPBARSE into a void"
        ]

        if antiform? ^subseries [
            panic "Cannot SUBPARSE an antiform synthesized result"
        ]

        subseries: ^subseries

        case [
            any-series? subseries []
            any-sequence? subseries [subseries: as block! subseries]
            return fail "Need SERIES! or SEQUENCE! input for use with SUBPARSE"
        ]

        ; If the entirety of the item at the list is matched by the
        ; supplied parser rule, then we advance past the item.
        ;
        [^result subremainder]: trap subparser subseries

        if (not relax) and (not tail? subremainder) [
            return fail "SUBPARSE succeeded, but didn't complete subseries"
        ]
        if match [
            return subseries
        ]
        return ^result
    ]

    'into combinator [
        "Arity-1 variation of SUBPARSE (presumes current position as input)"
        return: "Result of the subparser"
            [any-value?]
        input [any-list?]
        subparser [action!]
        :match "Return input on match, not synthesized value"
        :relax "Don't have to match input completely, just don't mismatch"
        <local> subseries subremainder ^result
    ][
        subseries: trap input.1
        input: next input

        case [
            any-series? subseries []
            any-sequence? subseries [subseries: as block! subseries]
            return fail "Need SERIES! or SEQUENCE! input for use with INTO"
        ]

        ; If the entirety of the item at the list is matched by the
        ; supplied parser rule, then we advance past the item.
        ;
        [^result subremainder]: trap subparser subseries

        if (not relax) and (not tail? subremainder) [
            return fail "SUBPARSE succeeded, but didn't complete subseries"
        ]
        if match [
            return subseries
        ]
        return ^result
    ]

    === COLLECT AND KEEP ===

    ; The COLLECT feature was first added by Red.  However, it did not use
    ; rollback across any KEEPs that happened when a parse rule failed, which
    ; makes the feature of limited use.
    ;
    ; UPARSE has a generic framework for bubbling up gathered items.  We look
    ; through that list of items here for anything marked as collect material
    ; and remove it.

    'collect combinator [
        return: "Block of collected values"
            [block!]
        input [any-series?]
        :pending [blank? block!]
        parser [action!]
        <local> collected
    ][
        [^ input pending]: trap parser input

        ; PENDING can be blank or a block full of items that may or may
        ; not be intended for COLLECT.  Right now the logic is that all QUOTED!
        ; items are intended for collect, extract those from the pending list.
        ;
        ; !!! More often than not a COLLECT probably is going to be getting an
        ; list of all QUOTED! items.  If so, there's probably no great reason
        ; to do a new allocation...instead the quoteds should be mutated into
        ; unquoted forms.  Punt on such optimizations for now.
        ;
        collected: collect [
            remove-each 'item pending [
                if quoted? item [keep unquote item, okay]
            ]
        ]

        return collected
    ]

    'keep combinator [
        return: "The kept value (same as input)"
            [<null> element? splice!]
        input [any-series?]
        :pending [blank? block!]
        parser [action!]
        <local> ^result
    ][
        [^result input pending]: trap parser input

        if void? ^result [
            pending: blank
            return null
        ]
        ^result: decay ^result

        ; Since COLLECT is considered the most common `pending` client, we
        ; reserve QUOTED! items for collected things.
        ;
        ; All collect items are appended literally--so COLLECT itself doesn't
        ; have to worry about splicing.  More efficient concepts (like double
        ; quoting normal items, and single quoting spliced blocks) could be
        ; used instead--though copies of blocks would be needed either way.
        ;
        case [
            element? ^result [
                pending: glom pending quote result  ; quoteds target COLLECT
            ]
            splice? ^result [
                for-each 'item unsplice result [
                    pending: glom pending quote item  ; quoteds target COLLECT
                ]
            ]
            panic "Incorrect KEEP (not value or SPREAD)"
        ]

        return ^result
    ]

    === ACCUMULATE ===

    ; The pattern of `collect [opt some keep [...]]` is common, and actually
    ; kind of easy to mess up by writing COLLECT KEEP SOME or just forgetting
    ; the KEEP entirely.
    ;
    ; ACCUMULATE gives you that pattern more easily and efficiently.

    'accumulate combinator [
        return: "Block of collected values"
            [block!]
        input [any-series?]
        parser [action!]
        <local> collected
    ][
        collected: copy []
        cycle [
            append collected (
                [{^} input]: parser input except e -> [
                    return collected
                ]
            )
        ]
    ]

    === GATHER AND EMIT ===

    ; With gather, the idea is to do more of a "bubble-up" sort of strategy
    ; for creating objects with labeled fields.  Also, the idea that PARSE
    ; itself would switch modes.
    ;
    ; !!! A particularly interesting concept that has come up is being able
    ; to "USE" or "IMPORT" an OBJECT! so its fields are local (like a WITH).
    ; This could combine with gather, e.g.
    ;
    ;     import parse [1 "hi"] [
    ;         return gather [emit x: integer!, emit y: text!]
    ;     ]
    ;     print [x "is one and" y "is -[hi]-"]
    ;
    ; The idea is interesting enough that it suggests being able to EMIT with
    ; no GATHER in effect, and then have the RETURN GATHER semantic.

    'gather combinator [
        return: "The gathered object"
            [object!]
        input [any-series?]
        :pending [blank? block!]
        parser [action!]
        <local> obj
    ][
        [^ input pending]: trap parser input

        ; Currently we assume that all the BLOCK! items in the pending are
        ; intended for GATHER.

        obj: make object! collect [
            remove-each 'item pending [
                if block? item [keep spread item, okay]
            ] else [
                ; should it error or fail if pending was space ?
            ]
        ]

        return obj
    ]

    'emit combinator [
        return: "The emitted value"
            [any-stable?]
        input [any-series?]
        :pending [blank? block!]
        @target [set-word? set-group?]
        parser [action!]
        <local> ^result
    ][
        if set-group? target [
            if not match [
                any-word? set-word? get-word?
            ] (target: eval unchain target) [
                panic [
                    "GROUP! from EMIT (...): must produce an ANY-WORD?, not"
                    @target
                ]
            ]

            ; !!! MAKE OBJECT! fails if any top-level SET-WORD! have bindings.
            ; Revisit if that rule changes and this becomes unnecessary.
            ;
            if binding of target: resolve target [
                panic ["EMIT can't use bound words for object keys:" target]
            ]
            target: setify target
        ] else [
            target: unbind target  ; `emit foo:` okay in function defining foo
        ]

        [^result input pending]: trap parser input

        ; We lift the result.  This lets us emit antiforms, since the MAKE
        ; OBJECT! evaluates.
        ;
        pending: glom pending reduce [target lift require ^result]
        return ^result
    ]

    === SET-WORD! and SET-TUPLE! COMBINATOR ===

    ; The concept behind Ren-C's SET-WORD! in PARSE is that parse combinators
    ; don't just update the parse input position, but they also return values.
    ; If these appear to the right of a set-word, then the set word will be
    ; assigned on a match.
    ;
    ; !!! SET-PATH! support will be very narrow, for assigning functions.

    '*: combinator [
        return: "The set value"
            [any-stable?]
        input [any-series?]
        value [set-word? set-tuple? set-group?]
        parser "If assignment, failed parser means target will be unchanged"
            [action!]
        <local> ^result var
    ][
        if group? var: inside value value.1 [  ; eval left hand group first
            var: eval var
        ]

        [^result input]: trap parser input

        return set var ^result
    ]

    'set combinator [
        "Disabled combinator, included to help guide to use SET-WORD!"
        return: [<divergent>]
        input [any-series?]
    ][
        panic [
            "The SET keyword in UPARSE is done with SET-WORD!, and if SET does"
            "come back it would be done differently:"
            https://forum.rebol.info/t/1139
        ]
    ]

    === TEXT! COMBINATOR ===

    ; For now we just build TEXT! on FIND, though this needs to be sensitive
    ; to whether we are operating on blocks or text/binary.
    ;
    ; !!! We presume that it's value-bearing, and gives back the value it
    ; matched against.  If you don't want it, you have to ELIDE it.  Note this
    ; value is the rule in the string and binary case, but the item in the
    ; data in the block case.

    text! combinator [
        return: "The rule series matched against (not input value)"
            [text!]
        input [any-series?]
        value [text!]
        <local> neq? item
    ][
        case [
            any-list? input [
                item: trap input.1
                /neq?: either state.case [not-equal?/] [lax-not-equal?/]
                if neq? item value [
                    return fail "Value at parse position does not match TEXT!"
                ]
                input: next input
                return item
            ]

            ; for both of these cases, we have to use value as the result,
            ; since there's no unique item to capture.  Should we copy it?

            any-string? input [
                [_ input]: find // [
                    input value
                    match: ok
                    case: state.case
                ] else [
                    return fail "String at parse position does not match TEXT!"
                ]
            ]
            <default> [
                assert [blob? input]
                [_ input]: find // [
                    input value
                    match: ok
                    case: state.case
                ] else [
                    return fail "Binary at parse position does not match TEXT!"
                ]
            ]
        ]

        return value
    ]

    === RUNE! COMBINATOR ===

    ; The RUNE! type is an optimized immutable form of string that will
    ; often be able to fit into a cell with no series allocation.  This makes
    ; it good for representing characters, but it can also represent short
    ; strings.  It matches case-sensitively.

    rune! combinator [
        return: "The token matched against (not input value)"
            [rune!]
        input [any-series?]
        value [rune!]
        :negated
        <local> item
    ][
        if tail? input [
            return fail "RUNE! cannot match at end of input"
        ]
        case [
            any-list? input [
                item: input.1
                if value = item [
                    if negated [
                        return fail "RUNE! matched, but combinator negated"
                    ]
                    input: next input
                    return item
                ]
                if negated [
                    input: next input
                    return item
                ]
                return fail "Value at parse position does not match RUNE!"
            ]
            any-string? input [
                if negated [
                    if (not char? value) [
                        panic "NOT RUNE! with strings is only for single char"
                    ]
                    item: input.1
                    if item = value [
                        return fail "Negated RUNE! char matched at string"
                    ]
                    remainder: next input
                    return item
                ]
                [_ input]: find:match:case input value else [
                    return fail [
                        "String at parse position does not match RUNE!"
                    ]
                ]
                return value
            ]
            <default> [
                assert [blob? input]
                if negated [
                    if (not char? value) or (255 < codepoint of value) [
                        panic "NOT RUNE! with blobs is only for single byte"
                    ]
                    item: input.1
                    if item = codepoint of value [
                        return fail "Negated RUNE! char matched at blob"
                    ]
                    remainder: next input
                    return make-char item
                ]
                [_ input]: find:match:case input value else [
                    return fail [
                        "Binary at parse position does not match RUNE!"
                    ]
                ]
                return value
            ]
        ]
    ]

    === BLOB! COMBINATOR ===

    ; Arbitrary matching of binary against text is a bit of a can of worms,
    ; because if we AS alias it then that would constrain the binary...which
    ; may not be desirable.  Also you could match partial characters and
    ; then not be able to set a string position.  So we don't do that.

    blob! combinator [
        return: "The binary matched against (not input value)"
            [blob!]
        input [any-series?]
        value [blob!]
        <local> item
    ][
        case [
            any-list? input [
                item: try input.1  ; use TRAP?
                if value = item [
                    input: next input
                    return item
                ]
                return fail "Value at parse position does not match BLOB!"
            ]
            <default> [  ; Note: BITSET! acts as "byteset" here
                ; binary or any-string input
                [_ input]: find:match input value else [
                    return fail [
                        "Content at parse position does not match BLOB!"
                    ]
                ]
                return value
            ]
        ]
    ]

    === LET COMBINATOR ===

    ; We want to be able to declare variables mid-parse.  The parsing state
    ; keeps track of a binding environment that can be added to by combinators
    ; as they go.  The combinatorization process uses this environment with
    ; INSIDE on unbound material.

    'let combinator [
        return: "Result of the LET if assignment, else bound word"
            [any-stable?]
        input [any-series?]
        'vars [set-word?]
        parser [action!]
        <local> ^result
    ][
        [^result input]: trap parser input

        state.env: add-let-binding state.env (unchain vars) ^result

        return ^result
    ]

    === IN COMBINATOR ===

    ; Propagates the binding from <input> onto a value.
    ; Should be <in> but TAG! combinators can't take arguments yet.

    '*in* combinator [
        return: "Argument binding gotten INSIDE the current input"
            [any-stable?]
        input [any-series?]
        parser [action!]
        <local> ^result
    ][
        [^result input]: trap parser input

        return inside state.input ^result
    ]

    === GROUP! AND PHASE COMBINATOR ===

    ; GROUP! does not advance the input, just runs the group.  It can return
    ; a value, which is used by value-accepting combinators.  Use ELIDE if
    ; this would be disruptive in terms of a value-bearing BLOCK! rule.
    ;
    ; There is a new and important feature where a GROUP! can ask to be
    ; deferred until a PHASE is complete (where the parse as a whole also
    ; counts as a phase).  This is done using the <delay> tag (may not be
    ; the best name).

    group! ghostable combinator [
        return: "Result of evaluating the group (invisible if <delay>)"
            [any-value?]
        input [any-series?]
        :pending [blank? block!]
        value [any-list?]  ; allow any array to use this "EVAL combinator"
        <local> ^result
    ][
        if tail? value [
            pending: blank
            return ^ghost
        ]

        if <delay> = first value [
            if 1 = length of value [
                panic "Use ('<delay>) to evaluate to the tag <delay> in GROUP!"
            ]
            pending: reduce [next value]  ; GROUP! signals delayed groups
            return ^ghost  ; act invisible
        ]

        pending: blank

        ^result: eval value

        case [
            error? ^result [
                if veto? ^result [  ; VETO error means fail the GROUP! rule
                    return ^result
                ]
                panic ^result  ; all other error antiforms escalate to panics
            ]
            void? ^result [  ; void is an interesting synthesized value...
                return ^void  ; but it wouldn't DECAY, so handle specially
            ]
            ghost? ^result [  ; allow (elide print "HI") to vanish
                return ^ghost
            ]
        ]

        return decay ^result
    ]

    'phase ghostable combinator [
        return: "Result of the parser evaluation"
            [any-value?]
        input [any-series?]
        :pending [blank? block!]
        parser [action!]
        <local> ^result
    ][
        [^result input pending]: trap parser input

        ; Run GROUP!s in order, removing them as one goes
        ;
        remove-each 'item pending [
            if group? item [eval item, okay]
        ]

        return ^result
    ]

    === INLINE COMBINATOR ===

    ; This allows the running of a rule generated by code, as if it had been
    ; written in the spot.
    ;
    ; !!! The rules of what are allowed or not when triggering through WORD!s
    ; likely apply here.  Should that be factored out?
    ;
    ; 1. Raising errors out of combinators has a specific meaning of the
    ;    combinator not matching.  We do not want to bubble out a definitional
    ;    error indicating a problem inside the evaluation in a way that would
    ;    conflate it with the combinator simply not matching.  Must panic.
    ;
    ; 2. The original INLINE concept allowed:
    ;
    ;        parse "aaa" [inline ('some) "a"]
    ;
    ;    To do that variadically would require some kind of back-channel of
    ;    communication to the BLOCK! combinator to return the material, or
    ;    have specific code in the BLOCK! combinator to process INLINE.
    ;    It's of questionable value--while this as an independent combinator
    ;    has clear value.  Arity-0 combinators may be an exception here.
    ;    Permit it inefficiently for now by enclosing in a block.
    ;
    ; 3. We don't need to call COMBINATORIZE because we can't handle non
    ;    arity-0 combinators here.  But this should have better errors if the
    ;    datatype combinator takes arguments.

    'inline combinator [
        return: "Result of running combinator from fetching the WORD!"
            [any-value?]
        input [any-series?]
        :pending [blank? block!]   ; we retrigger combinator; it may KEEP, etc.

        parser [action!]
        <local> ^r comb subpending
    ][
        [^r input pending]: parser input except e -> [
            panic e  ; can't use TRAP here, don't want to fail [1]
        ]

        if (void? ^r) or (ghost? ^r) [
            return ^ghost
        ]

        r: decay ^r  ; packs can't act as rules, decay them

        if antiform? ^r [  ; REVIEW: splices?
            panic "INLINE can't evaluate to antiform"  ; also not a fail [1]
        ]

        if not block? r [
            r: envelop [] r  ; enable arity-0 combinators [2]
        ]

        if not comb: select state.combinators (type of r) [
            panic [
                "Unhandled type in GET-GROUP! combinator:" to word! type of r
            ]
        ]

        [^r input subpending]: trap run comb state input r  ; [3]
        pending: glom pending subpending

        return ^r
    ]

    === GET-BLOCK! COMBINATOR ===

    ; If the GET-BLOCK! combinator were going to have a meaning, it would likely
    ; have to be "run this block as a rule, and use the synthesized product
    ; as a rule"
    ;
    ;     >> parse "aaabbb" [:[some "a" ([some "b"])]
    ;     == "b"
    ;
    ; It's hard offhand to think of great uses for that, but that isn't to say
    ; that they don't exist.

    ; !!! No current meaning, has to be part of ':* at this time

    === BITSET! COMBINATOR ===

    ; There is some question here about whether a bitset used with a BLOB!
    ; can be used to match UTF-8 characters, or only bytes.  This may suggest
    ; a sort of "INTO" switch that could change the way the input is being
    ; viewed, e.g. being able to do INTO BLOB! on a TEXT! (?)

    bitset! combinator [
        return: "The matched input value"
            [char? integer!]
        input [any-series?]
        value [bitset!]
        <local> item
    ][
        item: trap input.1

        case [
            any-list? input [
                if value = item [
                    input: next input
                    return item
                ]
            ]
            any-string? input [
                if pick value item [
                    input: next input
                    return item
                ]
            ]
            <default> [
                assert [blob? input]
                if pick value item [
                    input: next input
                    return item
                ]
            ]
        ]
        return fail "BITSET! did not match at parse position"
    ]

    === QUOTED! COMBINATOR ===

    ; When used with ANY-LIST? input, recognizes values literally.  When used
    ; with ANY-STRING? it leverages FIND's ability to match the molded form,
    ; e.g. counting the delimiters in tags.
    ;
    ;     >> parse "<a> 100 (b c)" ['<a> space '100 space '(b c)]
    ;     == (b c)

    quoted! combinator [
        return: "The matched value"
            [element?]
        input [any-series?]
        value [quoted!]
        :negated
        <local> comb eq? item
    ][
        if any-list? input [
            item: trap input.1
            input: next input

            /eq?: either state.case [equal?/] [lax-equal?/]
            if eq? item unquote value [
                if negated [
                    return fail "Negated QUOTED! parser matched item"
                ]
                return unquote value
            ]
            if negated [
                return item
            ]
            return fail [
                "Value at parse position wasn't unquote of QUOTED! item"
            ]
        ]

        if negated [  ; !!! allow single-char or single-byte negations?
            panic "QUOTED! combinator can only be negated for List input"
        ]

        ensure [any-string? blob!] input

        if tail? input [
            return fail "QUOTED! cannot match at end of input"
        ]

        [_ input]: find:match input value else [
            return fail "Molded form of QUOTED! item didn't match"
        ]
        return unquote value
    ]

    === LITERAL! COMBINATOR ===

    'literal combinator [  ; shorthanded as LIT
        return: "Literal value" [element?]
        input [any-series?]
        :pending [blank? block!]
        'value [element?]
        <local> comb
    ][
        ; Though generic quoting exists, being able to say [lit ''x] instead
        ; of ['''x] may be clarifying when trying to match ''x (for instance)

        comb: state.combinators.(quoted!)
        return [{_} input pending]: run comb state input (lift value)
    ]

    === ANTIFORM COMBINATORS ===

    ; !!! It's not clear how ~[...]~ should act generally, but one idea would
    ; be that it be a shorthand for PACK:
    ;
    ; https://rebol.metaeducation.com/t/dialecting-quasiforms-in-parse/2379
    ;
    ; Whether it evaluates the contents or does as-is, ~[]~ would still mean
    ; "synthesize a void" either way.  We introduce it here to bridge
    ; compatibility with old tests that matched stable void keywords.
    ;
    '~[]~ combinator [
        return: [~[]~]
        input [any-series?]
    ][
        return ^void
    ]

    keyword! combinator [
        return: [ghost!]
        input [any-series?]
        value [keyword!]
        <local> comb neq?
    ][
        switch value [
          ~null~ [
            panic make warning! [type: 'script, id: 'bad-antiform]
          ]
          ~okay~ [
            return ^ghost  ; let okay just act as a "guard", no influence
          ]
        ]
        panic ["Unknown keyword" mold lift value]
    ]

    splice! combinator [
        return: [<divergent>]
        input [any-series?]
        :pending [blank? block!]
        value [splice!]
        <local> comb neq?
    ][
        pending: blank

        if tail? input [
            return fail "SPLICE! cannot match at end of input"
        ]

        if not any-list? input [
            panic "Splice combinators only match ANY-LIST? input"
        ]

        /neq?: either state.case [not-equal?/] [lax-not-equal?/]
        for-each 'item unanti value [
            if neq? input.1 item [
                return fail [
                    "Value at input position didn't match splice element"
                ]
            ]
            input: next input
        ]
        return value  ; matched splice is result
    ]

    === INTEGER! COMBINATOR ===

    ; The behavior of INTEGER! in UPARSE is to just evaluate to itself.  If you
    ; want to repeat a rule a certain number of times, you have to say REPEAT.
    ;
    ; A compatibility combinator for Rebol2 PARSE has the repeat semantics
    ; (though variadic combinators would be required to accomplish [2 4 rule]
    ; meaning "apply the rule between 2 and 4 times")
    ;
    ; Note that REPEAT allows the use of SPACE to opt out of an iteration.

    integer! combinator [
        return: "Just the INTEGER! (see REPEAT for repeating rules)"
            [integer!]
        input [any-series?]
        value [integer!]
    ][
        return value
    ]

    'repeat combinator [
        return: "Last parser result"
            [any-value?]
        input [any-series?]
        times-parser [action!]
        parser [action!]
        <local> ^times min max ^result
    ][
        [^times input]: trap times-parser input

        if void? ^times [  ; VOID-in-NULL-out
            return null
        ]
        switch:type ^times [
            rune! [
                if ^times = _ [  ; should space be tolerated if void is?
                    return ^ghost  ; `[repeat (_) rule]` is a no-op
                ]

                if ^times <> # [
                    panic ["REPEAT takes RUNE! of # to act like TRY SOME"]
                ]
                min: 0, max: #
            ]
            integer! [
                max: min: ^times
            ]
            block! block?:pinned/ [
                parse ^times [
                    _ _ <end> (
                        return ^ghost  ; `[repeat ([_ _]) rule]` is a no-op
                    )
                    |
                    min: [integer! | _ (0)]
                    max: [integer! | _ (min) | '#]  ; space means max = min
                ]
            ]
        ] else [
            panic "REPEAT combinator requires INTEGER! or [INTEGER! INTEGER!]"
        ]

        all [max <> #, max < min] then [
            panic "Can't make MAX less than MIN in range for REPEAT combinator"
        ]

        append state.loops binding of $return

        ^result: ^ghost  ; [repeat (0) one] is void, but ^ can unvoidify it

        count-up 'i max [  ; will count infinitely if max is #
            ;
            ; After the minimum number of repeats are fulfilled, the parser
            ; may not match and return the last successful result.  So
            ; we don't do the assignment in the `[...]:` multi-return in case
            ; it would overwrite the last useful result.  Instead, the GROUP!
            ; potentially returns...only do the assignment if it does not.
            ;
            ; !!! Use ELIDE because packs have decay problems, and COUNT-UP
            ; is looking at the loop body value to decay it.
            ;
            elide [^result input]: parser input except [
                take:last state.loops
                if i <= min [  ; `<=` not `<` as this iteration failed!
                    return fail "REPEAT did not reach minimum repetitions"
                ]
                return ^result
            ]
        ]

        take:last state.loops
        return ^result
    ]

    === DATATYPE! COMBINATORS ===

    ; Traditionally you could only use a datatype with ANY-LIST? types,
    ; but since Ren-C uses UTF-8 Everywhere it makes it practical to merge in
    ; transcoding:
    ;
    ;     >> parse "-[Neat!]- 1020" [t: text! i: integer!]
    ;     == "-[Neat!]- 1020"
    ;
    ;     >> t
    ;     == "Neat!"
    ;
    ;     >> i
    ;     == 1020
    ;

    datatype! combinator [
        return: "Matched or synthesized value"
            [element?]
        input [any-series?]
        value [datatype!]
        :negated
        <local> item error
    ][
        if any-list? input [
            item: trap input.1
            input: next input
            if value <> type of item [
                if negated [
                    return item
                ]
                return fail "Value at parse position did not match TYPE-BLOCK!"
            ]
            if negated [
                return fail "Value at parse position matched TYPE-BLOCK!"
            ]
            return item
        ]

        if negated [
            panic "TYPE-BLOCK! only supported negated for array input"
        ]

        [input item]: trap transcode:next input

        ; If TRANSCODE knew what kind of item we were looking for, it could
        ; shortcut this.  Since it doesn't, we might waste time and memory
        ; doing something like transcoding a very long block, only to find
        ; afterward it's not something like a requested integer!.  Red
        ; has some type sniffing in their fast lexer, review relevance.
        ;
        if value != type of item [
            return fail "Could not TRANSCODE the datatype from input"
        ]
        return item
    ]

    === MATCH COMBINATOR ===

    'match combinator [
        return: "Element if it matches the match rule" [element?]
        input [any-series?]
        @value [frame! block! group!]
        <local> item
    ][
        if group? value [
            match [frame! block!] (value: trap eval value) else [
                panic ["GROUP! must evaluate to BLOCK! or FRAME! in MATCH"]
            ]
        ]

        item: trap input.1

        either block? value [
            if match value item [
                input: next input
                return item
            ]
        ][
            if run value item [
                input: next input
                return item
            ]
        ]

        return fail "MATCH didn't match type of current input position item"
    ]

    === JUST COMBINATOR ===

    ; The JUST combinator gives you "just the value", without matching it.

    'just combinator [
        return: "Quoted form of literal value (not matched)" [element?]
        input [any-series?]
        'value [element?]
    ][
        return value
    ]

    'the combinator [
        return: "Quoted form of literal value (not matched)" [element?]
        input [any-series?]
        @value [element?]
    ][
        return value
    ]

    === PINNED! (@XXX) COMBINATORS ===

    ; What the @XXX! combinators are for is doing literal matches vs. a match
    ; through a rule, e.g.:
    ;
    ;     >> block: [some "a"]
    ;
    ;     >> parse [[some "a"] [some "a"]] [some @block]
    ;     == [some "a"]
    ;
    ; However they need to have logic in them to do things like partial string
    ; matches on text.  That logic has to be in the QUOTED! combinator, so
    ; this code builds on that instead of repeating it.
    ;
    ; These follow a simple pattern, could generate at a higher level.
    ;
    ; 1. It would be wasteful to make [@ ...] a synonym for [@[...]], where
    ;    that also means [@ (...)] is a synonym for [@(...)].  Since @var
    ;    means "look up var and match it at this location", a much more
    ;    interesting and common application for the @ sigil is as a synonym
    ;    for the ONE combinator, to mean match anything at the current spot.
    ;
    ; 2. Evaluatively, if we get a quasiform as the lifted it means it was an
    ;    antiform.  Semantically that's matched by the quasiform combinator,
    ;    which treats source-level quasiforms as if they evaluated to their
    ;    antiforms, e.g. (parse [a b a b] [some ~(a b)~]) matches the splice.
    ;
    ; 3. @PATH! might be argued to have the meaning of "run this function
    ;    on the current item and if the result returns okay, then consider it
    ;    a match".  But that is done more cleanly with type constraints e.g.
    ;    `&even?`, which is better than `@/even?` or `@even?/`
    ;
    ; 4. @BLOCK! acting as just matching the block would be redundant with
    ;    quoted block.  So the most "logical" choice is to make it mean
    ;    "literally match the result of running the block as a rule". There
    ;    is not currently an operator that would do that inline, e.g.
    ;    [the some "a"] -> @[some "a"] due to a desire to avoid a combinator
    ;    named THE because of the special arity-0 application of @ as a
    ;    synonym or ONE.  That arity-1 combinator doesn't seem particularly
    ;    necessary, though one could of course make it.

    ; @ combinator is used for a different purpose [1]

    pinned! combinator compose [
        return: [any-value?]
        input [any-series?]
        :pending [blank? block!]
        value [@any-element?]
        <local> ^result comb subpending
    ][
        value: plain value  ; turn into plain GROUP!/WORD!/TUPLE!/etc.

        case [
            group? value [  ; run GROUP! to get *actual* value to match
                comb: runs state.combinators.(group!)
                [^result input pending]: trap comb state input value

                if void? ^result [
                    return ^void
                ]
                ^result: decay ^result
            ]
            match [tuple! word!] value [
                ^result: get value
                pending: blank  ; no pending, only "subpending"
            ]
            block? value [  ; match literal block redundant [4]
                comb: runs state.combinators.(block!)
                [^result input pending]: trap comb state input value
            ]
        ]

        comb: runs state.combinators.(type of lift result)  ; quoted or quasi

        [^result input subpending]: trap comb state input lift result

        pending: glom pending spread subpending
        return ^result
    ]


    === INVISIBLE COMBINATORS ===

    'elide ghostable combinator [
        "Transform a result-bearing combinator into one that has no result"
        return: [ghost!]
        input [any-series?]
        parser [action!]
    ][
        [^ input]: trap parser input
        return ^ghost
    ]

    'comment ghostable combinator [
        "Comment out an arbitrary amount of PARSE material"
        return: [ghost!]
        input [any-series?]
        'ignored [block! text! tag! rune!]
    ][
        return ^ghost
    ]

    'skip combinator [
        "Skip an integral number of items"
        return: "Input position after the skip"
            [any-series?]
        input [any-series?]
        parser [action!]
        <local> ^result
    ][
        [^result _]: trap parser input

        if void? ^result [  ; e.g. `skip (opt num)` when num is null
            return input
        ]
        if not integer? ^result [
            panic "SKIP expects INTEGER! amount to skip"
        ]

        return input: trap skip input ^result  ; out of bounds fails match
    ]

    'one combinator [
        "Match any one series item in input"
        return: "One element of series input"
            [element?]
        input [any-series?]
        <local> item
    ][
        item: trap input.1
        input: next input
        return item
    ]

    elide span-combinator: combinator [
        "Match any N series items in input"
        return: "Two elements of series input (SPLICE! if input is list)"
            [splice! any-string? blob!]
        input [any-series?]
        count
        <local> result
    ][
        result: copy:part input count  ; should this sematic be COPY:RELAX ?
        if count <> length of result [
            return fail "Insufficient items in series for span capture"
        ]
        input: skip input count

        if any-list? input [
            return spread result  ; want a SPLICE! in this case
        ]
        return result
    ]

    'two specialize span-combinator [count: 2]
    'three specialize span-combinator [count: 3]
    'four specialize span-combinator [count: 4]
    'five specialize span-combinator [count: 5]  ; is five enough?  too much?

    'next combinator [
        "Give next position in input, succeeding so long as it's not at END"
        return: "Input position after skipping one item"
            [element?]
        input [any-series?]
    ][
        return input: trap next input  ; match fail if out of bounds
    ]

    === ACTION! COMBINATOR ===

    ; The action combinator is a new idea of letting you call a normal
    ; function with parsers fulfilling the arguments.  At the Montreal
    ; conference Carl said he was skeptical of PARSE getting any harder to read
    ; than it was already, so the isolation of DO code to GROUP!s seemed like
    ; it was a good idea, but maybe allowing calling zero-arity functions.
    ;
    ; With Ren-C it's easier to try these ideas out.  So the concept is that
    ; you can make a PATH! that starts with / and that will be run as a normal
    ; action but whose arguments are fulfilled via PARSE.

    action! combinator [
        "Run an ordinary action with parse rule products as its arguments"
        return: "The return value of the action"
            [any-value?]
        input [any-series?]
        :pending [blank? block!]
        value [frame!]
        ; AUGMENT is used to add param1, param2, param3, etc.
        :parsers "Sneaky argument of parsers collected from arguments"
            [block!]
        <local> arg subpending
    ][
        ; !!! We very inelegantly pass a block of PARSERS for the argument in
        ; because we can't reach out to the augmented frame (rule of the
        ; design) so the augmented function has to collect them into a block
        ; and pass them in a refinement we know about.  This is the beginning
        ; of a possible generic mechanism for variadic combinators, but it's
        ; tricky because the variadic step is before this function actually
        ; runs...review as the prototype evolves.

        ; !!! We cannot use the autopipe mechanism because hooked combinator
        ; does not see the augmented frame.  Have to do it manually.
        ;
        pending: blank

        let f: make frame! value
        for-each 'key f [
            let param: select value key
            if not param.optional [
                ensure frame! parsers.1

                f.(key): [{_} input subpending]: trap run parsers.1 input

                pending: glom pending spread subpending
                parsers: next parsers
            ]
        ]
        assert [tail? parsers]
        return eval f
    ]

    === WORD! and TUPLE! COMBINATOR ===

    ; The WORD! combinator handles *non-keyword* WORD! dispatches.  It cannot
    ; also handle keywords, because that would mean it would have to be able
    ; to take the parameters to those keywords...hence needing to advance the
    ; rules--or be a "variadic" combinator.
    ;
    ; The operation is basically the same for TUPLE!, so the same code is used.

    word! combinator [
        return: "Result of running combinator from fetching the WORD!"
            [any-value?]
        input [any-series?]
        :pending [blank? block!]
        value [word! tuple!]
        <local> ^r comb rule-start rule-end
    ][
        rule-start: null
        rule-end: null

        switch:type ^r: get value [
            ;
            ; BLOCK!s are accepted as rules, and looked up via combinator.
            ; Most common case, make them first to shortcut earlier.
            ;
            block! [
                rule-start: ^r
                rule-end: tail of ^r
            ]

            ; GROUP!s are also accepted as rules, and looked up via combinator.
            ;
            group! [
                rule-start: null  ; !!! what to do here?
                rule-end: null
            ]

            ; These types are accepted literally (though they do run through
            ; the combinator looked up to, which ultimately may not mean
            ; that they are literal... should there be a special "literal"
            ; mapped-to-value so that if you rephrase them to active
            ; combinators the word lookup won't work?)
            ;
            text! []
            blob! []
            rune! []

            ; Datatypes looked up by words (e.g. TAG!) are legal as rules
            ;
            datatype! []

            ; Bitsets were also legal as rules without decoration
            ;
            bitset! []

            void?/ [
                pending: blank  ; not delegating to combinator with pending
                return ^void  ; yield void
            ]

            okay?/ [
                panic "WORD! fetches cannot be ~okay~ in UPARSE (see WHEN)"
            ]

            null?/ [
                panic "WORD! fetches cannot be ~null~ in UPARSE (see WHEN)"
            ]

            panic [
                "WORD! can't look up to active combinator, unless BLOCK!."
                "If literal match is meant, use" unspaced ["@" value]
            ]
        ]

        if not comb: select state.combinators (type of ^r) [
            panic ["Unhandled type in WORD! combinator:" to word! type of ^r]
        ]

        ; !!! We don't need to call COMBINATORIZE because we can't handle
        ; arguments here, but this should have better errors if the datatype
        ; combinator takes arguments.
        ;
        ; !!! REVIEW: handle `rule-start` and `rule-end` ?
        ;
        return [{^} input pending]: run comb state input ^r
    ]

    === NEW-STYLE ANY COMBINATOR ===

    ; Historically ANY was a synonym for what is today TRY SOME FURTHER.  It
    ; was seen as a confusing usage of the word ANY given its typical meaning
    ; related to picking a single item from a list of alternatives--not a
    ; looping construct.
    ;
    ; The new meaning of ANY provides the ability to treat a BLOCK! received
    ; as an argument as a block of alternatives.  This can frequently be more
    ; convenient than trying to build a block of alternatives with `|`, as
    ; that has conditions on the edges where [| ...] is always a no-op as it
    ; means the same thing as [[] | ...], and [... |] will always succeed if
    ; the other alternatives fail because it is the same as [... | []].
    ;
    ; Due to not wanting to handle the block argument as a rule dispatched to
    ; the block combinator, syntax is `any (alternates)` not `any alternates`.

    'any combinator [
        return: "Last result value"
            [any-value?]
        input [any-series?]
        :pending [blank? block!]
        @arg "Acts as rules if WORD!/BLOCK!, pinned acts inert"
            [word! block! group! @word! @block! @group! ]
        <local> ^result block
    ][
        block: switch:type arg [
            word! [
                (match block! get arg) else [
                    panic "ANY with WORD! must lookup to plain BLOCK!"
                ]
            ]
            word?:pinned/ [pin get arg]  ; treat result as literal
            group! [eval arg]  ; similar to default (synthesize eval)
            group?:pinned/ [pin eval arg]
            block! [arg]  ; almost certainly want to override default behavior
            block?:pinned/ [arg]  ; !!! override default behavior, good?
            panic
        ]

        if match [@block!] block [
            block: unpin block  ; should be able to enumerate regardless..
            pending: blank  ; not running any rules, won't add to pending
            if tail? block [
                return ^void  ; empty literal blocks match at any location
            ]
            if not tail? input [
                for-each 'item block [
                    if input.1 = any [
                        when any-string? input [to rune! item]
                        when blob? input [to blob! item]
                        item
                    ][
                        input: next input
                        return item  ; (parse "a" [any @[a]] -> a) ... not #a
                    ]
                ]
            ]
            return fail "None of the items in ANY literal block matched"
        ]

        if not block? block [
            panic ["The ANY combinator requires a BLOCK! of alternates"]
        ]

        until [tail? block] [
            ;
            ; Turn next alternative into a parser ACTION!, and run it.
            ; We take the first parser that succeeds.
            ;
            let [/parser 'block]: parsify state block  ; /parser for surprising
            return [{^result} input pending]: parser input except [
                continue
            ]
        ]

        return fail "All of the parsers in ANY block failed"
    ]

    === BLOCK! COMBINATOR ===

    ; Handling of BLOCK! is the central combinator.  The contents are processed
    ; as a set of alternatives separated by `|`, with a higher-level sequence
    ; operation indicated by `||`.  The bars are treated specially; e.g. | is
    ; not an "OR combinator" due to semantics, because if arguments to all the
    ; steps were captured in one giant ACTION! that would mean changes to
    ; variables by a GROUP! would not be percieved by later steps...since once
    ; a rule like SOME captures a variable it won't see changes:
    ;
    ; https://forum.rebol.info/t/when-should-parse-notice-changes/1528
    ;
    ; (There is also a performance benefit, since if | was a combinator then
    ; a sequence of 100 alternates would have to build a very large OR
    ; function...rather than being able to build a small function for each
    ; step that could short circuit before the others were needed.)

    block! (block-combinator: ghostable combinator [
        return: "Last result value"
            [any-value?]
        input [any-series?]
        :pending [blank? block!]
        value [block!]
        :limit "Limit of how far to consider (used by ... recursion)"
            [block!]
        :thru "Keep trying rule until end of block"
        <local> rules pos ^result f sublimit subpending temp old-env can-vanish
    ][
        rules: value  ; alias for clarity
        limit: default [tail of rules]
        pos: input

        pending: blank  ; can become GLOM'd into a BLOCK!

        ^result: ^ghost  ; default result is invisible

        old-env: state.env
        /return: adapt return/ [state.env: old-env]
        state.env: rules  ; currently using blocks as surrogate for environment

        until [same? rules limit] [
            if rules.1 = ', [  ; COMMA! is only legal between steps
                rules: my next
                continue
            ]

            if rules.1 = '| [
                ;
                ; Rule alternative was fulfilled.  Base case is a match, e.g.
                ; with input "cde" then [| "ab"] will consider itself to be a
                ; match before any input is consumed, e.g. before the "c".
                ;
                ; But UPARSE has an extra trick up its sleeve with `||`, so
                ; you can have a sequence of alternates within the same block.
                ; scan ahead to see if that's the case.
                ;
                ; !!! This carries a performance penalty, as successful matches
                ; must scan ahead through the whole rule of blocks just as a
                ; failing match would when searching alternates.  Caching
                ; "are there alternates beyond this point" or "are there
                ; sequences beyond this point" could speed that up as flags on
                ; cells if they were available to the internal implementation.
                ;
                catch [  ; use CATCH to continue outer loop
                    let r
                    while [r: try rules.1] [
                        rules: my next
                        if r = '|| [
                            input: pos  ; don't roll back past current pos
                            throw <inline-sequence-operator>
                        ]
                    ]
                ] then [
                    continue
                ]

                ; If we didn't find an inline sequencing operator, then the
                ; successful alternate means the whole block is done.
                ;
                input: pos
                return ^result
            ]

            ; If you hit an inline sequencing operator here then it's the last
            ; alternate in a list.
            ;
            if rules.1 = '|| [
                input: pos  ; don't roll back past current pos
                rules: my next
                continue
            ]

            can-vanish: if rules.1 = '^ [
                rules: next rules
                okay
            ]

            ; Do one "Parse Step".  This involves turning whatever is at the
            ; next parse position into an ACTION!, then running it.
            ;
            if rules.1 = '... [  ; "variadic" parser, use recursion
                rules: next rules
                if tail? rules [  ; if at end, act like [elide to <end>]
                    input: tail of pos
                    return ^result
                ]
                sublimit: find:part rules [...] limit

                ; !!! Once this used ACTION OF BINDING OF $RETURN,
                ; the fusion of actions and frames removed ACTION OF.  So the
                ; action is accessed by an assigned name from outside.  On
                ; the plus side, that allowed RULE-START and RULE-END to be
                ; exposed, which were not visible to the inner function that
                ; ACTION OF BINDING OF $RETURN received.
                ;
                f: make frame! block-combinator/  ; this combinator
                f.state: state
                f.input: pos  ; know parameter is called "input"
                f.value: rules
                f.limit: sublimit
                f.rule-start: rules
                f.rule-end: sublimit else [tail of rules]

                f.thru: ok

                rules: sublimit else [tail of rules]
            ] else [
                f: make frame! [{_} rules]: parsify state rules except e -> [
                    panic e  ; rule WORD! was null, no combinator for type...
                ]
                f.1: pos  ; PARSIFY specialized STATE in, so input is 1st
            ]

            can-vanish: default [ghostable? f]

            if not error? ^temp: eval f [
                [^temp pos subpending]: ^temp
                if unset? $pos [
                    print mold:limit rules 200
                    panic "Combinator did not set remainder"
                ]
                if unset? $subpending [
                    print mold:limit rules 200
                    panic "Combinator did not set pending"
                ]
                either ghost? ^temp [
                    if not can-vanish [
                        if not ghost? ^result [^result: ^void]
                    ]
                ][
                    ^result: ^temp  ; overwrite only if not ghost
                ]
                pending: glom pending spread subpending
            ] else [
                ^result: ^ghost  ; reset, e.g. `[veto |]`

                if pending <> blank [
                    free pending  ; proactively release memory
                ]
                pending: blank

                ; If we fail a match, we skip ahead to the next alternate rule
                ; by looking for an `|`, resetting the input position to where
                ; it was when we started.  If there are no more `|` then all
                ; the alternates failed, so return NULL.
                ;
                pos: catch [
                    let r
                    while [r: try rules.1] [
                        rules: my next
                        if r = '| [throw input]  ; reset POS

                        ; If we see a sequencing operator after a failed
                        ; alternate, it means we can't consider the alternates
                        ; across that sequencing operator as candidates.  So
                        ; return null just like we would if reaching the end.
                        ;
                        if r = '|| [break]
                    ]
                ] else [
                    if (not thru) or (tail? input) [
                        return fail* make warning! [
                            id: 'parse-mismatch
                            message:
                              "PARSE BLOCK! combinator did not match input"
                        ]
                    ]
                    rules: value
                    pos: input: my next
                    continue
                ]
            ]
        ]

        input: pos
        return ^result
    ])

    === PANIC COMBINATOR ===

    ; Gracefully handling panics has two places that you might want to
    ; provide assistance in implicating...one is the parse rules, and the other
    ; is the parse input.
    ;
    ; The PANIC combinator is--perhaps obviously (?)--not for pointing out
    ; syntax errors in the rules.  Because it's a rule.  So by default it will
    ; complain about the location where you are in the input.
    ;
    ; It lets you take an @[...] block, because a plain [...] block would be
    ; processed as a rule.  For the moment it quotes it for convenience.

    'panic combinator [
        return: [<divergent>]
        input [any-series?]
        'reason [@block!]
        <local> e
    ][
        e: make warning! [
            type: 'User
            id: 'parse
            message: to text! reason
        ]
        set-location-of-error e binding of $reason
        e.near: mold:limit input 80
        panic e
    ]
]


=== EQUIVALENTS ===

default-combinators.(tuple!): default-combinators.(word!)


=== ABBREVIATIONS ===

default-combinators.opt: default-combinators.optional
default-combinators.lit: default-combinators.literal
default-combinators.cond: default-combinators.conditional
default-combinators.(just @): default-combinators.one


=== HACKS TO COVER FOR LACK OF REFINEMENT SUPPORT ===

; Just match literal CHAIN!s :-(

s: default-combinators.subparse
default-combinators.('subparse-match): (unrun s:match/)
default-combinators.('subparse:relax): (unrun s:relax/)
default-combinators.('subparse-match:relax): (unrun s:match:relax/)
s: ~


=== COMPATIBILITY FOR NON-TAG KEYWORD FORMS ===

; !!! This has been deprecated.  But it's currently possible to get this
; just by saying `end: <end>` and `here: <here>`.

comment [
    default-combinators.here: default-combinators.<here>
    default-combinators.end: default-combinators.<end>
]


comment [combinatorize: func [

    "Analyze combinator parameters in rules to produce a specialized parser"

    return: "Parser function taking only input, and advanced rules position"
        [~[action! block!]~]
    combinator "Parser combinator taking input, but also other parameters"
        [frame!]
    rules [block!]
    state "Parse State" [frame!]
    :value "Initiating value (if datatype)" [element?]
    <local> r f rule-start
][
    rule-start: back rules  ; value may not be set if WORD! dispatch

    ; Combinators take arguments.  If the arguments are quoted, then they are
    ; taken literally from the rules feed.  If they are not quoted, they will
    ; be another "parser" generated from the rules.
    ;
    ; For instance: CHANGE takes two arguments.  The first is a parser and has
    ; to be constructed with PARSIFY from the rules.  But the replacement is a
    ; literal value, e.g.
    ;
    ;      >> data: "aaabbb"
    ;      >> parse data [change some "a" "literal" some "b"]
    ;      == "literalbbb"
    ;
    ; So we see that CHANGE got SOME "A" turned into a parser action, but it
    ; received "literal" literally.  The definition of the combinator is used
    ; to determine the arguments and which kind they are.

    f: make frame! combinator

    for-each 'key words of f [
        let param: select f key
        case [
            key = 'input [
                ; All combinators should have an input.  But the
                ; idea is that we leave this unspecialized.
            ]
            key = 'remainder [
                ; The remainder is a return; responsibility of the caller, also
                ; left unspecialized.
            ]
            key = 'pending [
                ; same for pending, this is the data being gathered that may
                ; need to be discarded (gives impression of "rollback") and
                ; is the feature behind COLLECT etc.
            ]
            key = 'value [
                set key value
            ]
            key = 'state [  ; the "state" is currently the UPARSE frame
                set key state
            ]
            key = 'rule-start [
                set key rule-start
            ]
            key = 'rule-end [
                ; skip (filled in at end)
            ]
            param.literal [  ; literal element captured from rules
                r: rules.1
                any [
                    not r
                    find [, | ||] r
                ]
                then [
                    if not param.endable [
                        panic "Too few parameters for combinator"
                    ]
                    ; leave unset
                ]
                else [
                    set key either param.class = 'just [r] [inside rules r]
                    rules: next rules
                ]
            ]
            param.optional [
                ; Leave refinements alone, e.g. :only ... a general strategy
                ; would be needed for these if the refinements add parameters
                ; as to how they work.
            ]
            <default> [  ; another parser to combine with
                ;
                ; !!! At the moment we disallow SET with GROUP!.
                ; This could be more conservative to stop calling
                ; functions.  For now, just work around it.
                ;
                r: rules.1
                any [
                    not r
                    find [, | ||] r
                ]
                then [
                    if not param.endable [
                        panic "Too few parameters for combinator"
                    ]
                    set key null
                ]
                else [
                    set key [{_} rules]: parsify state rules
                ]
            ]
        ]
    ]

    f.rule-end: rules

    return pack [(runs f) rules]  ; rules has been advanced
]]


; PARSIFY: The parsify function's job is to produce a single parser that
; encapsulates one "expression" a parse block.  So if you give parsify the
; block `[opt some "a", while ["b"] (print "hi")]` it will give back a
; black-box parsing function that implements the logic of `[opt some "a"]`.
;
;  * The parser takes an INPUT series at a given position as an argument
;  * If it matches, it will give back a synthesized value and a remainder
;  * If it doesn't match, it will return an error
;
; Parsify searches for elements in the combinator map, and it will coordinate
; the connections between those combinators.  However, since combinators are
; not variadic, all the "variadicness" is implemented here in parsify.  A
; datatype combinator (such as that for a TAG!) can thus not acquire any
; arguments unless code is added here to facilitate that.
;
; 1. The concept behind COMMA! is to provide a delimiting between rules.  That
;    is handled by the BLOCK! combinator.  So if you see `[some, "a"]` in
;    PARSIFY, that is just running out of turn.
;
;    (It may seem making a dummy "comma combinator" for the comma type is a
;    good idea.  But that's an unwanted axis of flexibility, and if we defer
;    the error until the combinator is run, it might never be run.)
;
; 2. First thing we do is look up the item in combinators to see if there is a
;    literal match.  This out-prioritizes a more general lookup by type.
;    (Original design was that the type got first dibs, with the concept
;    that it would make it easier to substitute hooked combinators.  But
;    hooking an overriding combinator is kind of a contradiction in terms,
;    it just meant there were dummy combinators for things like TAG! that
;    had to tunnel unknown conventions they did not understand.)
;
; 3. If not a FRAME! combinator, then you can map to an element which will
;    be looked up as if it had been written there literally.  This can be
;    used e.g. to map `<whitespace>` to a rule like `[some whitespace-char]`.
;    Unlike binding, this works for types other than WORD!...and such
;    definitions added to the combinator table are available globally while
;    the parser runs.
;
; 4. If a WORD! looks up to a user-written combinator that takes parser
;    arguments, we have to handle it here instead of being able to fold that
;    into the generic combinator for the WORD! datatype.
;
; 5. UPARSE introduced the idea of "action combinators", where if a PATH!
;    starts with slash, it's considered an invocation of a normal function with
;    the results of combinators as its arguments.  This has to be hacked in
;    variadically here, because the number of arguments needed depends on
;    the number of arguments taken by the action.  For the moment, be weird
;    and customize the combinator with AUGMENT for each argument (parser1,
;    parser2, parser3...)
;
;    (Note that we do fall through to a PATH! combinator for paths that don't
;    end in slashes.  This permits--among other things--the Rebol2 parse
;    semantics to look up variables via PATH!.)
;
; 6. If all else fails, dispatch is given to the datatype.  To reiterate,
;    this kind of dispatch is not variadic.  For that reason, we are stuck
;    on the case of things like dispatch to CHAIN!, because we want `word:`
;    to take an argument while `:(code)` does not.  Hence this hacks up an
;    answer of calling the types *: and :* depending.  Better answer needed.
;
parsify: func [
    "Transform one step's worth of rules into a parser combinator action"

    return: "Parser action for a full rule, advanced rules position"
        [~[action! block!]~]
    state "Parse state"
        [frame!]
    rules "Parse rules to (partially) convert to a combinator action"
        [block!]
    <local> r comb value
][
    r: rules.1

    if comma? r [  ; block combinator consumes the legal commas [1]
        return fail [
            "COMMA! can only be run between PARSE steps, not during them"
        ]
    ]
    rules: my next

    while [comb: select state.combinators r] [  ; literal match first [2]
        if match frame! comb [
            return combinatorize comb rules state
        ]

        r: comb  ; didn't look up to combinator, just element to substitute [3]
    ]

    r: inside state.env r  ; add binding if applicable

    ;--- SPECIAL-CASE VARIADIC SCENARIOS BEFORE DISPATCHING BY DATATYPE

    case [
        match [tuple! word!] r [  ; non-"keyword" WORD! (no literal lookup)
            ^value: get r else [
                return fail ["Null while fetching" mold r]
            ]
            if trash? ^value [
                return fail ["Trash while fetching" mold r]
            ]
            if action? ^value [
                return fail (
                    compose "ACTION! fetched, use /(mold r) if intentional"
                )
            ]

            if comb: match frame! value [  ; variable held a combinator [4]
                if combinator? comb [
                    return combinatorize comb rules state
                ]

                let name: uppercase to text! r
                return fail [
                    name "is not a COMBINATOR ACTION!"
                    "For non-combinator actions in PARSE, use"
                        unspaced [name "/"]
                ]
            ]

            ; fall through to datatype-based WORD! combinator handling
        ]

        (path? r) and (space? first r) [  ; "action combinator" [5]
            if not frame? let gotten: unrun get resolve r [
                return fail "PARSE PATH starting in / must be action or frame"
            ]
            if not comb: select state.combinators action! [
                return fail (
                    "No action! combinator, can't use PATH starting with /"
                )
            ]

            comb: unrun adapt (augment comb inside [] collect [
                let n: 1
                for-each 'key (words of gotten) [
                    let param: select gotten key
                    if not param.optional [
                        keep spread compose [
                            (to word! unspaced ["param" n]) [action!]
                        ]
                        n: n + 1
                    ]
                ]
            ])[
                parsers: copy []

                ; No RETURN visible in ADAPT.  :-/  Should we use ENCLOSE
                ; to more legitimately get the frame as a parameter?
                ;
                let f: binding of $param1

                let n: 1
                for-each 'key (words of value) [
                    let param: select value key
                    if not param.optional [
                        append parsers unrun f.(join 'param [n])/
                        n: n + 1
                    ]
                ]
            ]

            return combinatorize:value comb rules state gotten
        ]

        (path? r) and (space? last r) [  ; type constraint combinator
            let ^action: get resolve r  ; ^ to not store ACTION as label
            comb: state.combinators.match
            return combinatorize:value comb rules state action/
        ]

        ; !!! Here is where we would let GET-TUPLE! and GET-WORD! be used to
        ; subvert keywords if SEEK were universally adopted.
    ]

    if not comb: any [  ; datatype dispatch, special case sequences [6]
        if any-sequence? r [any [
            all [
                space? last r
                comb: try state.combinators.(as type of r '*.)  ; hack!
            ]
            all [
                space? first r
                comb: try state.combinators.(as type of r '.*)  ; hack!
            ]
        ]]
        select state.combinators (type of r)  ; datatypes dispatch meta
    ] [
        return fail [
            "Unhandled type in PARSIFY:" to word! type of r "-" mold r
        ]
    ]

    return combinatorize:value comb rules state r
]


=== ENTRY POINTS: PARSE*, PARSE ===

; The historical Redbol PARSE was focused on returning a LOGIC! so that you
; could write `if parse data rules [...]` and easily react to finding out if
; the rules matched the input to completion.
;
; But UPARSE goes deeper by letting rules evaluate to arbitrary results, and
; bubbles out the final result.  All values are in-band, so NULL can't be
; drawn out as signals of a lack of completion...definitional errors, and
; hence EXCEPT or RESCUE must be used:
;
;     result: parse data rules except [...]
;
; But PARSE-MATCH is a simple wrapper that returns the input or NULL:
; or it returns null:
;
;     >> parse-match "aaa" [some #a]
;     == "aaa"
;
;     >> parse-match "aaa" [some #b]
;     == ~null~  ; anti
;
; So for those who prefer to use IF to know about a parse's success or failure
; they can just use PARSE-MATCH instead.
;
; There is a core implementation for this called PARSE*, which is exposed
; separately in order to allow handling the case when pending values have
; not been all emitted.  This is utilized by things like the underlining
; EPARSE demo in the web console, where combinators are used to mark ranges
; in the text...but these need to be able to participate in the rollback
; mechanism.  So they are gathered in pending.

parse*: func [
    "Process as much of the input as parse rules consume (see also PARSE)"

    return: "Synthesized value from last match rule, and any pending values"
        [~[any-value? [blank? block!]]~ error!]
    input "Input data"
        [<opt-out> any-series? url! any-sequence?]
    rules "Block of parse rules"
        [block!]
    :combinators "List of keyword and datatype handlers used for this parse"
        [map!]
    :case "Do case-sensitive matching"
    :relax "Don't require reaching the tail of the input for success"
    :part "FAKE :PART FEATURE - runs on a copy of the series!"
        [integer! any-series?]
    :hook "Call a hook on dispatch of each combinator"
        [<unrun> frame!]

    <local> loops furthest ^synthesized remainder pending env
][
    ; PATH!s, TUPLE!s, and URL!s are read only and don't have indices.  But we
    ; want to be able to parse them, so make them read-only series aliases:
    ;
    ; https://forum.rebol.info/t/1276/16
    ;
    lib/case [  ; !!! Careful... :CASE is a refinement to this function
        any-sequence? input [input: as block! input]
        url? input [input: as text! input]
    ]

    loops: copy []  ; need new loop copy each invocation

    ; We put an implicit PHASE bracketing the whole of UPARSE* so that the
    ; <delay> groups will be executed.
    ;
    rules: inside rules bindable reduce ['phase bindable rules]

    ; There's no first-class ENVIRONMENT! type, so lists are used as a
    ; surrogate for exposing their bindings.  When the environment is
    ; expanded (e.g. by LET) the rules block in effect is changed to a block
    ; with the same contents (array+position) but a different binding.
    ;
    env: rules

    ; !!! Red has a :PART feature and so in order to run the tests pertaining
    ; to that we go ahead and fake it.  Actually implementing :PART would be
    ; quite a tax on the combinators...so thinking about a system-wide slice
    ; capability would be more sensible.  The series will not have the same
    ; identity, so mutating operations will mutate the wrong series...we
    ; could copy back, but that just shows what a slippery slope this is.
    ;
    if part [
        input: copy:part input part
    ]

    combinators: default [default-combinators]

    ; The COMBINATOR definition makes a function which is hooked with code
    ; that will mark the furthest point reached by any match.
    ;
    furthest: input

    ; Each UPARSE operation can have a different set of combinators in
    ; effect.  So it's necessary to need to have some way to get at that
    ; information.  For now, we use the FRAME! of the parse itself as the
    ; way that data is threaded...as it gives access to not just the
    ; combinators, but also the :VERBOSE or other settings...we can add more.
    ;
    let state: binding of $return

    let f: make frame! combinators.(block!)
    f.state: state
    set-combinator-input f input  ; don't know what BLOCK! combinator called it
    f.value: rules

    ; There's a display issue with giving the whole rule in that the outermost
    ; block isn't positioned within another block, so it would have to be
    ; nested in a singular block to get brackets on it and suggest you are
    ; in a block rule.  But more generally, giving the entire parse rule as
    ; a parse step in the display isn't that helpful--you know you're running
    ; the whole rule.  Revisit if this turns out to be a problem.
    ;
    f.rule-start: null
    f.rule-end: null

    sys.util/recover:relax [  ; :RELAX allows RETURN from block
        [^synthesized remainder pending]: eval f except e -> [
            assert [empty? state.loops]
            pending: blank  ; didn't get assigned due to error
            return fail e  ; wrappers catch
        ]
    ] then e -> [
        print "!!! HARD FAIL DURING PARSE !!!"
        print mold:limit state.rules 200
        panic e
    ]

    assert [empty? state.loops]

    if (not tail? remainder) and (not relax) [
        return fail make warning! [
            id: 'parse-incomplete
            message:
              "PARSE partially matched the input, but didn't reach the tail"
        ]
    ]

    return pack [^synthesized pending]
]

parse: redescribe [
    "Process input in the parse dialect, definitional error on failure"
](
    enclose parse*/ func [f] [
        let [^synthesized pending]: trap eval-free f

        if not empty? pending [
            panic "PARSE completed, but pending list was not empty"
        ]
        return ^synthesized
    ]
)

; We do not implement PARSE-MATCH by rewriting the rules to [(rules) <input>]
; to avoid dependence on the definition of the <input> combinator.
;
/parse-match: redescribe [
    "Process input using PARSE, return input on success or null on mismatch"
](
    enclose parse/ func [f] [
        eval f except [return null]
        return f.input  ; PARSE fixes name of INPUT (combinators can rename it)
    ]
)

; Due to the layering it's hard to write PARSE-THRU without using the ACCEPT
; and <here> combinators as they are defined.
;
/parse-thru: redescribe [
    "Process input using PARSE, return how far reached or null on mismatch"
](
    cascade [
        adapt parse:relax/ [  ; :RELAX irrelevant, drop from interface
            if combinators and (any [
                combinators.<here> != default-combinators.<here>
                combinators.accept != default-combinators.accept
            ]) [
                panic [
                    "Custom combinators redefining ACCEPT or <here> may"
                    "break PARSE-THRU.  Review if this comes up."
                ]
            ]
            rules: compose [(rules) accept <here>]
        ]
        try/  ; want to return NULL instead of error antiform on mismatch
    ]
)


sys.util.parse: parse/  ; !!! expose UPARSE to SYS.UTIL module, hack...


=== HOOKS ===

; These are some very primordial hooks; for an elaborate demo see EPARSE's
; rule-stepwise debugger.

parse-trace-hook: func [
    return: [pack!]
    f [frame!]
][
    let state: f.state

    if f.rule-start [
        print ["RULE:" mold spread copy:part f.rule-start f.rule-end]
    ]

    let ^result: eval f except e -> [
        print ["RESULT': FAIL"]
        return fail e
    ]

    print ["RESULT':" @result]

    return ^result
]

parse-trace: specialize parse/ [hook: parse-trace-hook/]


parse-furthest-hook: func [
    return: [pack!]
    f [frame!]
    var [word! tuple!]
][
    let ^result: trap eval f

    let remainder: unquote second unanti ^result
    if (index of remainder) > (index of get var) [
        set var remainder
    ]

    return ^result
]

/parse-furthest: adapt augment parse/ [
    var "Variable to hold furthest position reached"
        [word! tuple!]
][
    /hook: specialize parse-furthest-hook/ compose [var: '(var)]
    set var input
]


=== "USING" FEATURE ===

; !!! This operation will likely take over the name USE.  It is put here since
; the UPARSE tests involve it.
;
using: func [
    return: []  ; should it return a value?  (e.g. the object?)
    obj [<opt-out> object!]
][
    add-use-object (binding of $obj) obj
    return ~
]
