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

        Ren-C does not have an "OP!" function type, it just has ACTION!, but
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
; R3-ALPHA-LIT annotates to warn not to try and assign SET-WORD! forms, and
; to bind interned strings.
;
r3-alpha-lit: func [:spelling [word! text!]] [
    either word? spelling [
        spelling
    ][
        bind (to word! spelling) (binding of 'r3-alpha-lit)
    ]
]


; Make top-level words
;
+: -: *: and+: or+: xor+: _

for-each [math-op function-name] [
    +       add
    -       subtract
    *       multiply

    ; / is a 0-arity PATH! in Ren-C.  While "pathing" with a number on the
    ; left acts as division, it has slight differences.

    and+    intersect
    or+     union
    xor+    difference
][
    set/enfix math-op (get function-name)
]



; Make top-level words for things not added by %b-init.c
;
=: !=: ==: !==: =?: _

; <= looks a lot like a left arrow.  In the interest of "new thought", core
; defines the operation in terms of =<
;
lesser-or-equal?: :equal-or-lesser?

for-each [comparison-op function-name] compose [
    =       equal?
    <>      not-equal?
    <       lesser?
    (r3-alpha-lit "=<") equal-or-lesser?
    >       greater?
    >=      greater-or-equal?

    <=      lesser-or-equal? ;-- !!! https://forum.rebol.info/t/349/11

    !=      not-equal? ;-- !!! http://www.rebol.net/r3blogs/0017.html

    ==      strict-equal? ;-- !!! https://forum.rebol.info/t/349
    !==     strict-not-equal? ;-- !!! bad pairing, most would think !=

    =?      same?
][
    ; !!! See discussion about the future of comparison operators:
    ; https://forum.rebol.info/t/349
    ;
    set/enfix comparison-op (get function-name)
]


; Lambdas are experimental quick function generators via a symbol.
; The identity is used to shake up enfix ordering.
;
set/enfix (r3-alpha-lit "=>") :lambda

; <- is the SHOVE operator.  It grabs whatever is on the left and uses it as
; the first argument to whatever operation is on its right hand side.  It
; adopts the parameter convention of the right.  If the right's first argument
; is evaluative and the left is a SET-WORD! or SET-PATH!, it will grab the
; value of that and then run the assignment after the function is done.
;
; While both <- and -> are enfix operations, the -> variation does not follow
; the rules of enfix completion:
;
;     >> 10 <- lib/= 5 + 5
;     ** Script Error: + does not allow logic! for its value1 argument
;
;     >> 10 -> lib/= 5 + 5
;     == #[true]
;
set/enfix (r3-alpha-lit "<-") :shove/enfix
set/enfix (r3-alpha-lit "->") :shove


; The -- and ++ operators were deemed too "C-like", so ME was created to allow
; `some-var: me + 1` or `some-var: me / 2` in a generic way.  They share code
; with SHOVE, so it's folded into the implementation of that.

me: enfix redescribe [
    {Update variable using it as the left hand argument to an enfix operator}
](
    :shove/set/enfix  ; /ENFIX so `x: 1 | x: me + 1 * 10` is 20, not 11
)

my: enfix redescribe [
    {Update variable using it as the first argument to a prefix operator}
](
    :shove/set
)


; These constructs used to be enfix to complete their left hand side.  Yet
; that form of completion was only one expression's worth, when they wanted
; to allow longer runs of evaluation.  "Invisible functions" (those which
; `return: []`) permit a more flexible version of the mechanic.

set (r3-alpha-lit "<|") tweak copy :eval-all 'postpone on
set/enfix (r3-alpha-lit "|>") tweak copy :shove 'postpone on
||: :once-bar
