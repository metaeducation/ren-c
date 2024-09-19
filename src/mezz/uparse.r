Rebol [
    Title: "UPARSE: Usermode Implementation of Evolved PARSE Dialect"
    License: "LGPL 3.0"

    Type: module
    Name: Usermode-PARSE

    Exports: [
        combinator
        parse
        default-combinators
        parse-trace-hook
        using  ; TBD: will become new meaning of USE
    ]

    Description: {
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
        by an individual `parser` function.  The parameters to this function
        are stylized so that the parse engine can compose these smaller
        parsers together as part of larger processing operations.  This
        approach is typically called "Parser Combinators":

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
    }

    Notes: {
        * This implementation will be *extremely* slow for the foreseeable
          future.  But since it is built on usermode facilities, any
          optimizations that are applied to it will bring systemic benefits.
          Ultimately the goal is to merge this architecture in with the
          "messier" C code...hopefully preserving enough of the hackability
          while leveraging low-level optimizations where possible.
    }
]


; All combinators receive the INPUT to be processed.  They are also given an
; object representing the STATE of the parse (currently that is just the
; FRAME! of the main UPARSE call which triggered the operation, so any of the
; locals or parameters to that can be accessed, e.g. the list of /COMBINATORS)
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
; with "A", to get a composite parser that implements `opt some "a"`.
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
    {Make stylized code that fulfills the interface of a combinator}

    return: [frame!]

    spec [block!]
    body [block!]

    <static> wrapper (
        func [
            {Enclosing function for hooking all combinators}
            return: [pack?]
            f [frame!]
        ][
            return either f.state.hook [
                run f.state.hook f
            ][
                eval/undecayed f
            ]
        ]
    )
][
    let autopipe: ~

    let action: func compose [
        ; Get the text description if given
        (if text? spec.1 [spec.1, elide spec: my next])

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
            spread compose/deep [
                return: (maybe description)
                    [~[(types) any-series? [blank! block!]]~]
            ]
        )

        /remainder [any-series?]  ; all combinators have remainder

        (if '/pending = try spec.1 [
            assert [spec.2 = [blank! block!]]
            autopipe: 'no  ; they're asking to handle pending themselves
            spread reduce ['/pending spec.2]
            elide spec: my skip 2
        ] else [
            autopipe: 'yes  ; they didn't mention pending, handle automatically
            spread [/pending [blank! block!]]
        ])

        state [frame!]
        input [any-series?]

        ; Whatever arguments the combinator takes, if any
        ;
        (spread spec)
    ] compose [
        ;
        ; !!! If we are "autopipe" then we need to make it so the parsers that
        ; we receive in will automatically bubble up their pending contents in
        ; order of being called.

        (spread if yes? autopipe '[
            let f: binding of $return

            pending: _
            let in-args: 'no
            for-each [key val] f [
                if no? in-args [
                    if key = 'input [in-args: 'yes]
                    continue
                ]
                all [
                    not unset? $val
                    action? :val
                ] then [
                    ; All parsers passed as arguments, we want it to be
                    ; rigged so that their results append to an aggregator in
                    ; the order they are run (if they succeed).
                    ;
                    f.(key): enclose (augment :val [/modded]) func [
                        f2
                        <local> result' remainder subpending
                    ][
                        [^result' remainder subpending]:
                            eval/undecayed f2 except e -> [
                                return raise e
                            ]
                        pending: glom pending spread subpending
                        return pack [
                            unmeta result', remainder, subpending
                        ]
                    ]
                ]
            ]
        ])

        return: lambda [^atom] compose/deep [
            (unrun :return) pack [
                unmeta atom except e -> [(unrun :return) raise e]
                remainder
                pending
            ]
        ]

        ; ** Currently parsers unify RETURN where a failure is done with
        ; a `return raise`.  Should this instead be ACCEPT and REJECT, as
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
    return unrun enclose augment :action [
        /rule-start [block!] /rule-end [block!]
    ] :wrapper  ; returns plain ACTION!
]


; It should be possible to find out if something is a combinator in a more
; rigorous way than this.  But just check the parameters for now.
;
combinator?: func [
    {Crude test to try and determine if an ACTION! is a combinator}
    return: [logic?]
    frame [<unrun> frame!]
    <local> keys
][
    keys: words of frame  ; test if it's a combinator
    ;
    ; could also check RETURNS OF for `pending` and `state`, but might exclude
    ; wrapped combinators which return packs directly.
    ;
    return did all [
        find keys 'input
        find keys 'state
    ]
]

negatable-parser?: func [
    return: [logic?]
    frame [<unrun> frame!]
][
    return did find words of frame 'negated
]


block-combinator: ~  ; need in variable for recursion implementing "..."

; !!! We use a MAP! here instead of an OBJECT! because if it were MAKE OBJECT!
; then the parse keywords would override the Rebol functions (so you couldn't
; use ANY during the implementation of a combinator, because there's a
; combinator named ANY).  This is part of the general issues with binding that
; need to have answers.
;
default-combinators: make map! reduce [

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
        {If the parser argument is negatable, invoke it in the negated sense}
        return: [any-value? pack?]
        parser [action?]
        /negated
    ][
        if negated [  ; NOT NOT, e.g. call parser without negating it
            return [@ remainder]: parser input except e -> [
                return raise e
            ]
        ]
        if not negatable-parser? :parser [
            fail "NOT called on non-negatable combinator"
        ]
        return [@ remainder]: parser/negated input except e -> [
            return raise e
        ]
    ]

    === WHEN COMBINATOR ===

    'when combinator [
        "If parser's synthesized result is null, skip to next alternate"
        return: [any-value?]
        parser [action?]
        <local> result
    ][
        [result remainder]: parser input except e -> [
            return raise e  ; non-matching parser means WHEN did not match
        ]
        if void? :result [
            fail "WHEN combinator received ~void~ antiform result"
        ]
        if null? :result [
            return raise "WHEN combinator received ~null~ antiform result"
        ]
        return :result  ; can say e.g. [x: when (next var)] if you like
    ]

    === BYPASS COMBINATOR ===

    'bypass combinator [
        "Stop the current rule chain, and skip to the next `|` alternate"
        return: []
        ; /negated  <- could this be negated to not bypass?
    ][
        return raise "BYPASS to next alternate rule requested"
    ]

    === BASIC KEYWORDS ===

    'try combinator [
        {If applying parser fails, succeed and return NULL; don't advance input}
        return: "PARSER's result if it succeeds, otherwise NULL"
            [any-value? pack?]
        parser [action?]
        <local> result'
    ][
        fail "TRY combinator changed back to OPTIONAL (or abbreviate as OPT)"
    ]

    'optional combinator [
        {If applying parser fails, succeed and return NULL; don't advance input}
        return: "PARSER's result if it succeeds, otherwise NULL"
            [any-value? pack?]
        parser [action?]
        <local> result'
    ][
        [^result' remainder]: parser input except e -> [
            remainder: input  ; succeed on parser fail but don't advance input
            return null
        ]
        return unmeta result'  ; return successful parser result
    ]

    'spread combinator [
        {Return antiform group for list arguments}
        return: "Splice antiform if input is list"
            [~null~ ~void~ element? splice?]
        parser [action?]
        <local> result'
    ][
        [^result' remainder]: parser input except e -> [
            return raise e
        ]
        if (not quoted? result') or (not any-list? result': unquote result') [
            fail "SPREAD only accepts ANY-LIST? and QUOTED!"
        ]
        return spread result'  ; was unquoted above
    ]

    'ahead combinator [
        {Leave the parse position at the same location, but fail if no match}
        return: "parser result if success, NULL if failure"
            [any-value? pack?]
        parser [action?]
        /negated
    ][
        remainder: input  ; never advances
        if negated [
            parser input except e -> [
                return ~not~
            ]
            return raise "Negated parser passed to AHEAD succeded"
        ]
        return parser input  ; don't care about what parser's remainder is
    ]

    'further combinator [
        {Pass through the result only if the input was advanced by the rule}
        return: "parser result if it succeeded and advanced input, else NULL"
            [any-value? pack?]
        parser [action?]
        <local> result' pos
    ][
        [^result' pos]: parser input except e -> [
            return raise e  ; the parse rule did not match
        ]
        if (index? pos) <= (index? input) [
            return raise "FURTHER rule matched, but did not advance the input"
        ]
        remainder: pos
        return unmeta result'
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
        {Run the parser argument in a loop, requiring at least one match}
        return: "Result of last successful match"
            [any-value? pack?]
        parser [action?]
        <local> result'
    ][
        append state.loops binding of $return

        [^result' input]: parser input except e -> [
            take/last state.loops
            return raise e
        ]
        cycle [  ; if first try succeeds, we'll succeed overall--keep looping
            [^result' input]: parser input except [
                take/last state.loops
                remainder: input
                return unmeta result'
            ]
        ]
        fail ~<unreachable>~
    ]

    'while combinator [
        {Run the body parser in a loop, for as long as condition matches}
        return: "Result of last body parser (or void if body never matched)"
            [any-value? pack?]
        condition-parser [action?]
        body-parser [action?]
        <local> result'
    ][
        append state.loops binding of $return

        result': void'

        cycle [
            [^ input]: condition-parser input except [
                take/last state.loops
                remainder: input
                return unmeta result'
            ]

            ; We don't worry about the body parser's success or failure, but
            ; if it fails we disregard the result
            ;
            [^result' input]: body-parser input except [
                result': nihil'
                continue
            ]
        ]
        fail ~<unreachable>~
    ]

    'cycle combinator [
        {Run the body parser continuously in a loop until BREAK or STOP}
        return: "Result of last body parser (or void if body never matched)"
            [any-value? pack?]
        parser [action?]
        <local> result'
    ][
        append state.loops binding of $return

        result': void'

        cycle [
            [^result' input]: parser input except [
                result': nothing'
                continue
            ]
        ]
        fail ~<unreachable>~
    ]

    'tally combinator [
        {Iterate a rule and count the number of times it matches}
        return: "Number of matches (can be 0)"
            [integer!]
        parser [action?]
        <local> count
    ][
        append state.loops binding of $return

        count: 0
        cycle [
            [^ input]: parser input except [
                take/last state.loops
                remainder: input
                return count
            ]
            count: count + 1
        ]
        fail ~<unreachable>~
    ]

    'break combinator [
        {Break an iterated construct like SOME or REPEAT, failing the match}
        return: []
        <local> f
    ][
        f: take/last state.loops except [
            fail "No PARSE iteration to BREAK"
        ]

        f.remainder: input
        f/return raise "BREAK encountered"
    ]

    'stop combinator [
        {Break an iterated construct like SOME or REPEAT, succeeding the match}
        return: []
        parser [<end> action?]
        <local> f result'
    ][
        result': nothing'  ; default `[stop]` returns nothing
        if :parser [  ; parser argument is optional
            [^result' input]: parser input except e -> [
                return raise e
            ]
        ]

        f: take/last state.loops except [
            fail "No PARSE iteration to STOP"
        ]

        f.remainder: input
        f/return unmeta result'
    ]

   === ACCEPT KEYWORD ===

    ; ACCEPT is like R3-Alpha's RETURN keyword, but with the name adjusted to
    ; help distinguish it from the overall return of a function (as well as cue
    ; the reader it is not unconditional, e.g. you can say something like
    ; `[accept integer! | ...]`).
    ;
    ; RETURN was removed for a time in Ren-C due to concerns about how it
    ; could lead to abruptly ending a parse before all the matching was
    ; complete.  Now UPARSE can return ANY-VALUE? (or PACK?) and the only
    ; reason you'd ever use ACCEPT would be specifically for the abrupt exit,
    ; so it's fit for purpose.

    'accept combinator [
        {Return a value explicitly from the parse, terminating early}
        return: []
        parser [action?]
        <local> value'
    ][
        [^value' _]: parser input except e -> [
            return raise e
        ]

        ; !!! STATE is filled in as the frame of the top-level UPARSE call.  If
        ; we UNWIND then we bypass any of its handling code, so it won't set
        ; things like PENDING (which gets type checked as a multi-return, so
        ; we can't leave it as unset).  Review.
        ;
        pending: _
        state/return pack [(unmeta value') pending]
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
        {Get the current series index of the PARSE operation}
        return: "The INDEX OF the parse position"
            [integer!]
    ][
        remainder: input
        return index of input
    ]

    'measure combinator [
        {Get the length of a matched portion of content}
        return: "Length in series units"
            [integer!]
        parser [action?]
        <local> start end
    ][
        [^ remainder]: parser input except e -> [return raise e]

        end: index of remainder
        start: index of input

        ; Because parse operations can SEEK, this can potentially create
        ; issues.  We currently fail if the index is before.
        ;
        if start > end [
            fail "Can't MEASURE region where rules did a SEEK before the INPUT"
        ]

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
        {Substitute a match with new data}
        return: [~change~]
        parser [action?]
        replacer [action?]  ; !!! How to say result is used here?
        <local> replacement'
    ][
        [^ remainder]: parser input except e -> [  ; first find end position
            return raise e
        ]

        [^replacement' _]: replacer input except e -> [
            return raise e
        ]

        ; CHANGE returns tail, use as new remainder
        ;
        remainder: change/part input (unmeta replacement') remainder
        return ~change~
    ]

    'remove combinator [
        {Remove data that matches a parse rule}
        return: [~remove~]
        parser [action?]
    ][
        [^ remainder]: parser input except e -> [  ; first find end position
            return raise e
        ]

        remainder: remove/part input remainder
        return ~remove~
    ]

    'insert combinator [
        {Insert literal data into the input series}
        return: [~insert~]
        parser [action?]
        <local> insertion'
    ][
        [^insertion' _]: parser input except e -> [  ; remainder ignored
            return raise e
        ]

        remainder: insert input (unmeta insertion')
        return ~insert~
    ]

    === SEEKING KEYWORDS ===

    'to combinator [
        {Match up TO a certain rule (result position before succeeding rule)}
        return: "The rule's product"
            [any-value? pack?]
        parser [action?]
        <local> result'
    ][
        cycle [
            [^result' _]: parser input except [
                if tail? input [  ; could be `to <end>`, so check tail *after*
                    return raise "TO did not find desired pattern"
                ]
                input: next input
                continue
            ]
            remainder: input  ; TO means do not include match range
            return unmeta result'
        ]
        fail ~<unreachable>~
    ]

    'thru combinator [
        {Match up THRU a certain rule (result position after succeeding rule)}
        return: "The rule's product"
            [any-value? pack?]
        parser [action?]
        <local> result'
    ][
        cycle [
            [^result' remainder]: parser input except [
                if tail? input [  ; could be `thru <end>`, check TAIL? *after*
                    return raise "THRU did not find desired pattern"
                ]
                input: next input
                continue
            ]
            return unmeta result'
        ]
        fail ~<unreachable>~
    ]

    'seek combinator [
        return: "seeked position"
            [any-series?]
        parser [action?]
        <local> where
    ][
        [^where remainder]: parser input except e -> [
            return raise e
        ]
        case [
            '~void~ = where [
                remainder: input
            ]
            quasi? where [
                fail "Cannot SEEK to antiform"
            ]

            (elide where: my unquote)

            integer? where [
                remainder: at head input where
            ]
            any-series? :where [
                if not same? head input head where [
                    fail "Series SEEK in UPARSE must be in the same series"
                ]
                remainder: where
            ]
            fail "SEEK requires INTEGER!, series position, or VOID"
        ]
        return remainder
    ]

    'between combinator [
        return: "Copy of content between the left and right parsers"
            [any-series?]
        parser-left [action?]
        parser-right [action?]
        <local> start limit
    ][
        [^ start]: (parser-left input) except e -> [
            return raise e
        ]

        limit: start
        cycle [
            [^ remainder]: (parser-right limit) except e -> [
                if tail? limit [  ; remainder of null
                    return raise e
                ]
                limit: next limit
                continue  ; don't try to assign the `[^ remainder]:`
            ]
            return copy/part start limit
        ]
        fail ~<unreachable>~
    ]

    === TAG! SUB-DISPATCHING COMBINATOR ===

    ; Historical PARSE matched tags literally, while UPARSE pushes to the idea
    ; that they are better leveraged as "special nouns" to avoid interfering
    ; with the user variables in wordspace.

    <here> combinator [
        {Get the current parse input position, without advancing input}
        return: "parse position"
            [any-series?]
    ][
        remainder: input
        return input
    ]

    <end> combinator [
        {Only match if the input is at the end}
        return: "Invisible"
            [nihil?]
        /negated
    ][
        remainder: input  ; never advances
        if tail? input [
            if negated [
                return raise "PARSE position at <end> (but parser negated)"
            ]
            return nihil
        ]
        if negated [
            return nihil
        ]
        return raise "PARSE position not at <end>"
    ]

    <input> combinator [
        {Get the original input of the PARSE operation}
        return: "parse position"
            [any-series?]
    ][
        remainder: input
        return state.input
    ]

    <subinput> combinator [
        {Get the input of the SUBPARSE operation}
        return: "parse position"
            [any-series?]
    ][
        remainder: input
        return head of input  ; !!! What if SUBPARSE series not at head?
    ]

    === ACROSS (COPY?) ===

    ; Historically Rebol used COPY to mean "match across a span of rules and
    ; then copy from the first position to the tail of the match".  That could
    ; have assignments done during, which extract some values and omit others.
    ; You could thus end up with `y: copy x: ...` and wind up with x and y
    ; being different things, which is not intuitive.

    'across combinator [
        {Copy from the current parse position through a rule}
        return: "Copied series"
            [any-series?]
        parser [action?]
    ][
        [^ remainder]: parser input except e -> [
            return raise e
        ]
        if any-list? input [
            return as block! copy/part input remainder
        ]
        if any-string? input [
            return as text! copy/part input remainder
        ]
        return copy/part input remainder
    ]

    'copy combinator [
        {Disabled combinator, included to help guide to use ACROSS}
        return: []
    ][
        fail [
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

    'subparse combinator [
        "Recursion into other data with a rule, result of rule if match"
        return: "Result of the subparser"
            [any-value? pack?]
        parser [action?]  ; !!! Easier expression of value-bearing parser?
        subparser [action?]
        <local> subseries result'
    ][
        [^subseries remainder]: parser input except e -> [
            ;
            ; If the parser in the first argument can't get a value to subparse
            ; then we don't process it.
            ;
            ; !!! Review: should we allow non-value-bearing parsers that just
            ; set limits on the input?
            ;
            return raise e
        ]

        if void' = subseries [
            fail "Cannot SUPBARSE into a void"
        ]

        if quasi? subseries [
            fail "Cannot SUBPARSE an antiform synthesized result"
        ]

        subseries: my unquote

        case [
            any-series? subseries []
            any-sequence? subseries [subseries: as block! subseries]
            return raise "Need SERIES! or SEQUENCE! input for use with SUBPARSE"
        ]

        ; If the entirety of the item at the list is matched by the
        ; supplied parser rule, then we advance past the item.
        ;
        [^result' subseries]: subparser subseries except e -> [return raise e]
        if not tail? subseries [
            return raise "SUBPARSE rule succeeded, but didn't complete subseries"
        ]
        return unmeta result'
    ]

    'validate combinator [
        "Recurse into other data with a rule, other data if match"
        return: "Input if the rules matched"
            [any-series?]
        parser [action?]  ; !!! Easier expression of value-bearing parser?
        subparser [action?]
        <local> subseries subremainder
    ][
        [^subseries remainder]: parser input except e -> [
            ;
            ; If the parser in the first argument can't get a value to subparse
            ; then we don't process it.
            ;
            ; !!! Review: should we allow non-value-bearing parsers that just
            ; set limits on the input?
            ;
            return raise e
        ]

        if void' = subseries [
            fail "Cannot VALIDATE a void"
        ]

        if quasi? subseries [
            fail "Cannot VALIDATE an antiform synthesized result"
        ]

        subseries: my unquote

        case [
            any-series? subseries []
            any-sequence? subseries [subseries: as block! subseries]
            return raise "Need SERIES! or SEQUENCE! input for use with VALIDATE"
        ]

        ; If the entirety of the item at the input list is matched by the
        ; supplied parser rule, then we advance past the item.
        ;
        [_ subremainder]: subparser subseries except e -> [
            return raise e
        ]
        if not tail? subremainder [
            return raise "VALIDATE rule succeeded, but didn't complete subseries"
        ]
        return subseries
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
        /pending [blank! block!]
        parser [action?]
        <local> collected
    ][
        [^ remainder pending]: parser input except e -> [
            return raise e
        ]

        ; PENDING can be NULL or a block full of items that may or may
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
            [any-value?]
        /pending [blank! block!]
        parser [action?]
        <local> result'
    ][
        [^result' remainder pending]: parser input except e -> [
            return raise e
        ]
        if pack? unmeta result' [  ; KEEP picks first pack item
            result': (try first unquasi result') else [  ; empty pack, ~[]~
                fail "Can't KEEP NIHIL"
            ]
        ]
        if void? unmeta result' [
            pending: _
            return void
        ]

        ; Since COLLECT is considered the most common `pending` client, we
        ; reserve QUOTED! items for collected things.
        ;
        ; All collect items are appended literally--so COLLECT itself doesn't
        ; have to worry about splicing.  More efficient concepts (like double
        ; quoting normal items, and single quoting spliced blocks) could be
        ; used instead--though copies of blocks would be needed either way.
        ;
        case [
            quoted? result' [  ; unmeta'd result asked to be kept literally
                pending: glom pending result'  ; retain meta quote as signal
            ]
            splice? unmeta result' [
                for-each 'item unquasi result' [
                    ;
                    ; We quote to signal that this pending item targets COLLECT.
                    ;
                    pending: glom pending quote item
                ]
            ]
            fail "Incorrect KEEP (not value or SPREAD)"
        ]

        return unmeta result'
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
        parser [action?]
        <local> collected
    ][
        collected: copy []
        remainder: input
        cycle [
            append collected (
                [@ remainder]: parser remainder except e -> [
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
    ;     print [x "is one and" y "is {hi}"]
    ;
    ; The idea is interesting enough that it suggests being able to EMIT with
    ; no GATHER in effect, and then have the RETURN GATHER semantic.

    'gather combinator [
        return: "The gathered object"
            [object!]
        /pending [blank! block!]
        parser [action?]
        <local> obj
    ][
        [^ remainder pending]: parser input except e -> [
            return raise e
        ]

        ; Currently we assume that all the BLOCK! items in the pending are
        ; intended for GATHER.

        obj: make object! collect [
            remove-each 'item pending [
                if block? item [keep spread item, okay]
            ] else [
                ; should it error or fail if pending was NULL ?
            ]
        ]

        return obj
    ]

    'emit combinator [
        return: "The emitted value"
            [any-value?]
        /pending [blank! block!]
        @target [set-word! set-group!]
        parser [action?]
        <local> result'
    ][
        if set-group? target [
            if not any-word? (target: eval as block! target) [
                fail [
                    "GROUP! from EMIT (...): must produce an ANY-WORD?, not"
                    ^target
                ]
            ]

            ; !!! MAKE OBJECT! fails if any top-level SET-WORD! have bindings.
            ; Revisit if that rule changes and this becomes unnecessary.
            ;
            if binding of target [
                fail ["EMIT can't use bound words for object keys:" target]
            ]
        ] else [
            target: unbind target  ; `emit foo:` okay in function defining foo
        ]

        [^result' remainder pending]: parser input except e -> [
            return raise e
        ]

        ; The value is quoted or quasi because of ^ on ^(parser input).
        ; This lets us emit antiforms, since the MAKE OBJECT! evaluates.
        ;
        pending: glom pending :[as set-word! unbind target result']
        return unmeta result'
    ]

    === SET-WORD! and SET-TUPLE! COMBINATOR ===

    ; The concept behind Ren-C's SET-WORD! in PARSE is that parse combinators
    ; don't just update the remainder of the parse input, but they also return
    ; values.  If these appear to the right of a set-word, then the set word
    ; will be assigned on a match.
    ;
    ; !!! SET-PATH! is not supported, as paths will be used for functions only.

    set-word! combinator [
        return: "The set value"
            [any-value?]
        value [set-word!]
        parser "Failed parser will means target SET-WORD! will be unchanged"
            [action?]
        <local> result'
    ][
        [^result' remainder]: parser input except e -> [return raise e]

        return set value unmeta result'
    ]

    set-tuple! combinator [
        return: "The set value"
            [any-value?]
        value [set-tuple!]
        parser "Failed parser will means target SET-TUPLE! will be unchanged"
            [action?]
        <local> result'
    ][
        [^result' remainder]: parser input except e -> [return raise e]

        return set value unmeta result'
    ]

    'set combinator [
        {Disabled combinator, included to help guide to use SET-WORD!}
        return: []
    ][
        fail [
            "The SET keyword in UPARSE is done with SET-WORD!, and if SET does"
            "come back it would be done differently:"
            https://forum.rebol.info/t/1139
        ]
    ]

    set-group! combinator [
        return: "The set value"
            [any-value?]
        value [set-group!]
        parser "Failed parser will means target will be unchanged"
            [action?]
        <local> result'
    ][
        let var: eval value
        [^result' remainder]: parser input except e -> [return raise e]
        return set var unmeta result'
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
        value [text!]
        <local> neq?
    ][
        case [
            any-list? input [
                neq?: either state.case [:strict-not-equal?] [:not-equal?]
                if neq? try input.1 value [
                    return raise "Value at parse position does not match TEXT!"
                ]
                remainder: next input
                return input.1
            ]

            ; for both of these cases, we have to use the rule, since there's
            ; no isolated value to capture.  Should we copy it?

            any-string? input [
                [_ remainder]: apply get $find [
                    input value
                    /match ok
                    /case state.case
                ] else [
                    return raise "String at parse position does not match TEXT!"
                ]
            ]
            <default> [
                assert [binary? input]
                [_ remainder]: apply get $find [
                    input value
                    /match ok
                    /case state.case
                ] else [
                    return raise "Binary at parse position does not match TEXT!"
                ]
            ]
        ]

        return value
    ]

    === TOKEN! COMBINATOR (currently ISSUE! and CHAR!) ===

    ; The TOKEN! type is an optimized immutable form of string that will
    ; often be able to fit into a cell with no series allocation.  This makes
    ; it good for representing characters, but it can also represent short
    ; strings.  It matches case-sensitively.

    issue! combinator [
        return: "The token matched against (not input value)"
            [issue!]
        value [issue!]
        /negated
    ][
        if tail? input [
            return raise "ISSUE! cannot match at end of input"
        ]
        case [
            any-list? input [
                remainder: next input
                if value == input.1 [
                    if negated [
                        return raise "ISSUE! matched, but combinator negated"
                    ]
                    return input.1
                ]
                if negated [
                    return input.1
                ]
                return raise "Value at parse position does not match ISSUE!"
            ]
            any-string? input [
                if negated [
                    if (not char? value) [
                        fail "NOT ISSUE! with strings is only for single char"
                    ]
                    if input.1 = value [
                        return raise "Negated ISSUE! char matched at string"
                    ]
                    remainder: next input
                    return input.1
                ]
                [_ remainder]: find/match/case input value else [
                    return raise [
                        "String at parse position does not match ISSUE!"
                    ]
                ]
                return value
            ]
            <default> [
                assert [binary? input]
                if negated [
                    if (not char? value) or (255 < codepoint of value) [
                        fail "NOT ISSUE! with blobs is only for single byte"
                    ]
                    if input.1 = codepoint of value [
                        return raise "Negated ISSUE! char matched at blob"
                    ]
                    remainder: next input
                    return codepoint-to-char input.1
                ]
                [_ remainder]: find/match/case input value else [
                    return raise [
                        "Binary at parse position does not match ISSUE!"
                    ]
                ]
                return value
            ]
        ]
    ]

    === BINARY! COMBINATOR ===

    ; Arbitrary matching of binary against text is a bit of a can of worms,
    ; because if we AS alias it then that would constrain the binary...which
    ; may not be desirable.  Also you could match partial characters and
    ; then not be able to set a string position.  So we don't do that.

    binary! combinator [
        return: "The binary matched against (not input value)"
            [binary!]
        value [binary!]
    ][
        case [
            any-list? input [
                if value = try input.1 [
                    remainder: next input
                    return input.1
                ]
                return raise "Value at parse position does not match BINARY!"
            ]
            <default> [  ; Note: BITSET! acts as "byteset" here
                ; binary or any-string input
                [_ remainder]: find/match input value else [
                    return raise [
                        "Content at parse position does not match BINARY!"
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
            [any-value?]
        'vars [set-word!]
        parser [action?]
        <local> result'
    ][
        [^result' remainder]: parser input except e -> [
            return raise e
        ]
        state.env: add-let-binding state.env vars unmeta result'

        return unmeta result'
    ]

    === IN COMBINATOR ===

    ; Propagates the binding from <input> onto a value.
    ; Should be <in> but TAG! combinators can't take arguments yet.

    '*in* combinator [
        return: "Argument binding gotten INSIDE the current input"
            [any-value?]
        parser [action?]
        <local> result'
    ][
        [^result' remainder]: parser input except e -> [
            return raise e
        ]

        return inside state.input unmeta result'
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

    group! combinator [
        return: "Result of evaluating the group (invisible if <delay>)"
            [any-value? pack?]
        /pending [blank! block!]
        value [any-list?]  ; allow any array to use this "EVAL combinator"
    ][
        remainder: input

        if tail? value [
            pending: _
            return nihil
        ]

        if <delay> = first value [
            if length of value = 1 [
                fail "Use ('<delay>) to evaluate to the tag <delay> in GROUP!"
            ]
            pending: reduce [next value]  ; GROUP! signals delayed groups
            return nihil  ; act invisible
        ]

        pending: _
        return eval/undecayed value  ; !!! Pass on definitional failure?
    ]

    'phase combinator [
        return: "Result of the parser evaluation"
            [any-value? pack?]
        /pending [blank! block!]
        parser [action?]
        <local> result'
    ][
        [^result' remainder pending]: parser input except e -> [
            return raise e
        ]

        ; Run GROUP!s in order, removing them as one goes
        ;
        remove-each 'item pending [
            if group? item [eval item, okay]
        ]

        return unmeta result'
    ]

    === GET-GROUP! COMBINATOR ===

    ; This allows the running of a rule generated by code, as if it had been
    ; written in the spot where the GET-GROUP! is.
    ;
    ; !!! The rules of what are allowed or not when triggering through WORD!s
    ; likely apply here.  Should that be factored out?
    ;
    ; 1. Raising errors out of combinators has a specific meaning of the
    ;    combinator not matching.  We do not want to bubble out a definitional
    ;    error indicating a problem inside the evaluation in a way that would
    ;    conflate it with the combinator simply not matching.  Must fail.
    ;
    ; 2. NULL was originally prohibited, in the spirit of safety (kind of like
    ;    not casually vaporizing nulls when used in COMPOSE groups).  But
    ;    with "Flexible Logic", that safety broke the case of conditional
    ;    tests like :(mode = 'read) because they now returned either ~okay~
    ;    or ~null~ antiforms.  The safety tradeoff just doesn't work out to
    ;    be worth it (other systemic NULL safety is being provided now with
    ;    things like needing TRY with PICK and tuple on series to get null).
    ;
    ; 3. Originally returning void would vaporize the expression.  But this
    ;    makes it impossible to synthesize void from a GET-GROUP!, and it
    ;    might be useful to do so, e.g.
    ;
    ;        parse [a b c] [append/ (data) :(if flag '[across some word!])]
    ;
    ;    So instead, you can use ~okay~ to ask the GET-GROUP! to vaporize.
    ;
    ; 4. The original GET-GROUP! concept allowed:
    ;
    ;        parse "aaa" [:('some) "a"]
    ;
    ;    To do that variadically would require some kind of back-channel of
    ;    communication to the BLOCK! combinator to return the material, or
    ;    have specific code in the BLOCK! combinator to process GET-GROUP!.
    ;    It's of questionable value--while this as an independent combinator
    ;    has clear value.  However, arity-0 combinators may be an exception
    ;    here (in particular, BYPASS).  Permit it inefficiently for now by
    ;    enclosing in a block.
    ;
    ; 5. We don't need to call COMBINATORIZE because we can't handle non
    ;    arity-0 combinators here.  But this should have better errors if the
    ;    datatype combinator takes arguments.

    get-group! combinator [
        return: "Result of running combinator from fetching the WORD!"
            [any-value? pack?]
        /pending [blank! block!]   ; we retrigger combinator; it may KEEP, etc.

        value [any-list?]  ; allow any array to use this "REPARSE-COMBINATOR"
        <local> r comb
    ][
        r: meta eval/undecayed value except e -> [fail e]  ; can't raise [1]

        if r = null' [  ; like [:(1 = 0)]
            return raise "GET-GROUP! evaluated to NULL"  ; means no match [2]
        ]

        pending: _
        remainder: input

        any [
            r = okay'  ; like [:(1 = 1)]
            r = nihil'  ; like [:(comment "hi")]
        ] then [
            return nihil  ; invisible
        ]

        if r = void' [  ; like [:(if 1 = 0 [...])]
            return void  ; couldn't produce void at all if vaporized [3]
        ]

        r: unmeta r  ; only needed as ^META to check for NIHIL

        if word? r [
            r: :[r]  ; enable 0-arity combinators [4]
        ]

        if not comb: select state.combinators type of r [
            fail ["Unhandled type in GET-GROUP! combinator:" mold type of r]
        ]

        return [@ remainder pending]: run comb state input r  ; [5]
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

    get-block! combinator [
        return: []
        value [get-block!]
    ][
        fail "No current meaning for GET-BLOCK! combinator"
    ]

    === BITSET! COMBINATOR ===

    ; There is some question here about whether a bitset used with a BINARY!
    ; can be used to match UTF-8 characters, or only bytes.  This may suggest
    ; a sort of "INTO" switch that could change the way the input is being
    ; viewed, e.g. being able to do INTO BINARY! on a TEXT! (?)

    bitset! combinator [
        return: "The matched input value"
            [char? integer!]
        value [bitset!]
    ][
        if tail? input [
            return raise "BITSET! cannot match at end of input"
        ]
        case [
            any-list? input [
                if value = input.1 [
                    remainder: next input
                    return input.1
                ]
            ]
            any-string? input [
                if pick value input.1 [
                    remainder: next input
                    return input.1
                ]
            ]
            <default> [
                assert [binary? input]
                if pick value input.1 [
                    remainder: next input
                    return input.1
                ]
            ]
        ]
        return raise "BITSET! did not match at parse position"
    ]

    === QUOTED! COMBINATOR ===

    ; When used with ANY-LIST? input, recognizes values literally.  When used
    ; with ANY-STRING? it will convert the value to a string before matching.
    ;
    ; 1. Not exactly sure what this operation is, but it's becoming more
    ;    relevant...it's like FORM by putting < > on tags (TO TEXT! won't) and
    ;    it will merge strings in blocks without spacing.

    quoted! combinator [
        return: "The matched value"
            [element?]
        /pending [blank! block!]
        value [quoted?]
        /negated
        <local> comb neq?
    ][
        if tail? input [
            return raise "QUOTED! cannot match at end of input"
        ]

        if any-list? input [
            neq?: either state.case [:strict-not-equal?] [:not-equal?]
            remainder: next input
            pending: _
            if neq? input.1 unquote value [
                if negated [
                    return input.1
                ]
                return raise [
                    "Value at parse position wasn't unquote of QUOTED! item"
                ]
            ]
            if negated [
                return raise "Negated QUOTED! parser matched item"
            ]
            return unquote value
        ]

        if negated [  ; !!! allow single-char or single-byte negations?
            fail "QUOTED! combinator can only be negated for List input"
        ]

        if any-string? input [
            let text: append copy "" unquote value  ; !!! name for this? [2]

            comb: (state.combinators).(text!)
            [@ remainder pending]: run comb state input text except e -> [
                return raise e
            ]
            return unquote value
        ]

        assert [binary? input]

        value: to binary! unquote value
        comb: (state.combinators).(binary!)
        return [@ remainder pending]: run comb state input value
    ]

    'lit combinator [  ; should long form be LITERALLY or LITERAL ?
        return: "Literal value" [element?]
        /pending [blank! block!]
        'value [element?]
        <local> comb
    ][
        ; Though generic quoting exists, being able to say [lit ''x] instead
        ; of ['''x] may be clarifying when trying to match ''x (for instance)

        comb: (state.combinators).(quoted!)
        return [@ remainder pending]: run comb state input ^value
    ]

    === QUASIFORM! COMBINATOR ===

    ; The combinator map can't hold antiforms as keys or values.  So for the
    ; behavior of things like splices or voids, that's done through their
    ; quasiforms, as it is assumed they would be equivalent:
    ;
    ;     >> parse [a b a b] [some ~(a b)~]
    ;     == ~(a b)~  ; anti
    ;
    ;     >> splice: ~(a b)~
    ;
    ;     >> parse [a b a b] [some splice]
    ;     == ~(a b)~  ; anti
    ;
    ; This is called e.g. by the @xxx combinators when they fetch an antiform.
    ; Fetching a quasiform has no meaning, and is an error...so acting like
    ; what you'd think a quasiform would do in source rules (by becoming an
    ; antiform) works.

    elide let quasi-return-spec: [return: [~void~ nihil? element? splice?]]

    quasiform! combinator compose [
        (spread quasi-return-spec)
        /pending [blank! block!]
        value [quasi?]
        <local> comb neq?
    ][
        if '~null~ = value [
            fail make error! [type: 'Script, id: 'need-non-null]
        ]

        pending: _
        remainder: input

        if '~void~ = value [
            return void  ; does not vanish
        ]

        if '~okay~ = value [
            return nihil  ; let okay just act as a "guard", no influence
        ]

        if '~[]~ = value [
            return nihil  ; act invisibly (same as ~okay~... make sense?)
        ]

        if tail? input [
            return raise "QUASIFORM! cannot match at end of input"
        ]

        if not splice? unmeta value [
            fail "Only antiform matched against list content is splice"
        ]

        if any-list? input [
            neq?: either state.case [:strict-not-equal?] [:not-equal?]
            for-each 'item unquasi value [
                if neq? remainder.1 item [
                    return raise [
                        "Value at input position didn't match splice element"
                    ]
                ]
                remainder: next remainder
            ]
            return unmeta value  ; matched splice is result
        ]

        fail "Splice combinators only match ANY-LIST? input"
    ]

    === BLANK! COMBINATOR ===

    ; Blanks match themselves literally in blocks.  This is particularly
    ; helpful for recognizing things like refinement:
    ;
    ;     >> refinement-rule: subparse path! [_ word!]
    ;
    ;     >> parse [/a] [refinement-rule]
    ;     == a
    ;
    ; In strings, they are matched as spaces, saving you the need to write out
    ; the full word "space" or use an ugly abbreviation like "sp"

    blank! combinator [
        return: "BLANK! if matched in list or space character in strings"
            [blank! char?]
        value [blank!]
    ][
        if tail? input [
            return raise "BLANK! cannot match at end of input"
        ]
        remainder: next input  ; if we consume an item, we'll consume just one
        if any-list? input [
            if blank? input.1 [
                return blank
            ]
            return raise "BLANK! rule found next input in list was not a blank"
        ]
        if any-string? input [
            if space = input.1 [
                return space
            ]
            return raise "BLANK! rule found next input in list was not space"
        ]
        assert [binary? input]
        if 32 = input.1 [  ; codepoint of space, crucially single byte for test
            return space  ; acts like if you'd written space in the rule
        ]
        return raise "BLANK! rule found next input in binary was not ASCII 32"
    ]

    === INTEGER! COMBINATOR ===

    ; The behavior of INTEGER! in UPARSE is to just evaluate to itself.  If you
    ; want to repeat a rule a certain number of times, you have to say REPEAT.
    ;
    ; A compatibility combinator for Rebol2 PARSE has the repeat semantics
    ; (though variadic combinators would be required to accomplish [2 4 rule]
    ; meaning "apply the rule between 2 and 4 times")
    ;
    ; Note that REPEAT allows the use of BLANK! to opt out of an iteration.

    integer! combinator [
        return: "Just the INTEGER! (see REPEAT for repeating rules)"
            [integer!]
        value [integer!]
    ][
        remainder: input
        return value
    ]

    'repeat combinator [
        return: "Last parser result"
            [any-value? pack?]
        times-parser [action?]
        parser [action?]
        <local> times' min max result'
    ][
        [^times' input]: times-parser input except e -> [return raise e]

        if times' = void' [  ; VOID-in-NULL-out
            remainder: input
            return null
        ]
        switch/type unmeta times' [
            blank! [  ; should blank be tolerated if void is?
                remainder: input
                return void  ; `[repeat (_) rule]` is a no-op
            ]
            issue! [
                if times' <> meta # [
                    fail ["REPEAT takes ISSUE! of # to act like TRY SOME"]
                ]
                min: 0, max: #
            ]
            integer! [
                max: min: unquote times'
            ]
            block! the-block! [
                parse unquote times' [
                    '_ '_ <end> (
                        remainder: input
                        return void  ; `[repeat ([_ _]) rule]` is a no-op
                    )
                    |
                    min: [integer! | '_ (0)]
                    max: [integer! | '_ (min) | '#]  ; blank means max = min
                ]
            ]
        ] else [
            fail "REPEAT combinator requires INTEGER! or [INTEGER! INTEGER!]"
        ]

        all [max <> #, max < min] then [
            fail "Can't make MAX less than MIN in range for REPEAT combinator"
        ]

        append state.loops binding of $return

        result': void'  ; `repeat (0) one` => void intent

        count-up 'i max [  ; will count infinitely if max is #
            ;
            ; After the minimum number of repeats are fulfilled, the parser
            ; may not match and return the last successful result.  So
            ; we don't do the assignment in the `[...]:` multi-return in case
            ; it would overwrite the last useful result.  Instead, the GROUP!
            ; potentially returns...only do the assignment if it does not.
            ;
            [^result' input]: parser input except [
                take/last state.loops
                if i <= min [  ; `<=` not `<` as this iteration failed!
                    return raise "REPEAT did not reach minimum repetitions"
                ]
                remainder: input
                return unmeta result'
            ]
        ]

        take/last state.loops
        remainder: input
        return unmeta result'
    ]

    === TYPE-XXX! COMBINATORS ===

    ; Traditionally you could only use a datatype with ANY-LIST? types,
    ; but since Ren-C uses UTF-8 Everywhere it makes it practical to merge in
    ; transcoding:
    ;
    ;     >> parse "{Neat!} 1020" [t: text! i: integer!]
    ;     == "{Neat!} 1020"
    ;
    ;     >> t
    ;     == "Neat!"
    ;
    ;     >> i
    ;     == 1020
    ;

    type-block! combinator [
        return: "Matched or synthesized value"
            [element?]
        value [type-block!]
        /negated
        <local> item error
    ][
        if tail? input [
            return raise "TYPE-BLOCK! cannot match at end of input"
        ]
        either any-list? input [
            if value <> type of input.1 [
                if negated [
                    remainder: next input
                    return input.1
                ]
                return raise "Value at parse position did not match TYPE-BLOCK!"
            ]
            if negated [
                return raise "Value at parse position matched TYPE-BLOCK!"
            ]
            remainder: next input
            return input.1
        ][
            if negated [
                fail "TYPE-BLOCK! only supported negated for array input"
            ]
            [remainder item]: transcode/next input except e -> [return raise e]

            ; If TRANSCODE knew what kind of item we were looking for, it could
            ; shortcut this.  Since it doesn't, we might waste time and memory
            ; doing something like transcoding a very long block, only to find
            ; afterward it's not something like a requested integer!.  Red
            ; has some type sniffing in their fast lexer, review relevance.
            ;
            if value != type of item [
                return raise "Could not TRANSCODE the datatype from input"
            ]
            return item
        ]
    ]

    type-group! combinator [
        return: "Matched or synthesized value"
            [element?]
        value [type-group!]
        <local> item error
    ][
        if tail? input [
            return raise "TYPE-GROUP! cannot match at end of input"
        ]
        either any-list? input [
            if not match value input.1 [
                return raise "Value at parse position did not match TYPE-GROUP!"
            ]
            remainder: next input
            return input.1
        ][
            [remainder item]: transcode/next input except e -> [return raise e]

            ; If TRANSCODE knew what kind of item we were looking for, it could
            ; shortcut this.  Since it doesn't, we might waste time and memory
            ; doing something like transcoding a very long block, only to find
            ; afterward it's not something like a requested integer!.  Red
            ; has some type sniffing in their fast lexer, review relevance.
            ;
            match value item else [
                return raise "Could not TRANSCODE the TYPE-GROUP! from input"
            ]
            return item
        ]
    ]

    type-word! combinator [
        return: "Matched or synthesized value"
            [element?]
        value [type-word!]
        /negated
        <local> item error
    ][
        if tail? input [
            return raise "TYPE-WORD! cannot match at end of input"
        ]
        either any-list? input [
            if not match value input.1 [
                if negated [
                    remainder: next input
                    return input.1
                ]
                return raise "Value at parse position did not match TYPE-WORD!"
            ]
            if negated [
                return raise "Value at parse position matched TYPE-WORD!"
            ]
            remainder: next input
            return input.1
        ][
            if negated [
                fail "TYPE-WORD! only supported negated for array input"
            ]
            [remainder item]: transcode/next input except e -> [return raise e]

            ; If TRANSCODE knew what kind of item we were looking for, it could
            ; shortcut this.  Since it doesn't, we might waste time and memory
            ; doing something like transcoding a very long block, only to find
            ; afterward it's not something like a requested integer!.  Red
            ; has some type sniffing in their fast lexer, review relevance.
            ;
            match value item else [
                return raise "Could not TRANSCODE the TYPE-WORD! from input"
            ]
            return item
        ]
    ]

    === THE COMBINATOR ===

    '@ combinator [  ; see [1] in THE-XXX! combinators section
        return: "Quoted form of literal value (not matched)" [element?]
        'value [element?]
    ][
        remainder: input
        return :value
    ]

    'the combinator [
        return: "Quoted form of literal value (not matched)" [element?]
        'value [element?]
    ][
        remainder: input
        return :value
    ]

    === THE-XXX! COMBINATORS ===

    ; What the THE-XXX! combinators are for is doing literal matches vs. a
    ; match through a rule, e.g.:
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
    ;    that also means [@ (...)] is a synonym for [@(...)].  It's better to
    ;    use it as a shorthand for generating literal unmatched material.
    ;
    ; 2. Evaluatively, if we get a quasiform as the meta it means it was an
    ;    antiform.  Semantically that's matched by the quasiform combinator,
    ;    which treats source-level quasiforms as if they evaluated to their
    ;    antiforms, e.g. (parse [a b a b] [some ~(a b)~]) matches the splice.
    ;
    ; 3. THE-PATH! might be argued to have the meaning of "run this function
    ;    on the current item and if the result returns okay, then consider it
    ;    a match".  But that is done more cleanly with type constraints e.g.
    ;    `&even?`, which is better than `@/even?` or `@even?/`
    ;
    ; 4. THE-BLOCK! acting as just matching the block would be redundant with
    ;    quoted block.  So the most "logical" choice is to make it mean
    ;    "literally match the result of running the block as a rule".  This
    ;    makes it redundant with [1], but choosing another purpose for @ would
    ;    likely be able to be higher leverage.

    ; @ combinator is used for a different purpose [1]

    the-word! combinator compose [
        (spread quasi-return-spec)
        /pending [blank! block!]
        value [the-word!]
        <local> comb lookup'
    ][
        lookup': meta get value
        ensure [quoted? quasi?] lookup'  ; quasi means antiform [2]
        comb: (state.combinators).(type of lookup')
        return [@ remainder pending]: run comb state input lookup'
    ]

    ; THE-PATH! has no meaning at this time [3]

    the-tuple! combinator compose [
        (spread quasi-return-spec)
        /pending [blank! block!]
        value [the-tuple!]
        <local> comb lookup'
    ][
        lookup': meta get value
        ensure [quoted? quasi?] lookup'  ; quasi means antiform [2]
        comb: (state.combinators).(type of lookup')
        return [@ remainder pending]: run comb state input lookup'
    ]

    the-group! combinator compose [
        (spread quasi-return-spec)
        /pending [blank! block!]
        value [the-group!]
        <local> result' comb subpending single
    ][
        value: as group! value  ; remove @ decoration
        comb: runs (state.combinators).(group!)
        [^result' remainder pending]: comb state input value except e -> [
            return raise e
        ]

        ensure [quoted? quasi?] result'  ; quasi means antiform [2]
        comb: runs state.combinators.(type of result')
        [^result' remainder subpending]: comb state remainder result'
            except e -> [
                return raise e
            ]

        pending: glom pending spread subpending
        return unmeta result'
    ]

    the-block! combinator compose [  ; matching literal block is redundant [4]
        (spread quasi-return-spec)
        /pending [blank! block!]
        value [the-block!]
        <local> result' comb subpending
    ][
        value: as block! value  ; remove @ decoration
        comb: runs (state.combinators).(block!)
        [^result' remainder pending]: comb state input value except e -> [
            return raise e
        ]

        ensure [quoted? quasi?] result' ; quasi means antiform [2]
        comb: runs (state.combinators).(type of result')
        [^result' remainder subpending]: comb state remainder result'
            except e -> [
                return raise e
            ]

        pending: glom pending spread subpending
        return unmeta result'
    ]

    === META-XXX! COMBINATORS ===

    ; The META-XXX! combinators add a quoting level to their result, unless
    ; it's antiform in which case it becomes a qusaiform.  The quoting is
    ; important with functions like KEEP...but advanced tunneling of behavior
    ; regarding unsets, nulls, and invisibility requires the feature.
    ;
    ; Note: These are NOT fixed as `[:block-combinator | :meta]`, because
    ; they want to inherit whatever combinator that is currently in use for
    ; their un-meta'd type (by default).  This means dynamically reacting to
    ; the set of combinators chosen for the particular parse.
    ;
    ; !!! These follow a simple pattern, could all use the same combinator and
    ; just be sensitive to the received kind of value.

    '^ combinator [
        return: "Meta quoted" [~null~ quasi? quoted?]
        parser [action?]
    ][
        return [^ remainder]: parser input
    ]

    meta-word! combinator [
        return: "Meta quoted" [~null~ quasi? quoted?]
        /pending [blank! block!]
        value [meta-word!]
        <local> comb
    ][
        value: as word! value
        comb: runs state.combinators.(word!)
        return [^ remainder pending]: comb state input value  ; leave meta
    ]

    meta-tuple! combinator [
        return: "Meta quoted" [~null~ quasi? quoted?]
        /pending [blank! block!]
        value [meta-tuple!]
        <local> comb
    ][
        value: as tuple! value
        comb: runs state.combinators.(tuple!)
        return [^ remainder pending]: comb state input value  ; leave meta
    ]

    meta-path! combinator [
        return: "Meta quoted" [~null~ quasi? quoted?]
        /pending [blank! block!]
        value [meta-path!]
        <local> comb
    ][
        value: as path! value
        comb: runs state.combinators.(path!)
        return [^ remainder pending]: comb state input value  ; leave meta
    ]

    meta-group! combinator [
        return: "Meta quoted" [~null~ quasi? quoted?]
        /pending [blank! block!]
        value [meta-group!]
        <local> comb
    ][
        value: as group! value
        comb: runs state.combinators.(group!)
        return [^ remainder pending]: comb state input value  ; leave meta
    ]

    meta-block! combinator [
        return: "Meta quoted" [~null~ quasi? quoted?]
        /pending [blank! block!]
        value [meta-block!]
        <local> comb
    ][
        value: as block! value
        comb: runs state.combinators.(block!)
        return [^ remainder pending]: comb state input value  ; leave meta
    ]

    === INVISIBLE COMBINATORS ===

    ; If BLOCK! is asked for a result, it will accumulate results from any
    ; result-bearing rules it hits as it goes.  Not all rules give results
    ; by default--such as GROUP! or literals for instance.  If something
    ; gives a result and you do not want it to, use ELIDE.

    'elide combinator [
        {Transform a result-bearing combinator into one that has no result}
        return: "Invisible"
            [nihil?]
        parser [action?]
    ][
        [^ remainder]: parser input except e -> [return raise e]
        return nihil
    ]

    'comment combinator [
        {Comment out an arbitrary amount of PARSE material}
        return: "Invisible"
            [nihil?]
        'ignored [block! text! tag! issue!]
    ][
        ; !!! This presents a dilemma, should it be quoting out a rule, or
        ; quoting out material that's quoted?  This *could* make a parser and
        ; simply not call it:
        ;
        ;    >> parse "aaa" [3 "a" comment across some "b"]
        ;    == "a"
        ;
        ; In any case, forming a parser rule that's not going to be run is
        ; less efficient than just quoting material, which can be done on
        ; rules with illegal content:
        ;
        ;    >> parse "a" [comment [across some "a" ~illegal~] "a"]
        ;    == "a"
        ;
        ; The more efficient and flexible form is presumed here to be the
        ; most useful, and the closest parallel to the plain COMMENT action.
        ;
        remainder: input
        return nihil
    ]

    'skip combinator [
        {Skip an integral number of items}
        return: "Invisible" [nihil?]
        parser [action?]
        <local> result
    ][
        [result _]: parser input except e -> [return raise e]

        if blank? :result [
            remainder: input
            return nihil
        ]
        if not integer? :result [
            fail "SKIP expects INTEGER! amount to skip"
        ]
        remainder: skip input result else [
            return raise "Attempt to SKIP past end of parse input"
        ]
        return nihil
    ]

    'one combinator [  ; historically used "SKIP" for this
        "Match one series item in input, succeeding so long as it's not at END"
        return: "One element of series input"
            [element?]
    ][
        if tail? input [
            return raise "PARSE position at tail, ONE has no item to match"
        ]
        remainder: next input
        return input.1
    ]

    <next> combinator [  ; historically used "SKIP" for this, also
        "Give next position in input, succeeding so long as it's not at END"
        return: "One element of series input"
            [element?]
    ][
        if tail? input [
            return raise "PARSE position at tail, <NEXT> can't advance position"
        ]
        remainder: next input
        return remainder
    ]

    === FRAME! COMBINATOR ===

    ; The frame combinator is a new idea of letting you call a normal
    ; function with parsers fulfilling the arguments.  At the Montreal
    ; conference Carl said he was skeptical of PARSE getting any harder to read
    ; than it was already, so the isolation of DO code to GROUP!s seemed like
    ; it was a good idea, but maybe allowing calling zero-arity functions.
    ;
    ; With Ren-C it's easier to try these ideas out.  So the concept is that
    ; you can make a PATH! that ends in / and that will be run as a normal
    ; ACTION!, but whose arguments are fulfilled via PARSE.

    frame! combinator [
        {Run an ordinary action with parse rule products as its arguments}
        return: "The return value of the action"
            [any-value? pack?]
        /pending [blank! block!]
        value [frame!]
        ; AUGMENT is used to add param1, param2, param3, etc.
        /parsers "Sneaky argument of parsers collected from arguments"
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

        ; !!! We cannot use the autopipe mechanism because the hooked combinator
        ; does not see the augmented frame.  Have to do it manually.
        ;
        pending: _

        let f: make frame! value
        for-each 'param (parameters of f) [
            if not path? param [
                ensure frame! parsers.1
                if meta-word? param [
                    param: to word! param
                    f.(param): [^ input subpending]: (
                        run parsers.1 input
                    ) except e -> [
                        return raise e
                    ]
                ] else [
                    f.(param): [@ input subpending]: (
                        run parsers.1 input
                    ) except e -> [
                        return raise e
                    ]
                ]
                pending: glom pending spread subpending
                parsers: next parsers
            ]
        ]
        assert [tail? parsers]
        remainder: input
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
            [any-value? pack?]
        /pending [blank! block!]
        value [word! tuple!]
        <local> r comb rule-start rule-end
    ][
        rule-start: null
        rule-end: null

        switch/type r: get value [
            ;
            ; BLOCK!s are accepted as rules, and looked up via combinator.
            ; Most common case, make them first to shortcut earlier.
            ;
            block! [
                rule-start: r
                rule-end: tail of r
            ]

            ; These types are accepted literally (though they do run through
            ; the combinator looked up to, which ultimately may not mean
            ; that they are literal... should there be a special "literal"
            ; mapped-to-value so that if you rephrase them to active
            ; combinators the word lookup won't work?)
            ;
            text! []
            binary! []
            issue! []

            ; Datatypes looked up by words (e.g. TAG!) are legal as rules
            ;
            type-word! []
            type-tuple! []
            type-path! []
            type-block! []
            type-group! []

            ; Bitsets were also legal as rules without decoration
            ;
            bitset! []

            ; Blanks were legal as inert...they may become the actual canon
            ; representation of the space CHAR!.
            ;
            blank! []

            &void? [
                remainder: input
                pending: _  ; not delegating to combinator with pending
                return void  ; yield void
            ]

            &okay? [
                fail "WORD! fetches cannot be ~okay~ in UPARSE (see WHEN)"
            ]

            &null? [
                fail "WORD! fetches cannot be ~null~ in UPARSE (see WHEN)"
            ]

            fail [
                "WORD! can't look up to active combinator, unless BLOCK!."
                "If literal match is meant, use" unspaced ["@" value]
            ]
        ]

        if not comb: select state.combinators type of r [
            fail ["Unhandled type in WORD! combinator:" mold type of r]
        ]

        ; !!! We don't need to call COMBINATORIZE because we can't handle
        ; arguments here, but this should have better errors if the datatype
        ; combinator takes arguments.
        ;
        ; !!! REVIEW: handle `rule-start` and `rule-end` ?
        ;
        return [@ remainder pending]: run comb state input :r
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
            [any-value? pack?]
        /pending [blank! block!]
        @arg "To catch instances of old ANY, only GROUP! and THE-BLOCK!"
            [element?]  ; lie and take any element to report better error
        <local> result' block
    ][
        switch/type :arg [
            group! [
                if not block? block: eval arg [
                    fail ["The ANY combinator requires a BLOCK! of alternates"]
                ]
            ]
            block! [block: arg]
        ] else [
            fail [
                "The ANY combinator in UPARSE is not an iterating construct."
                "Use TRY SOME, TRY SOME FURTHER, etc. depending on purpose:"
                https://forum.rebol.info/t/1572
            ]
        ]

        while [not tail? block] [
            ;
            ; Turn next alternative into a parser ACTION!, and run it.
            ; We take the first parser that succeeds.
            ;
            let [parser 'block]: parsify state block
            [^result' remainder pending]: parser input except [continue]
            return unmeta result'
        ]

        return raise "All of the parsers in ANY block failed"
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

    block! (block-combinator: combinator [
        return: "Last result value"
            [any-value? pack?]
        /pending [blank! block!]
        value [block!]
        /limit "Limit of how far to consider (used by ... recursion)"
            [block!]
        /thru "Keep trying rule until end of block"
        <local> rules pos result' f sublimit subpending temp old-env
    ][
        rules: value  ; alias for clarity
        limit: default [tail of rules]
        pos: input

        pending: _  ; can become GLOM'd into a BLOCK!

        result': nihil'  ; default result is invisible

        old-env: state.env
        return: adapt get $return [state.env: old-env]
        state.env: rules  ; currently using blocks as surrogate for environment

        while [not same? rules limit] [
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
                remainder: pos
                return unmeta result'
            ]

            ; If you hit an inline sequencing operator here then it's the last
            ; alternate in a list.
            ;
            if rules.1 = '|| [
                input: pos  ; don't roll back past current pos
                rules: my next
                continue
            ]

            ; Do one "Parse Step".  This involves turning whatever is at the
            ; next parse position into an ACTION!, then running it.
            ;
            if rules.1 = '... [  ; "variadic" parser, use recursion
                rules: next rules
                if tail? rules [  ; if at end, act like [elide to <end>]
                    remainder: tail of pos
                    return unmeta result'
                ]
                sublimit: find/part rules [...] limit

                ; !!! Once this used ACTION OF BINDING OF $RETURN,
                ; the fusion of actions and frames removed ACTION OF.  So the
                ; action is accessed by an assigned name from outside.  On
                ; the plus side, that allowed RULE-START and RULE-END to be
                ; exposed, which were not visible to the inner function that
                ; ACTION OF BINDING OF $RETURN received.
                ;
                f: make frame! :block-combinator  ; this combinator
                f.state: state
                f.value: rules
                f.limit: sublimit
                f.rule-start: rules
                f.rule-end: sublimit else [tail of rules]

                f.thru: ok

                rules: sublimit else [tail of rules]
            ] else [
                f: make frame! unrun [@ rules]: parsify state rules
            ]

            f.input: pos

            if not error? temp: entrap f [
                [^temp pos subpending]: unmeta temp
                if unset? $pos [
                    print mold/limit rules 200
                    fail "Combinator did not set remainder"
                ]
                if unset? $subpending [
                    print mold/limit rules 200
                    fail "Combinator did not set pending"
                ]
                if temp <> nihil' [
                    result': temp  ; overwrite if was visible
                ]
                pending: glom pending spread subpending
            ] else [
                result': nihil'  ; reset, e.g. `[bypass |]`

                free pending  ; proactively release memory
                pending: _

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
                        remainder: null
                        return raise* make error! [
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

        remainder: pos
        return unmeta result'
    ])

    === FAIL COMBINATOR ===

    ; Gracefully handling failure has two places that you might want to
    ; provide assistance in implicating...one is the parse rules, and the other
    ; is the parse input.
    ;
    ; The FAIL combinator is--perhaps obviously (?)--not for pointing out
    ; syntax errors in the rules.  Because it's a rule.  So by default it will
    ; complain about the location where you are in the input.
    ;
    ; It lets you take an @[...] block, because a plain [...] block would be
    ; processed as a rule.  For the moment it quotes it for convenience.

    'fail combinator [
        return: []
        'reason [the-block!]
        <local> e
    ][
        e: make error! [
            type: 'User
            id: 'parse
            message: to text! reason
        ]
        set-location-of-error e binding of $reason
        e.near: mold/limit input 80
        fail e
    ]
]


=== MAKE TUPLE! COMBINATOR SAME AS WORD! COMBINATOR ===

; There's no easy way to do this in a MAP!, like `word!: tuple!: ...` would
; work for OBJECT!.

default-combinators.(tuple!): default-combinators.(word!)


=== ABBREVIATIONS ===

default-combinators.opt: default-combinators.optional
default-combinators.try: default-combinators.optional  ; deprecated


=== COMPATIBILITY FOR NON-TAG KEYWORD FORMS ===

; !!! This has been deprecated.  But it's currently possible to get this
; just by saying `end: <end>` and `here: <here>`.

comment [
    default-combinators.('here): default-combinators.<here>
    default-combinators.('end): default-combinators.<end>
]


comment [combinatorize: func [

    {Analyze combinator parameters in rules to produce a specialized "parser"}

    return: "Parser function taking only input, and advanced rules position"
        [~[action? block!]~]
    combinator "Parser combinator taking input, but also other parameters"
        [frame!]
    rules [block!]
    state "Parse State" [frame!]
    /value "Initiating value (if datatype)" [element?]
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

    for-each 'param parameters of :combinator [
        case [
            param = 'input [
                ; All combinators should have an input.  But the
                ; idea is that we leave this unspecialized.
            ]
            param = '/remainder [
                ; The remainder is a return; responsibility of the caller, also
                ; left unspecialized.
            ]
            param = '/pending [
                ; same for pending, this is the data being gathered that may
                ; need to be discarded (gives impression of "rollback") and
                ; is the feature behind COLLECT etc.
            ]
            param = 'value [
                f.value: value
            ]
            param = 'state [  ; the "state" is currently the UPARSE frame
                f.state: state
            ]
            param = '/rule-start [
                f.rule-start: rule-start
            ]
            param = '/rule-end [
                ; skip (filled in at end)
            ]
            quoted? param [  ; literal element captured from rules
                param: unquote param
                r: rules.1
                any [
                    not r
                    find [, | ||] r
                ]
                then [
                    if not endable? in f param [
                        fail "Too few parameters for combinator"
                    ]
                    f.(param): null
                ]
                else [
                    f.(param): r
                    rules: next rules
                ]
            ]
            refinement? param [
                ; Leave refinements alone, e.g. /only ... a general strategy
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
                    if not endable? in f param [
                        fail "Too few parameters for combinator"
                    ]
                    f.(param): null
                ]
                else [
                    f.(param): [@ rules]: parsify state rules
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
;  * If it doesn't match, it will return a raised error
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
;    ends in a slash, it is considered an invocation of a normal function with
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
;    this kind of dispatch is not variadic--and hence no arguments are taken.
;    (That's fine for BLOCK!, TAG!, TEXT! etc. in the general case, though
;    this limitation may be overcome someday.)
;
parsify: func [
    "Transform one step's worth of rules into a parser combinator action"

    return: "Parser action for a full rule, advanced rules position"
        [~[action? block!]~]
    state "Parse state"
        [frame!]
    rules "Parse rules to (partially) convert to a combinator action"
        [block!]
    <local> r comb value
][
    r: rules.1

    if comma? r [  ; block combinator consumes the legal commas [1]
        fail "COMMA! can only be run between PARSE steps, not during them"
    ]
    rules: my next

    while [comb: try state.combinators.(r)] [  ; literal match first [2]
        if match frame! comb [
            return combinatorize comb rules state
        ]

        r: comb  ; didn't look up to combinator, just element to substitute [3]
    ]

    r: inside state.env r  ; add binding if applicable

    ;--- SPECIAL-CASE VARIADIC SCENARIOS BEFORE DISPATCHING BY DATATYPE

    case [
        word? r [  ; non-"keyword" WORD! (didn't look up literally)
            if null? value: get r [
                fail make error! [type: 'Script, id: 'need-non-null]
            ]

            if comb: match frame! :value [  ; variable held a combinator [4]
                if combinator? :comb [
                    return combinatorize :comb rules state
                ]

                let name: uppercase to text! r
                fail [
                    name "is not a COMBINATOR ACTION!"
                    "For non-combinator actions in PARSE, use"
                        unspaced [name "/"]
                ]
            ]

            ; fall through to datatype-based WORD! combinator handling
        ]

        (path? r) and (blank? last r) [  ; "action combinator" [5]
            if not frame? let gotten: unrun get/any r [
                fail "In UPARSE PATH ending in / must be action or frame"
            ]
            if not comb: select state.combinators frame! [
                fail "No frame! combinator, can't use PATH ending in /"
            ]

            comb: unrun adapt (augment comb inside [] collect [
                let n: 1
                for-each 'param parameters of gotten [
                    if not path? param [
                        keep spread compose [
                            (to word! unspaced ["param" n]) [action?]
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
                for-each 'param (parameters of value) [
                    if not path? param [
                        append parsers unrun :f.(as word! unspaced ["param" n])
                        n: n + 1
                    ]
                ]
            ]

            return combinatorize/value comb rules state gotten
        ]

        ; !!! Here is where we would let GET-TUPLE! and GET-WORD! be used to
        ; subvert keywords if SEEK were universally adopted.
    ]

    if not comb: try state.combinators.(type of r) [  ; datatype dispatch [6]
        fail ["Unhandled type in PARSIFY:" mold type of r "-" mold r]
    ]

    return combinatorize/value comb rules state r
]


=== ENTRY POINTS: PARSE*, PARSE ===

; The historical Redbol PARSE was focused on returning a LOGIC! so that you
; could write `if parse data rules [...]` and easily react to finding out if
; the rules matched the input to completion.
;
; But UPARSE goes deeper by letting rules evaluate to arbitrary results, and
; bubbles out the final result.  All values are in-band, so NULL can't be
; drawn out as signals of a lack of completion...definitional errors, and
; hence EXCEPT or TRAP must be used:
;
;     result: parse data rules except [...]
;
; But there's a variant called PARSE/MATCH which returns the input matched,
; or it returns null:
;
;     >> parse/match "aaa" [some #a]
;     == "aaa"
;
;     >> validate "aaa" [some #b]
;     == ~null~  ; anti
;
; So for those who prefer to use IF to know about a parse's success or failure
; they can just use PARSE/MATCH instead.
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
        [~[[any-value? pack?] [blank! block!]]~ raised?]
    input "Input data"
        [<maybe> any-series? url! any-sequence?]
    rules "Block of parse rules"
        [block!]
    /combinators "List of keyword and datatype handlers used for this parse"
        [map!]
    /case "Do case-sensitive matching"
    /relax "Don't require reaching the tail of the input for success"
    /part "FAKE /PART FEATURE - runs on a copy of the series!"
        [integer! any-series?]
    /hook "Call a hook on dispatch of each combinator"
        [<unrun> frame!]

    <local> loops furthest synthesized' remainder pending env
][
    ; PATH!s, TUPLE!s, and URL!s are read only and don't have indices.  But we
    ; want to be able to parse them, so make them read-only series aliases:
    ;
    ; https://forum.rebol.info/t/1276/16
    ;
    lib/case [  ; !!! Careful... /CASE is a refinement to this function
        any-sequence? input [input: as block! input]
        url? input [input: as text! input]
    ]

    loops: copy []  ; need new loop copy each invocation

    ; We put an implicit PHASE bracketing the whole of UPARSE* so that the
    ; <delay> groups will be executed.
    ;
    rules: inside rules bindable reduce ['phase bindable rules]

    ; There's no first-class ENVIRONMENT! type, so lists are used as a
    ; surrogate for exposing their specifiers.  When the environment is
    ; expanded (e.g. by LET) the rules block in effect is changed to a block
    ; with the same contents (array+position) but a different specifier.
    ;
    env: rules

    ; !!! Red has a /PART feature and so in order to run the tests pertaining
    ; to that we go ahead and fake it.  Actually implementing /PART would be
    ; quite a tax on the combinators...so thinking about a system-wide slice
    ; capability would be more sensible.  The series will not have the same
    ; identity, so mutating operations will mutate the wrong series...we
    ; could copy back, but that just shows what a slippery slope this is.
    ;
    if part [
        input: copy/part input part
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
    ; combinators, but also the /VERBOSE or other settings...we can add more.
    ;
    let state: binding of $return

    let f: make frame! combinators.(block!)
    f.state: state
    f.input: input
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

    sys.util/rescue/relax [  ; /RELAX allows RETURN from block
        [^synthesized' remainder pending]: eval/undecayed f except e -> [
            assert [empty? state.loops]
            pending: _  ; didn't get assigned due to error
            return raise e  ; wrappers catch
        ]
    ] then e -> [
        print "!!! HARD FAIL DURING PARSE !!!"
        print mold/limit state.rules 200
        fail e
    ]

    assert [empty? state.loops]

    if (not tail? remainder) and (not relax) [
        return raise make error! [
            id: 'parse-incomplete
            message:
              "PARSE partially matched the input, but didn't reach the tail"
        ]
    ]

    ; While combinators can vaporize, don't allow PARSE itself to vaporize
    ;
    if synthesized' = nihil' [
        synthesized': void'
    ]

    return pack [(unmeta synthesized') pending]
]

parse: (comment [redescribe [  ; redescribe not working at the moment (?)
    {Process input in the parse dialect, definitional error on failure}
] ]
    enclose get $parse* func [f] [
        let [^synthesized' pending]: eval/undecayed f except e -> [
            return raise e
        ]
        if not empty? pending [
            fail "PARSE completed, but pending list was not empty"
        ]
        return unmeta synthesized'
    ]
)

parse-: (comment [redescribe [  ; redescribe not working at the moment (?)
    {Process input in the parse dialect, return how far reached}
] ]
    enclose get $parse* func [f] [
        f.rules: compose [(f.rules) || accept <here>]

        let [^synthesized' pending]: eval/undecayed f except [
            return null
        ]
        if not empty? pending [
            fail "PARSE completed, but pending list was not empty"
        ]
        return heavy unmeta synthesized'
    ]
)


sys.util.parse: runs get $parse  ; !!! expose UPARSE to SYS.UTIL module, hack...


=== HOOKS ===

; These are some very primordial hooks; for an elaborate demo see EPARSE's
; rule-stepwise debugger.

parse-trace-hook: func [
    return: [pack?]
    f [frame!]
][
    let state: f.state

    if f.rule-start [
        print ["RULE:" mold spread copy/part f.rule-start f.rule-end]
    ]

    let result': ^ eval/undecayed f except e -> [
        print ["RESULT': FAIL"]
        return raise e
    ]

    print ["RESULT':" (mold result' else ["NULL"])]

    return unmeta result'
]

parse-trace: specialize get $parse [hook: :parse-trace-hook]


parse-furthest-hook: func [
    return: [pack?]
    f [frame!]
    var [word! tuple!]
][
    let result': ^ eval/undecayed f except e -> [
        return raise e
    ]

    let remainder: unquote second unquasi result'
    if (index? remainder) > (index? get var) [
        set var remainder
    ]

    return unmeta result'
]

parse-furthest: adapt augment :parse [
    var "Variable to hold furthest position reached"
        [word! tuple!]
][
    hook: specialize get $parse-furthest-hook compose [var: '(var)]
    set var input
]


=== "USING" FEATURE ===

; !!! This operation will likely take over the name USE.  It is put here since
; the UPARSE tests involve it.
;
using: func [
    return: [~]  ; should it return a value?  (e.g. the object?)
    obj [<maybe> object!]
][
    add-use-object (binding of $obj) obj
]
