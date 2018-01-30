REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Infix operator symbol definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        In R3-Alpha, an "OP!" function would gather its left argument greedily
        without waiting for further evaluation, and its right argument would
        stop processing if it hit another "OP!".  This meant that a sequence
        of all infix ops would appear to process left-to-right, e.g.
        `1 + 2 * 3` would be 9.

        Ren-C does not have an "OP!" function type, it just has FUNCTION!, but
        a WORD! can be SET with the /ENFIX refinement.  This indicates that
        when the function is dispatched through that word, it should get its
        first parameter from the left.  However it will obey the parameter
        conventions of the original function (including quoting).  Hence since
        ADD has normal parameter conventions, `+: enfix :add` would wind up
        with `1 + 2 * 3` as 7.

        So a new parameter convention indicated by ISSUE! is provided to get
        the "#tight" behavior of OP! arguments in R3-Alpha.
    }
]

; R3-Alpha has several forms illegal for SET-WORD! (e.g. `<:`)  Ren-C allows
; more of these things, but if they were top-level SET-WORD! in this file then
; R3-Alpha wouldn't be able to read it when used as bootstrap r3-make.  It
; also can't LOAD several WORD! forms that Ren-C can (e.g. `->`)
;
; So %b-init.c manually adds the keys via Add_Lib_Keys_R3Alpha_Cant_Make().
; R3-ALPHA-QUOTE annotates to warn not to try and assign SET-WORD! forms, and
; to bind interned strings.
;
r3-alpha-quote: func [:spelling [word! string!]] [
    either word? spelling [
        spelling
    ][
        bind (to word! spelling) (context of 'r3-alpha-quote)
    ]
]


; Make top-level words (note: / is added by %b-init.c as an "odd word")
;
+: -: *: _

for-each [math-op function-name] [
    +       add
    -       subtract
    *       multiply
    /       divide ;-- !!! may become pathing operator (which also divides)
][
    ; Ren-C's infix math obeys the "tight" parameter convention of R3-Alpha.
    ; But since the prefix functions themselves have normal parameters, this
    ; would require a wrapping function...adding a level of inefficiency:
    ;
    ;     +: enfix func [#a #b] [add :a :b]
    ;
    ; TIGHTEN optimizes this by making a "re-skinned" version of the function
    ; with tight parameters, without adding extra overhead when called.  This
    ; mechanism will eventually generalized to do any rewriting of convention
    ; one wants (e.g. to switch one parameter from normal to quoted).
    ;
    set/enfix math-op (tighten get function-name)
]


; Make top-level words
;
and+: or+: xor+: _

for-each [set-op function-name] [
    and+    intersect
    or+     union
    xor+    difference
][
    ; The enfixed versions of the set operations currently can't take any
    ; refinements, so we go ahead and specialize them out.  (It's also the
    ; case that TIGHTEN currently changes all normal parameters to #tight,
    ; which creates an awkward looking /SKIP's SIZE.
    ;
    set/enfix set-op (tighten specialize function-name [
        case: false
        skip: false
    ])
]


; Make top-level words for things not added by %b-init.c
;
=: !=: ==: !==: =?: _

for-each [comparison-op function-name] [
    =       equal?
    <>      not-equal?
    <       lesser?
    <=      lesser-or-equal? ;-- !!! or left arrow?  Consider `=<`
    >       greater?
    >=      greater-or-equal?

    !=      not-equal? ;-- !!! http://www.rebol.net/r3blogs/0017.html

    ==      strict-equal?
    !==     strict-not-equal?

    =?      same?
][
    ; !!! See discussion about the future of comparison operators:
    ; https://forum.rebol.info/t/349
    ;
    ; While they were "tight" in R3-Alpha, Ren-C makes them use normal
    ; parameters.  So you can write `if length of block = 10 + 20 [...]` and
    ; other expressive things.  It comes at the cost of making it so that
    ; `if not x = y [...]` is interpreted as `if (not x) = y [...]`, which
    ; all things considered is still pretty natural (and popular in many
    ; languages)...and a small price to pay.  Hence no TIGHTEN call here.
    ;
    set/enfix comparison-op (get function-name)
]


; !!! Originally in Rebol2 and R3-Alpha, ? was a synonym for HELP.  This seems
; wasteful for the language as a whole, when it's easy enough to type HELP,
; or add it to the console-specific abbreviations as H (as with Q for QUIT).
;
; This experiments with making `? var` equivalent to `set? 'var`.  Some are
; made uncomfortable by ? being prefix and not infix, but this is a very
; useful feature to have a shorthand for.  (Note: might `! var` being a
; shorthand for `not set? 'var` make more sense than meaning NOT, because
; there the tradeoff of literacy for symbology actually makes something a
; bit clearer instead of less clear?)
;
?: func [
    {Determine whether a word represents a variable that is SET?}

    'var [any-word! any-path!]
        {Variable name to test}
][
    ; Note: since this just changes the parameter convention, it could use a
    ; facade (the way TIGHTEN does) and run the native code for SET?.  Revisit
    ; when REDESCRIBE has this ability.
    ;
    set? var
]


; !!! Originally in Rebol2 and R3-Alpha, ?? was used to dump variables.  In
; the spirit of not wanting to take ? for something like HELP, that function
; has been defined as DUMP (and extended with significant new features).  The
; console makes D at the start of input a shortcut for this.
;
; Instead, ?? is used to make an infix operator, that takes a condition on the
; left and a value on the right--like a THEN that won't run blocks/functions.
; As a complement, !! is then taken as a parallel to ELSE, which will also not
; run blocks or functions.  This is a similar to these operators from Perl6:
;
; https://docs.perl6.org/language/operators#infix_??_!!
;
; However, note that if you say `1 < 2 ?? 3 + 3 !! 4 + 4`, both additions
; will be run.  To "block" evaluation, there has to be a BLOCK! somewhere,
; hence these are not meant as a generic substitute for IF and ELSE.
;
??: enfix func [
    {If left is conditionally true, return value on the right (as-is)}

    return: [<opt> any-value!]
        {Void if the condition is FALSEY?, else value}
    condition [any-value!]
    value [<opt> any-value!]
][
    if/only :condition [:value]
]

!!: enfix func [
    {If left isn't void, return it, else return value on the right (as-is)}

    return: [<opt> any-value!]
        {Left if it isn't void, else right}
    left [<opt> any-value!]
    right [<opt> any-value!]
][
    either-test-value/only :left [:right]
]

?!: enfix func [
    {If left is conditionally false, return value on the right (as-is)}

    return: [<opt> any-value!]
        {Void if the condition is FALSEY?, else value}
    condition [any-value!]
    value [<opt> any-value!]
][
    unless/only :condition [:value]
]


; THEN is an enfixed form of IF, which gives a branch running parallel for ??.
; Since it has a longer name it may not seem useful--but it can occasionally
; be useful in balancing the look of an expression, e.g. compare:
;
;    if (some long) and (complicated expression) [a + b] else [c + d]
;
;    (some long) and (complicated expression) then [a + b] else [c + d]
;

then: enfix :if

then*: enfix specialize :if [ ;-- THEN/ONLY is a path, can't dispatch infix
    only: true
]


; ALSO and ELSE are "non-TIGHTened" enfix functions which either pass through
; an argument or run a branch, based on nothing-ness of a value.  They take
; advantage of the pattern of conditionals such as `if condition [...]` to
; only return void if the branch does not run, and never return void or blank
; if it does run (void branch evaluations are forced to FALSE)  Forcing to
; false also makes them work with ANY and ALL, which use BLANK! as their
; no-match result.
;
; These could be implemented as specializations of the generic EITHER-TEST
; native.  But due to their common use they are hand-optimized into their own
; specialized natives: EITHER-TEST-NOTHING and EITHER-TEST-SOMETHING.

also: enfix redescribe [
    "Evaluate the branch if the left hand side expression isn't void or blank"
](
    comment [specialize 'either-test [test: :nothing?]]
    :either-test-nothing
)

also*: enfix redescribe [
    "Evaluate branch if left hand side isn't void, return branch result as-is"
](
    specialize 'either-test-void [only: true]
)

else: enfix redescribe [
    "Evaluate the branch if the left hand side expression is void or blank"
](
    comment [specialize 'either-test [test: :something?]]
    :either-test-something
)

else*: enfix redescribe [
    "Evaluate branch if left hand side is void, return branch result as-is"
](
    specialize 'either-test-value [only: true]
)


; SHORT-CIRCUIT BOOLEAN OPERATORS
;
; Traditionally Rebol's AND/OR/XOR infix operations were bitwise operations,
; not logical ones.  Because bitwise math is relatively uncommon, and most
; languages would use the words to mean "conditional" operations, it was a
; rather popular proposal to change the behavior:
;
; https://github.com/rebol/rebol-issues/issues/1879
;
; However, many languages have "short-circuit" behavior, e.g. the right hand
; side of an AND will not be evaluated if the left hand side is false.  In
; Rebol this can't be done without using a BLOCK! or a quoted group.  For the
; moment, Ren-C experiments with requiring the right hand side of a short
; circuit operation to be a GROUP! so that it may be quoted and then either
; executed or not.

and: enfix func [
    {Short-circuit boolean AND}

    return: [logic!]
    left [any-value!]
        {Expression which will always be evaluated}
    :right [group!]
        {Quoted expression, that will be evaluated only if LEFT is TRUTHY?}
][
    either left [to-logic do right] [false]
]

or: enfix func [
    {Short-circuit boolean AND}

    return: [logic!]
    left [any-value!]
        {Expression which will always be evaluated}
    :right [group!]
        {Quoted expression, that will be evaluated only if LEFT is FALSEY?}
][
    either left [true] [to-logic do right]
]

nor: enfix func [
    {Short-circuit boolean NOR}

    return: [logic!]
    left [any-value!]
        {Expression which will always be evaluated}
    :right [group!]
        {Quoted expression, that will be evaluated only if LEFT is FALSEY?}
][
    either left [false] [not do right]
]

nand: enfix func [
    {Short-circuit boolean NAND}

    return: [logic!]
    left [any-value!]
        {Expression which will always be evaluated}
    :right [group!]
        {Quoted expression, that will be evaluated only if LEFT is FALSEY?}
][
    either left [not do right] [true]
]


; There's no way to do a shortcut XOR...both sides have to be tested.
;
xor: enfix :xor?


; The -- and ++ operators were deemed too "C-like", so ME was created to allow
; `some-var: me + 1` or `some-var: me / 2` in a generic way.
;
; !!! This depends on a fairly lame hack called EVAL-ENFIX at the moment, but
; that evaluator exposure should be generalized more cleverly.
;
me: enfix func [
    {Update variable using it as the left hand argument to an enfix operator}

    return: [<opt> any-value!]
    :var [set-word! set-path!]
        {Variable to assign (and use as the left hand enfix argument)}
    :rest [<opt> any-value! <...>]
        {Code to run with var as left (first element should be enfixed)}
][
    set* var eval-enfix (get* var) rest
]

my: enfix func [
    {Update variable using it as the first argument to a prefix operator}

    return: [<opt> any-value!]
    :var [set-word! set-path!]
        {Variable to assign (and use as the first prefix argument)}
    :rest [<opt> any-value! <...>]
        {Code to run with var as left (first element should be prefix)}
][
    set* var eval-enfix/prefix (get* var) rest
]


; Lambdas are experimental quick function generators via a symbol
;
set/enfix (r3-alpha-quote "->") :lambda
set/enfix (r3-alpha-quote "<-") (specialize :lambda [only: true])


; These constructs used to be enfix to complete their left hand side.  Yet
; that form of completion was only one expression's worth, when they wanted
; to allow longer runs of evaluation.  "Invisible functions" (those which
; `return: []`) permit a more flexible version of the mechanic.

set (r3-alpha-quote "<|") :invisible-eval-all
set (r3-alpha-quote "|>") :right-bar
||: enfix :once-bar
