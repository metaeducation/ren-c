REBOL [
    System: "Ren-C Language Interpreter and Run-time Environment"
    Title: "Canonical Words Where Order or Optimization in Lib Lookup Matters"
    Rights: {
        Copyright 2012-2021 Ren-C Open Source Contributors
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        These are words that are used internally by Rebol, which and are
        canonized with small integer SYM_XXX constants.  These constants can
        be quickly used in switch() statements, and a lookup table is on
        hand to turn SYM_XXX integers into the corresponding symbol pointer.

        The reason ordered words are kept in a separate file is that the
        symbol space overlaps with the names of natives and generics that
        are put into lib.  Due to optimizations, this means words where the
        relative order matters need to establish their indexes in lib that
        the natives then must accomodate.

        Unordered words do not forcibly have slots in the optimized range of
        lib, so they are in a separate file.
    }
]

; The order of these don't matter, but getting to TRUE_VALUE and FALSE_VALUE
; quickly is useful.
;
true
false
on
off
yes
no

blank
blackhole

newline
space

; The `sys context` and `system object` are different things, which seems a
; bit confusing.  Review.
;
sys
system
lib

; When building PATH! and TUPLE! the system collapses 2-element sequences
; that are all BLANK! to these words.  Different meanings in bootstrap exe
; so just use strings.
;
"/"
"."

; Not loadable by bootstrap executable.
"@"
"^^"  ; actually just one caret
":"

; PARSE - These words must not be reserved above!!  The range of consecutive
; index numbers are used by PARSE to detect keywords.
;
set  ; must be first first (SYM_SET referred to by GET_VAR() in %u-parse.c)
let
copy  ; `copy x rule` deprecated, use `x: across rule` for this intent
across
collect  ; Variant in Red, but Ren-C's acts SET-like, suggested by @rgchris
keep
some
any  ; no longer a parse keyword, use OPT SOME FURTHER for precise meaning
further  ; https://forum.rebol.info/t/1593
opt
maybe  ; new
not  ; turned to _not_ for SYM__NOT_, see TO-C-NAME for why this is weird
and  ; turned to _and_ for SYM__AND_, see TO-C-NAME for why this is weird
ahead  ; Ren-C addition (also in Red)
remove
insert
change
if  ; deprecated: https://forum.rebol.info/t/968/7
fail
reject
while  ; no longer supported, use OPT SOME or MAYBE SOME for precise meaning
repeat
limit
seek  ; Ren-C addition
??
"|"  ; Text form to gloss over bootstrap issue (was BAR! datatype)
"||"
accept
break
; ^--prep words above
    return  ; removed: https://github.com/metaeducation/ren-c/pull/898
; v--match words below
skip
to
thru
quote  ; !!! kept for compatibility, but use THE instead
just
lit-word!  ; !!! simulated datatype constraint (a QUOTED! like 'x)
lit-path!  ; !!! simulated datatype costraint (a QUOTED! like 'x/y)
refinement!  ; !!! simulated datatype constraint (a PATH! like `/word`)
predicate!  ; !!! simulated datatype constraint (a TUPLE! like `.word`)
blackhole!  ; !!! simulated datatype constraint (the ISSUE! `#`)
char!  ; !!! simulated datatype constraint (single-element ISSUE!)
match  ; !!! no longer supported by PARSE3
do
into
only
end  ; must be last (SYM_END referred to by GET_VAR() in %u-parse.c)

; It is convenient to be able to say `for-each [_ x y] [1 2 3 ...] [...]` and
; let the blank indicate you are not interested in a value.  This might be
; doable with a generalized "anonymous key" system.  But for now it is assumed
; that all keys have unique symbols.
;
; To get the feature for today, we use some dummy symbols.  They cannot be
; used alongside the actual names `dummy1`, `dummy2`, etc...but rather
; than pick a more esoteric name then `map-each [_ dummy1] ...` is just an
; error.  Using simple names makes the frame more legible.
;
dummy1
dummy2
dummy3
dummy4
dummy5
dummy6
dummy7
dummy8
dummy9
