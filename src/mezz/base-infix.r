REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Infix operator symbol definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2016 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        When a variable is set to a function value with SET, there is an option
        to designate that particular binding as /LOOKBACK.  This means that
        when the function is invoked through that variable, its first argument
        will come from the left hand side--before the invoking WORD!.

        If the function has two parameters, this gives the effect of what
        Rebol2 called an "OP!" (infix operator).  However, Ren-C's choice to
        make this a separate flag to SET means it does not require a new
        datatype.  Any FUNCTION! of any arity can be used, and it will just
        get its first argument from the left, with the rest from the right.
        
        This file sets up the common "enfixed" operators.
    }
]

; Due to Rebol's complex division of lexical space, operations like `<` have
; needed special rules in the scanner code.  These rules may have permitted
; use of the WORD! form, but made the SET-WORD! forms illegal (e.g. `<:`).
;
; Ren-C allows more of these things, but if they were used in this file it
; could not be read by R3-Alpha; which is used to process this file for
; bootstrap.  So it brings the operators into existence in %b-init.c in
; the function Add_Lib_Keys_R3Alpha_Cant_Make().
; 
; These hacks are used to get the properly bound WORD!s.  Note that FIRST,
; LOAD, INTERN etc. are not in the definition order at this point...so
; PICK MAKE BLOCK! is used.
;
; Note also the unsets for these at the bottom of the file for "cleanliness."

lt: (pick [<] 1)
lteq: (pick [<=] 1)
gt: (pick [>] 1)
gteq: (pick [>=] 1)
dv: (pick [/] 1) ;-- "slash" is the character #"/"
dvdv: (pick [//] 1)
should-be-empty-tag: (pick [<>] 1)

right-arrow: bind (pick make block! "->" 1) context-of 'lambda
left-arrow: bind (pick make block! "<-" 1) context-of 'lambda
left-flag: bind (pick make block! "<|" 1) context-of 'lambda
right-flag: bind (pick make block! "|>" 1) context-of 'lambda


; While Ren-C has no particular concept of "infix OP!s" as a unique datatype,
; a function which is arity-2 and bound lookback to a variable acts similarly. 
; Yet the default is to obey the same lookahead rules as prefix operations
; historically applied.  Also, the left hand argument will be evaluated as
; complete an expression as it can.
;
; The <tight> annotation is long-term likely a legacy-only property, which
; requests as *minimal* a complete expression on a slot as possible.  So if 
; you have SOME-INFIX with tight parameters on the left and the right it
; would see:
;
;     add 1 2 some-infix add 1 2 + 10
;
; and interpret it as:
;
;     add 1 (2 some-infix add 1 2) + 10
;
; Whereas if the arguments were not tight, it would see this as:
;
;     (add 1 2) some-infix (add 1 2 + 10)
;
; For the moment while the features settle, the operators "in the box" are
; all wrapped to behave with tight left and right arguments.  Long term the
; feature is theorized to be unnecessary.
;

+: enfix tighten :add
-: enfix tighten :subtract
*: enfix tighten :multiply
**: enfix tighten :power

set/lookback dv tighten :divide
set/lookback dvdv tighten :remainder

=: enfix tighten :equal?
=?: enfix tighten :same?

==: enfix tighten :strict-equal?
!=: enfix tighten :not-equal?
!==: enfix tighten :strict-not-equal?

set/lookback should-be-empty-tag tighten :not-equal?

set/lookback lt tighten :lesser?
set/lookback lteq tighten :lesser-or-equal?

set/lookback gt tighten :greater?
set/lookback gteq tighten :greater-or-equal?

and: enfix tighten :and?
or: enfix tighten :or?
xor: enfix tighten :xor?
nor: enfix tighten :nor?

nand: enfix tighten :nand?
and*: enfix tighten :and~
or+: enfix tighten :or~
xor+: enfix tighten :xor~


; Postfix operator for asking the most existential question of Rebol...is it
; a Rebol value at all?  (non-void)
;
; !!! Originally in Rebol2 and R3-Alpha, ? was a synonym for HELP, which seems
; wasteful for the language as a whole when it's easy enough to type HELP.
; Postfix was not initially considered, because there was no ability of
; enfixed operators to force the left hand side of expressions to be as
; maximal as possible.  Hence `while [take blk ?] [...]` would ask if blk was
; void, not `take blk`.  So it was tried as a prefix operator, which wound
; up looking somewhat junky...now it's being tried as working postfix.

?: enfix :any-value?


; ELSE is an experiment to try and allow IF condition [branch1] ELSE [branch2]
; For efficiency it uses references to the branches and does not copy them
; into the body or protect them from mutation.  It is supported by the
; BRANCHER native, which still has the overhead of creating a function at this
; time but is still speedier than a user function.
;
else: enfix :brancher


; Lambdas are experimental quick function generators via a symbol
;
set/lookback right-arrow :lambda
set/lookback left-arrow (specialize :lambda [only: true])


; These usermode expression-barrier like constructs may not necessarily use
; their left-hand arguments...however by being enfixed and not having <tight>
; first args, they are able to force complete expressions to their left.

set/lookback left-flag :left-bar
set/lookback right-flag :right-bar
||: enfix :once-bar


; Clean up the words used to hold things that can't be made SET-WORD!s (or
; perhaps even words) in R3-Alpha

lt:
lteq:
gt:
gteq:
dv:
dvdv:
should-be-empty-tag:
right-arrow:
left-arrow:
left-flag:
right-flag:
    ()
